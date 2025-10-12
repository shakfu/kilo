# Undo/Redo System Design for Loki

## Overview

Undo/redo is one of the most critical missing features for production text editor use. This document analyzes where and how to implement a robust undo/redo system for loki.

## Current Edit Architecture

### Existing Edit Operations (src/loki_core.c)

```c
/* Three main user-facing edit operations */
void editor_insert_char(editor_ctx_t *ctx, int c);      // Line 591
void editor_insert_newline(editor_ctx_t *ctx);          // Line 613
void editor_del_char(editor_ctx_t *ctx);                // Line 649

/* Lower-level row operations (internal) */
static void editor_insert_row(editor_ctx_t *ctx, int at, char *s, size_t len);
static void editor_del_row(editor_ctx_t *ctx, int at);
static void editor_row_insert_char(editor_ctx_t *ctx, t_erow *row, int at, int c);
static void editor_row_del_char(editor_ctx_t *ctx, t_erow *row, int at);
static void editor_row_append_string(editor_ctx_t *ctx, t_erow *row, char *s, size_t len);
```

### Edit Flow Analysis

All user edits flow through the three public functions:
- **Insert character**: `editor_insert_char()` → `editor_row_insert_char()`
- **Delete character**: `editor_del_char()` → `editor_row_del_char()` or `editor_del_row()` (if deleting newline)
- **Insert newline**: `editor_insert_newline()` → `editor_insert_row()` and possibly row split

This clean architecture means we only need to intercept 3 functions to capture all edits.

## Where Should Undo/Redo Live?

### Option A: Extend loki_core.c (~200 lines added)
- **Pros**: All buffer operations in one place, simpler mental model
- **Cons**: File grows to ~1,536 lines, mixing core buffer logic with undo state management
- **Best for**: Minimal implementation with no grouping, simple undo/redo

### Option B: New file src/loki_undo.c (~350-450 lines)
- **Pros**: Separation of concerns, easier to test, can be disabled via compile flag
- **Cons**: Extra file, slight indirection
- **Best for**: Production-quality undo with grouping, memory limits, undo branches

### **Recommendation: Option B - Create dedicated src/loki_undo.c**

Here's why:

1. **Single Responsibility**: Undo state management is distinct from buffer manipulation
2. **Testability**: Can unit test undo logic independently from editor core
3. **Memory Management**: Undo requires circular buffers, memory limits, cleanup - this is complex enough to deserve its own module
4. **Undo Grouping**: Production undo needs to group operations (typing a word = 1 undo), which is separate logic
5. **Future Extensions**: Undo branches (vim-style undo tree), persistent undo - easier in dedicated module

## Implementation Design

### File Structure

```
src/
├── loki_core.c         # Buffer operations (insert/delete)
├── loki_undo.c         # Undo/redo state management (NEW)
└── loki_undo.h         # Undo API (NEW)

include/loki/
└── core.h              # Add undo/redo public API
```

### 1. Core Data Structures

**src/loki_undo.h:**

```c
#ifndef LOKI_UNDO_H
#define LOKI_UNDO_H

#include "loki/core.h"

/* Operation types that can be undone */
typedef enum {
    UNDO_INSERT_CHAR,    /* Insert single character */
    UNDO_DELETE_CHAR,    /* Delete single character */
    UNDO_INSERT_LINE,    /* Insert newline */
    UNDO_DELETE_LINE,    /* Delete entire line */
    UNDO_REPLACE_CHAR,   /* Replace character (future: for overwrite mode) */
} undo_op_type_t;

/* Single undo operation */
typedef struct {
    undo_op_type_t type;     /* Operation type */
    int row;                 /* Row where operation occurred (file coordinates) */
    int col;                 /* Column where operation occurred */

    /* Operation-specific data */
    union {
        struct {
            char ch;         /* Character inserted/deleted */
        } char_op;

        struct {
            char *content;   /* Line content (for line ops) */
            int length;      /* Content length */
        } line_op;
    } data;

    /* Cursor position after operation (for redo) */
    int cursor_row;
    int cursor_col;

    /* Grouping information */
    int group_id;            /* Operations with same group_id undo together */
    int group_break;         /* 1 = start new group after this operation */
} undo_entry_t;

/* Undo state (embedded in editor_ctx_t) */
typedef struct {
    undo_entry_t *entries;   /* Circular buffer of undo entries */
    int capacity;            /* Max entries (e.g., 1000) */
    int count;               /* Current number of entries */
    int head;                /* Write position (next entry goes here) */
    int current;             /* Current position in undo stack */

    int next_group_id;       /* Next group ID to assign */
    int current_group_id;    /* Current operation group */

    /* Grouping heuristics */
    time_t last_edit_time;   /* Timestamp of last edit */
    int last_edit_row;       /* Row of last edit */
    int last_edit_col;       /* Column of last edit */
    undo_op_type_t last_op;  /* Type of last operation */

    /* Memory tracking */
    size_t memory_used;      /* Bytes used by undo data */
    size_t memory_limit;     /* Max bytes (e.g., 10MB) */
} undo_state_t;

/* ======================== Public API ======================== */

/* Initialize undo system */
void undo_init(editor_ctx_t *ctx, int capacity, size_t memory_limit);

/* Free undo system resources */
void undo_free(editor_ctx_t *ctx);

/* Record an edit operation (called by editor_insert_char, etc.) */
void undo_record_insert_char(editor_ctx_t *ctx, int row, int col, char ch);
void undo_record_delete_char(editor_ctx_t *ctx, int row, int col, char ch);
void undo_record_insert_line(editor_ctx_t *ctx, int row, const char *content, int length);
void undo_record_delete_line(editor_ctx_t *ctx, int row, const char *content, int length);

/* Force start of new undo group (e.g., after mode change, after delay) */
void undo_break_group(editor_ctx_t *ctx);

/* Undo last operation/group */
int undo_perform(editor_ctx_t *ctx);

/* Redo previously undone operation/group */
int redo_perform(editor_ctx_t *ctx);

/* Check if undo/redo available */
int undo_can_undo(editor_ctx_t *ctx);
int undo_can_redo(editor_ctx_t *ctx);

/* Clear all undo history (e.g., after file save or major change) */
void undo_clear(editor_ctx_t *ctx);

/* Get undo statistics (for debugging/status display) */
void undo_get_stats(editor_ctx_t *ctx, int *undo_levels, int *redo_levels, size_t *memory);

#endif /* LOKI_UNDO_H */
```

### 2. Implementation: src/loki_undo.c

```c
/* loki_undo.c - Undo/Redo state management
 *
 * Implements undo/redo with operation grouping and memory limits.
 * Operations are grouped by heuristics (time gap, cursor movement, operation type).
 */

#include "loki_undo.h"
#include "loki_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Undo grouping heuristics */
#define UNDO_GROUP_TIMEOUT_MS 1000   /* 1 second gap = new group */
#define UNDO_GROUP_MOVEMENT_GAP 2    /* Cursor moved >2 positions = new group */

/* ======================== Initialization ======================== */

void undo_init(editor_ctx_t *ctx, int capacity, size_t memory_limit) {
    undo_state_t *undo = malloc(sizeof(undo_state_t));
    if (!undo) return;

    undo->entries = calloc(capacity, sizeof(undo_entry_t));
    if (!undo->entries) {
        free(undo);
        return;
    }

    undo->capacity = capacity;
    undo->count = 0;
    undo->head = 0;
    undo->current = 0;
    undo->next_group_id = 1;
    undo->current_group_id = 0;
    undo->last_edit_time = 0;
    undo->last_edit_row = -1;
    undo->last_edit_col = -1;
    undo->last_op = -1;
    undo->memory_used = 0;
    undo->memory_limit = memory_limit;

    ctx->undo_state = undo;
}

void undo_free(editor_ctx_t *ctx) {
    if (!ctx->undo_state) return;

    undo_state_t *undo = ctx->undo_state;

    /* Free all line_op content strings */
    for (int i = 0; i < undo->count; i++) {
        undo_entry_t *e = &undo->entries[i];
        if ((e->type == UNDO_INSERT_LINE || e->type == UNDO_DELETE_LINE) &&
            e->data.line_op.content) {
            free(e->data.line_op.content);
        }
    }

    free(undo->entries);
    free(undo);
    ctx->undo_state = NULL;
}

/* ======================== Grouping Logic ======================== */

/* Should we start a new undo group for this operation? */
static int should_break_group(undo_state_t *undo, undo_op_type_t op,
                               int row, int col) {
    if (undo->current_group_id == 0) return 1;  /* First operation */

    /* Time gap check */
    time_t now = time(NULL);
    if (now - undo->last_edit_time > (UNDO_GROUP_TIMEOUT_MS / 1000)) {
        return 1;
    }

    /* Operation type change (insert→delete or vice versa) */
    if (undo->last_op == UNDO_INSERT_CHAR && op == UNDO_DELETE_CHAR) return 1;
    if (undo->last_op == UNDO_DELETE_CHAR && op == UNDO_INSERT_CHAR) return 1;

    /* Line operations always break groups */
    if (op == UNDO_INSERT_LINE || op == UNDO_DELETE_LINE) return 1;

    /* Cursor jumped (user moved cursor manually) */
    int row_gap = abs(row - undo->last_edit_row);
    int col_gap = abs(col - undo->last_edit_col);
    if (row_gap > 0 || col_gap > UNDO_GROUP_MOVEMENT_GAP) return 1;

    /* Special case: inserting space breaks word group */
    /* (Implemented when recording char) */

    return 0;  /* Continue current group */
}

void undo_break_group(editor_ctx_t *ctx) {
    if (!ctx->undo_state) return;

    undo_state_t *undo = ctx->undo_state;
    if (undo->count > 0) {
        int last_idx = (undo->head - 1 + undo->capacity) % undo->capacity;
        undo->entries[last_idx].group_break = 1;
    }
    undo->current_group_id = 0;  /* Force new group on next operation */
}

/* ======================== Recording Operations ======================== */

static void record_operation(editor_ctx_t *ctx, undo_entry_t *entry) {
    undo_state_t *undo = ctx->undo_state;
    if (!undo) return;

    /* Check if we should start new group */
    if (should_break_group(undo, entry->type, entry->row, entry->col)) {
        undo->current_group_id = undo->next_group_id++;
    }

    entry->group_id = undo->current_group_id;
    entry->group_break = 0;

    /* Update grouping heuristics */
    undo->last_edit_time = time(NULL);
    undo->last_edit_row = entry->row;
    undo->last_edit_col = entry->col;
    undo->last_op = entry->type;

    /* If we've undone operations, discard redo history */
    if (undo->current < undo->count) {
        /* Free any line content in discarded entries */
        for (int i = undo->current; i < undo->count; i++) {
            int idx = (undo->head - undo->count + i + undo->capacity) % undo->capacity;
            undo_entry_t *e = &undo->entries[idx];
            if ((e->type == UNDO_INSERT_LINE || e->type == UNDO_DELETE_LINE) &&
                e->data.line_op.content) {
                undo->memory_used -= e->data.line_op.length;
                free(e->data.line_op.content);
                e->data.line_op.content = NULL;
            }
        }
        undo->count = undo->current;
    }

    /* Add entry to circular buffer */
    if (undo->count == undo->capacity) {
        /* Buffer full - evict oldest entry */
        int evict_idx = undo->head;
        undo_entry_t *evict = &undo->entries[evict_idx];
        if ((evict->type == UNDO_INSERT_LINE || evict->type == UNDO_DELETE_LINE) &&
            evict->data.line_op.content) {
            undo->memory_used -= evict->data.line_op.length;
            free(evict->data.line_op.content);
        }
    } else {
        undo->count++;
    }

    /* Write entry */
    undo->entries[undo->head] = *entry;
    undo->head = (undo->head + 1) % undo->capacity;
    undo->current = undo->count;

    /* Track memory for line operations */
    if (entry->type == UNDO_INSERT_LINE || entry->type == UNDO_DELETE_LINE) {
        undo->memory_used += entry->data.line_op.length;
    }
}

void undo_record_insert_char(editor_ctx_t *ctx, int row, int col, char ch) {
    if (!ctx->undo_state) return;

    undo_entry_t entry = {
        .type = UNDO_INSERT_CHAR,
        .row = row,
        .col = col,
        .data.char_op.ch = ch,
        .cursor_row = row,
        .cursor_col = col + 1  /* Cursor after insert */
    };

    record_operation(ctx, &entry);
}

void undo_record_delete_char(editor_ctx_t *ctx, int row, int col, char ch) {
    if (!ctx->undo_state) return;

    undo_entry_t entry = {
        .type = UNDO_DELETE_CHAR,
        .row = row,
        .col = col,
        .data.char_op.ch = ch,
        .cursor_row = row,
        .cursor_col = col  /* Cursor after delete */
    };

    record_operation(ctx, &entry);
}

void undo_record_insert_line(editor_ctx_t *ctx, int row,
                              const char *content, int length) {
    if (!ctx->undo_state) return;

    undo_entry_t entry = {
        .type = UNDO_INSERT_LINE,
        .row = row,
        .col = 0,
        .data.line_op.content = strdup(content),
        .data.line_op.length = length,
        .cursor_row = row + 1,
        .cursor_col = 0
    };

    record_operation(ctx, &entry);
}

void undo_record_delete_line(editor_ctx_t *ctx, int row,
                              const char *content, int length) {
    if (!ctx->undo_state) return;

    undo_entry_t entry = {
        .type = UNDO_DELETE_LINE,
        .row = row,
        .col = 0,
        .data.line_op.content = strdup(content),
        .data.line_op.length = length,
        .cursor_row = row,
        .cursor_col = 0
    };

    record_operation(ctx, &entry);
}

/* ======================== Undo/Redo Operations ======================== */

/* Undo single entry (low-level) */
static void undo_single_entry(editor_ctx_t *ctx, undo_entry_t *entry) {
    /* Suppress undo recording while applying undo */
    undo_state_t *saved_state = ctx->undo_state;
    ctx->undo_state = NULL;

    switch (entry->type) {
        case UNDO_INSERT_CHAR:
            /* Undo insert = delete the character */
            /* TODO: Call internal delete function */
            break;

        case UNDO_DELETE_CHAR:
            /* Undo delete = re-insert the character */
            /* TODO: Call internal insert function */
            break;

        case UNDO_INSERT_LINE:
            /* Undo line insert = delete the line */
            /* TODO: Call editor_del_row() */
            break;

        case UNDO_DELETE_LINE:
            /* Undo line delete = re-insert the line */
            /* TODO: Call editor_insert_row() */
            break;
    }

    /* Restore undo state */
    ctx->undo_state = saved_state;
}

int undo_perform(editor_ctx_t *ctx) {
    undo_state_t *undo = ctx->undo_state;
    if (!undo || undo->current == 0) return 0;  /* Nothing to undo */

    /* Find start of current group */
    int target_group = -1;
    int undo_idx = undo->current - 1;

    for (int i = undo->current - 1; i >= 0; i--) {
        int idx = (undo->head - undo->count + i + undo->capacity) % undo->capacity;
        undo_entry_t *e = &undo->entries[idx];

        if (target_group == -1) {
            target_group = e->group_id;
        } else if (e->group_id != target_group) {
            break;  /* Different group, stop here */
        }
        undo_idx = i;
    }

    /* Undo all operations in this group (in reverse order) */
    for (int i = undo->current - 1; i >= undo_idx; i--) {
        int idx = (undo->head - undo->count + i + undo->capacity) % undo->capacity;
        undo_single_entry(ctx, &undo->entries[idx]);
    }

    undo->current = undo_idx;
    return 1;
}

int redo_perform(editor_ctx_t *ctx) {
    undo_state_t *undo = ctx->undo_state;
    if (!undo || undo->current >= undo->count) return 0;  /* Nothing to redo */

    /* Find end of next group */
    int idx = (undo->head - undo->count + undo->current + undo->capacity) % undo->capacity;
    int target_group = undo->entries[idx].group_id;
    int redo_end = undo->current;

    for (int i = undo->current; i < undo->count; i++) {
        idx = (undo->head - undo->count + i + undo->capacity) % undo->capacity;
        undo_entry_t *e = &undo->entries[idx];

        if (e->group_id != target_group) break;
        redo_end = i + 1;
    }

    /* Redo all operations in this group (in forward order) */
    /* TODO: Implement redo (replay operations) */

    undo->current = redo_end;
    return 1;
}

/* ======================== Query Functions ======================== */

int undo_can_undo(editor_ctx_t *ctx) {
    undo_state_t *undo = ctx->undo_state;
    return undo && undo->current > 0;
}

int undo_can_redo(editor_ctx_t *ctx) {
    undo_state_t *undo = ctx->undo_state;
    return undo && undo->current < undo->count;
}

void undo_clear(editor_ctx_t *ctx) {
    if (!ctx->undo_state) return;

    undo_state_t *undo = ctx->undo_state;

    /* Free all line content */
    for (int i = 0; i < undo->count; i++) {
        undo_entry_t *e = &undo->entries[i];
        if ((e->type == UNDO_INSERT_LINE || e->type == UNDO_DELETE_LINE) &&
            e->data.line_op.content) {
            free(e->data.line_op.content);
            e->data.line_op.content = NULL;
        }
    }

    undo->count = 0;
    undo->head = 0;
    undo->current = 0;
    undo->memory_used = 0;
}

void undo_get_stats(editor_ctx_t *ctx, int *undo_levels,
                     int *redo_levels, size_t *memory) {
    undo_state_t *undo = ctx->undo_state;
    if (!undo) {
        if (undo_levels) *undo_levels = 0;
        if (redo_levels) *redo_levels = 0;
        if (memory) *memory = 0;
        return;
    }

    if (undo_levels) *undo_levels = undo->current;
    if (redo_levels) *redo_levels = undo->count - undo->current;
    if (memory) *memory = undo->memory_used;
}
```

### 3. Integration with Editor Core

**Modify src/loki_core.c:**

```c
#include "loki_undo.h"

void editor_insert_char(editor_ctx_t *ctx, int c) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    /* ... existing insertion logic ... */

    /* Record undo AFTER successful operation */
    undo_record_insert_char(ctx, filerow, filecol, c);
}

void editor_del_char(editor_ctx_t *ctx) {
    int filerow = ctx->rowoff+ctx->cy;
    int filecol = ctx->coloff+ctx->cx;
    /* ... */

    /* Get character BEFORE deleting */
    char deleted_char = /* ... extract from row ... */;

    /* ... perform deletion ... */

    /* Record undo */
    undo_record_delete_char(ctx, filerow, filecol, deleted_char);
}
```

### 4. Modal Integration

**In src/loki_modal.c:**

```c
static void process_normal_mode(editor_ctx_t *ctx, int fd) {
    int c = terminal_read_key(fd);

    switch (c) {
        case 'u':  /* Undo */
            if (undo_perform(ctx)) {
                editor_set_status_msg(ctx, "Undo");
            } else {
                editor_set_status_msg(ctx, "Already at oldest change");
            }
            break;

        case CTRL_R:  /* Redo */
            if (redo_perform(ctx)) {
                editor_set_status_msg(ctx, "Redo");
            } else {
                editor_set_status_msg(ctx, "Already at newest change");
            }
            break;

        case 'i':  /* Enter insert mode */
            undo_break_group(ctx);  /* Break undo group on mode change */
            ctx->mode = MODE_INSERT;
            break;

        /* ... existing cases ... */
    }
}
```

### 5. Update Editor Context

**Add to src/loki_internal.h:**

```c
struct editor_ctx {
    /* ... existing fields ... */

    /* Undo/redo state */
    struct undo_state *undo_state;  /* NULL if undo disabled */
};
```

### 6. Lua Integration

**Add to src/loki_lua.c:**

```c
/* loki.undo() - Undo last operation */
static int lua_loki_undo(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    int result = undo_perform(ctx);
    lua_pushboolean(L, result);
    return 1;
}

/* loki.redo() - Redo operation */
static int lua_loki_redo(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    int result = redo_perform(ctx);
    lua_pushboolean(L, result);
    return 1;
}

/* Register in loki table */
lua_pushcfunction(L, lua_loki_undo);
lua_setfield(L, -2, "undo");

lua_pushcfunction(L, lua_loki_redo);
lua_setfield(L, -2, "redo");
```

## Memory and Performance Analysis

### Memory Overhead

**Per-entry overhead:**
- Character operation: `~64 bytes` (struct with small union)
- Line operation: `~64 bytes + line_length`

**Example configurations:**

| Capacity | Avg Line Length | Total Memory |
|----------|----------------|--------------|
| 100      | N/A (char ops) | ~6 KB        |
| 1,000    | N/A (char ops) | ~64 KB       |
| 1,000    | 50 chars       | ~114 KB      |
| 5,000    | 50 chars       | ~570 KB      |

**Recommended defaults:**
- **Capacity**: 1,000 operations (~64 KB for char-only edits)
- **Memory limit**: 10 MB (allows ~200K chars of line undo data)

### Performance Impact

**Recording overhead:**
- Character insert/delete: `O(1)` - just store char + position
- Line insert/delete: `O(n)` - copy line content (unavoidable)
- Grouping check: `O(1)` - simple comparisons

**Undo/redo performance:**
- Single operation: `O(1)` buffer manipulation
- Group of N operations: `O(N)` - must undo each in sequence
- Typical group size: 5-50 operations (typing a word)

**Impact on normal editing:** Negligible (<1% overhead)

## Undo Grouping Strategy

### Heuristics for Grouping

1. **Time-based**: >1 second gap = new group
2. **Operation type change**: Insert→Delete or Delete→Insert = new group
3. **Cursor movement**: Cursor jumped >2 positions = new group
4. **Mode changes**: NORMAL→INSERT = new group
5. **Whitespace**: Inserting space after non-space = new group (word boundary)
6. **Explicit breaks**: Commands like `:w` force new group

### Example Grouping Scenarios

```
User types "hello world"
→ Group 1: "hello" (5 insert_char ops)
→ Group 2: " world" (6 insert_char ops, break on space)
→ Undo once: removes " world"
→ Undo again: removes "hello"

User types "foo", moves cursor, types "bar"
→ Group 1: "foo" (3 insert_char ops)
→ Group 2: "bar" (3 insert_char ops, break on cursor movement)
```

## Testing Strategy

### New Test File: tests/test_undo.c

```c
#include "test_framework.h"
#include "loki_internal.h"
#include "loki_undo.h"

TEST(undo_init) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    undo_init(&ctx, 100, 10 * 1024 * 1024);
    ASSERT_TRUE(ctx.undo_state != NULL);

    undo_free(&ctx);
    editor_ctx_free(&ctx);
}

TEST(undo_insert_char) {
    editor_ctx_t ctx;
    init_test_ctx(&ctx, "");
    undo_init(&ctx, 100, 10 * 1024 * 1024);

    /* Insert "abc" */
    editor_insert_char(&ctx, 'a');
    editor_insert_char(&ctx, 'b');
    editor_insert_char(&ctx, 'c');

    /* Verify buffer contains "abc" */
    ASSERT_STR_EQ(ctx.row[0].chars, "abc");

    /* Undo - should remove "abc" as one group */
    undo_perform(&ctx);
    ASSERT_STR_EQ(ctx.row[0].chars, "");

    free_test_ctx(&ctx);
}

TEST(undo_delete_char) {
    editor_ctx_t ctx;
    init_test_ctx(&ctx, "hello");
    undo_init(&ctx, 100, 10 * 1024 * 1024);

    ctx.cx = 5;  /* Position at end */
    editor_del_char(&ctx);  /* Delete 'o' */

    ASSERT_STR_EQ(ctx.row[0].chars, "hell");

    undo_perform(&ctx);
    ASSERT_STR_EQ(ctx.row[0].chars, "hello");

    free_test_ctx(&ctx);
}

TEST(undo_grouping_time) {
    /* Test that time gap breaks undo groups */
}

TEST(undo_grouping_cursor_movement) {
    /* Test that cursor jump breaks undo groups */
}

TEST(undo_redo_chain) {
    /* Test undo→redo→undo→redo sequence */
}

TEST(undo_memory_limit) {
    /* Test that memory limit evicts old entries */
}

TEST(undo_circular_buffer) {
    /* Test that capacity limit evicts oldest */
}
```

## Build System Integration

**CMakeLists.txt:**

```cmake
# Optional undo/redo support
option(LOKI_ENABLE_UNDO "Enable undo/redo functionality" ON)

if (LOKI_ENABLE_UNDO)
    target_compile_definitions(libloki PRIVATE LOKI_HAVE_UNDO=1)
    target_sources(libloki PRIVATE src/loki_undo.c)
endif()

# Add undo tests
if (LOKI_ENABLE_UNDO)
    add_executable(test_undo tests/test_undo.c)
    target_include_directories(test_undo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(test_undo PRIVATE libloki test_framework)
    add_test(NAME test_undo COMMAND test_undo)
endif()
```

## Estimated Implementation Size

**Component breakdown:**

| File               | Estimated Lines | Complexity |
|--------------------|----------------|------------|
| loki_undo.h        | ~120 lines     | Low        |
| loki_undo.c        | ~400 lines     | Medium     |
| Integration (core) | ~30 lines      | Low        |
| Integration (modal)| ~20 lines      | Low        |
| Lua bindings       | ~30 lines      | Low        |
| Tests              | ~300 lines     | Medium     |
| **Total**          | **~900 lines** | **Medium** |

## Implementation Phases

### Phase 1: Basic Undo (No Grouping) - 2-3 days
1. Implement `loki_undo.c` with circular buffer (no grouping)
2. Record character insert/delete operations
3. Implement undo/redo for character operations
4. Add `u` and `Ctrl-R` keybindings
5. Basic tests (15 tests)

### Phase 2: Undo Grouping - 1-2 days
1. Implement grouping heuristics (time, cursor movement)
2. Group operations by context
3. Test grouping scenarios (10 tests)

### Phase 3: Line Operations - 1-2 days
1. Add newline insert/delete tracking
2. Handle line split/merge in undo
3. Memory management for line content
4. Test line operations (10 tests)

### Phase 4: Polish & Integration - 1 day
1. Lua API (undo/redo functions)
2. Status bar integration (show undo levels)
3. Memory limit enforcement
4. Documentation

**Total estimated effort: 5-8 days for complete implementation**

## Future Enhancements

### Undo Branches (Vim-style Undo Tree)
Instead of discarding redo history, maintain tree of all edits:

```
Initial: ""
  ↓ insert "hello"
"hello"
  ├→ delete "o" → "hell" → insert "p" → "hellp"
  └→ insert " world" → "hello world"
```

This allows navigating to any previous state without losing work.

### Persistent Undo
Save undo history to disk (`.loki/undo/<filename>.undo`):
- Survive editor restarts
- Useful for long-lived files
- Compression needed for large undo files

### Undo Visualization
Show undo tree graphically (like `:undotree` in vim):
```
:undotree

   1 "hello"
   |\
   2 3 "hell"    "hello world"
   |
   4 "hellp"

Current: node 3
```

## Summary

**Recommendation:** Implement in dedicated `src/loki_undo.c` module.

**Key design points:**
1. **Circular buffer** for memory efficiency (1,000 operations ≈ 64 KB)
2. **Operation grouping** via heuristics (time, cursor movement, operation type)
3. **Three integration points**: `editor_insert_char`, `editor_del_char`, `editor_insert_newline`
4. **Vim-compatible keybindings**: `u` for undo, `Ctrl-R` for redo
5. **Memory limits** to prevent unbounded growth (default 10 MB)

**Benefits:**
- **Production-ready**: Undo is essential for serious text editing
- **Minimal overhead**: <1% performance impact, ~64 KB memory for typical use
- **Clean architecture**: Separate module, easy to test and enhance
- **User-friendly**: Intelligent grouping (undo word-by-word, not char-by-char)

**Next steps if implementing:**
1. Create `src/loki_undo.c` and `src/loki_undo.h`
2. Implement Phase 1 (basic undo without grouping)
3. Add integration points to `editor_insert_char`, etc.
4. Write comprehensive tests
5. Add grouping heuristics (Phase 2)
6. Polish and document
