# Removing the Global Singleton: Context Passing Architecture

**Status:** Phase 1 Complete (Infrastructure Ready)
**Created:** 2025-10-11
**Updated:** 2025-10-11
**Purpose:** Enable split windows and proper multi-buffer support

**Progress:**
- [x] Phase 1: Context structure and helper functions (COMPLETE)
- ⏸ Phase 2-6: Migration of existing code (NOT STARTED)

---

## Executive Summary

The current Loki architecture uses a global singleton `struct editorConfig E` that fundamentally prevents:
1. **Split windows** - Multiple independent viewports (completely blocked)
2. **Tabs/multiple buffers** - Clean buffer management (awkward workarounds only)

This document outlines the architectural changes required to eliminate the global singleton and implement explicit context passing, enabling these features.

**Effort Estimate:** 2-3 days, ~3000-4000 line changes
**Risk Level:** High (touches core architecture)
**Benefit:** Unlocks split windows, clean tab implementation, better testability

---

## Current Architecture (The Problem)

### Global Singleton Definition

**Location:** `src/loki_internal.h:132-161`

```c
struct loki_editor_instance {
    int cx, cy;           // Cursor position
    int rowoff, coloff;   // Viewport offset
    int screenrows, screencols;  // Terminal dimensions
    int screenrows_total; // Rows available after status bars
    int numrows;          // Buffer line count
    int rawmode;          // Terminal raw mode flag
    t_erow *row;          // Buffer content
    int dirty;            // Modification flag
    char *filename;       // Current file
    char statusmsg[80];   // Status bar message
    time_t statusmsg_time;
    struct t_editor_syntax *syntax;  // Current syntax
    lua_State *L;         // Lua state
    t_lua_repl repl;      // REPL state
    EditorMode mode;      // Modal editing mode
    int word_wrap;        // Word wrap flag
    int sel_active;       // Selection active flag
    int sel_start_x, sel_start_y;  // Selection start
    int sel_end_x, sel_end_y;      // Selection end
    t_hlcolor colors[9];  // Syntax colors
};

// Global singleton instance (in src/loki_core.c:44)
extern struct loki_editor_instance E;
```

### How It's Used Throughout Codebase

**Direct global access (~2000+ sites):**

```c
// src/loki_core.c - cursor movement
void editor_move_cursor(int key) {
    int filerow = E.rowoff + E.cy;  // Direct access
    int filecol = E.coloff + E.cx;  // Direct access
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];  // Direct access
    // ...
}

// src/loki_core.c - screen rendering
void editor_refresh_screen(void) {
    if (E.cy >= E.screenrows) {  // Direct access
        E.cy = E.screenrows - 1;
    }
    // ...
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = E.rowoff + y;
        if (filerow >= E.numrows) {
            // ...
        }
    }
}

// src/loki_core.c - text insertion
void editor_insert_char(int c) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    if (filerow == E.numrows) {
        editor_insert_row(E.numrows, "", 0);
    }
    editor_row_insert_char(&E.row[filerow], filecol, c);
    // ...
}
```

### Why This Blocks Critical Features

#### 1. Split Windows - **Impossible**

Split windows require **multiple independent viewports** viewing the same or different buffers:

```
┌─────────────────────────────────┐
│ main.c (line 100, col 10)       │  viewport A
│   cursor here: E.cx=10, E.cy=5  │
├─────────────────────────────────┤
│ main.c (line 500, col 20)       │  viewport B
│      cursor here: E.cx=20 ???   │  ← CONFLICT: can't have two cx values!
└─────────────────────────────────┘
```

**Cannot represent:**
- Two cursors in same buffer (need independent `cx`, `cy`)
- Two viewports at different scroll positions (need independent `rowoff`, `coloff`)
- Two terminal regions (need independent `screenrows`, `screencols`)

#### 2. Multiple Buffers (Tabs) - **Awkward**

Current workaround requires full state swapping:

```c
// Awkward approach with global singleton
buffer_t saved_buffers[10];
int current_buffer = 0;

void switch_to_buffer(int idx) {
    // Save ALL state from global E
    saved_buffers[current_buffer].rows = E.row;
    saved_buffers[current_buffer].numrows = E.numrows;
    saved_buffers[current_buffer].cx = E.cx;
    saved_buffers[current_buffer].cy = E.cy;
    saved_buffers[current_buffer].rowoff = E.rowoff;
    saved_buffers[current_buffer].coloff = E.coloff;
    saved_buffers[current_buffer].filename = E.filename;
    saved_buffers[current_buffer].dirty = E.dirty;
    // ... 40+ more fields to save/restore!

    // Load new state into global E
    E.row = saved_buffers[idx].rows;
    E.numrows = saved_buffers[idx].numrows;
    // ... 40+ more fields to restore!

    current_buffer = idx;
}
```

**Problems:**
- Error-prone: easy to miss fields
- Performance overhead: copying entire state
- Maintenance burden: every new field needs swap code
- Ugly: violates single responsibility principle

---

## Target Architecture (The Solution)

### Context Passing Pattern

**Core idea:** Replace global singleton with explicit context parameter passed to all functions.

### New Data Structure

```c
// src/loki_internal.h

/* Editor context - one instance per editor viewport/buffer */
typedef struct editor_ctx {
    /* Buffer content */
    t_erow *row;
    int numrows;
    char *filename;
    int dirty;
    struct editorSyntax *syntax;

    /* Cursor position (viewport-specific) */
    int cx, cy;

    /* Viewport (viewport-specific) */
    int rowoff, coloff;
    int screenrows, screencols;

    /* Modal editing state */
    int mode;
    int sel_start_row, sel_start_col;
    int sel_end_row, sel_end_col;
    int sel_active;
    t_erow *clipboard;
    int clipboard_len;

    /* Status bar */
    char statusmsg[80];
    time_t statusmsg_time;

    /* Lua state (shared across all contexts) */
    lua_State *L;  // Note: this stays global or becomes shared resource

    /* REPL (could be shared or per-context) */
    t_lua_repl repl;
} editor_ctx_t;
```

### Function Signature Changes

**Before (global access):**
```c
void editor_move_cursor(int key);
void editor_refresh_screen(void);
void editor_insert_char(int c);
int editor_open(char *filename);
```

**After (context passing):**
```c
void editor_move_cursor(editor_ctx_t *ctx, int key);
void editor_refresh_screen(editor_ctx_t *ctx);
void editor_insert_char(editor_ctx_t *ctx, int c);
int editor_open(editor_ctx_t *ctx, char *filename);
```

### Example Conversions

#### Example 1: Cursor Movement

**Before:**
```c
void editor_move_cursor(int key) {
    int filerow = E.rowoff + E.cy;
    int filecol = E.coloff + E.cx;
    t_erow *row = (filerow >= E.numrows) ? NULL : &E.row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (E.cx == 0) {
            if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[filerow-1].size;
            }
        } else {
            E.cx--;
        }
        break;
    // ...
    }
}
```

**After:**
```c
void editor_move_cursor(editor_ctx_t *ctx, int key) {
    int filerow = ctx->rowoff + ctx->cy;
    int filecol = ctx->coloff + ctx->cx;
    t_erow *row = (filerow >= ctx->numrows) ? NULL : &ctx->row[filerow];

    switch(key) {
    case ARROW_LEFT:
        if (ctx->cx == 0) {
            if (ctx->cy > 0) {
                ctx->cy--;
                ctx->cx = ctx->row[filerow-1].size;
            }
        } else {
            ctx->cx--;
        }
        break;
    // ...
    }
}
```

**Change pattern:** `E.field` → `ctx->field` (mechanical search-replace)

#### Example 2: Screen Rendering

**Before:**
```c
void editor_refresh_screen(void) {
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6);  // Hide cursor
    ab_append(&ab, "\x1b[H", 3);     // Cursor to top-left

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    // Position cursor
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (E.cy - E.rowoff) + 1,
             (E.cx - E.coloff) + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[?25h", 6);  // Show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}
```

**After:**
```c
void editor_refresh_screen(editor_ctx_t *ctx) {
    editor_scroll(ctx);

    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6);  // Hide cursor
    ab_append(&ab, "\x1b[H", 3);     // Cursor to top-left

    editor_draw_rows(ctx, &ab);
    editor_draw_status_bar(ctx, &ab);
    editor_draw_message_bar(ctx, &ab);

    // Position cursor
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (ctx->cy - ctx->rowoff) + 1,
             (ctx->cx - ctx->coloff) + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[?25h", 6);  // Show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}
```

**Change pattern:** Add `ctx` parameter, pass to called functions, `E.` → `ctx->`

#### Example 3: Row Operations

**Before:**
```c
void editor_insert_row(int at, char *s, size_t len) {
    if (at > E.numrows) return;

    if ((size_t)E.numrows >= SIZE_MAX / sizeof(t_erow)) {
        editor_set_status_message("Too many rows");
        return;
    }

    E.row = realloc(E.row, sizeof(t_erow) * (E.numrows + 1));
    if (!E.row) {
        perror("realloc");
        exit(1);
    }

    memmove(&E.row[at+1], &E.row[at], sizeof(t_erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    editor_update_row(&E.row[at]);

    E.numrows++;
    E.dirty++;
}
```

**After:**
```c
void editor_insert_row(editor_ctx_t *ctx, int at, char *s, size_t len) {
    if (at > ctx->numrows) return;

    if ((size_t)ctx->numrows >= SIZE_MAX / sizeof(t_erow)) {
        editor_set_status_message(ctx, "Too many rows");
        return;
    }

    ctx->row = realloc(ctx->row, sizeof(t_erow) * (ctx->numrows + 1));
    if (!ctx->row) {
        perror("realloc");
        exit(1);
    }

    memmove(&ctx->row[at+1], &ctx->row[at], sizeof(t_erow) * (ctx->numrows - at));

    ctx->row[at].size = len;
    ctx->row[at].chars = malloc(len + 1);
    memcpy(ctx->row[at].chars, s, len);
    ctx->row[at].chars[len] = '\0';

    ctx->row[at].rsize = 0;
    ctx->row[at].render = NULL;
    ctx->row[at].hl = NULL;
    editor_update_row(ctx, &ctx->row[at]);

    ctx->numrows++;
    ctx->dirty++;
}
```

---

## Benefits After Migration

### 1. Split Windows Become Possible

```c
typedef struct window {
    editor_ctx_t *ctx;    // Shared buffer context
    int viewport_row;     // Window position on screen
    int viewport_col;
    int viewport_height;
    int viewport_width;
} window_t;

window_t windows[4];

// Split horizontally
void split_horizontal(void) {
    windows[1].ctx = windows[0].ctx;  // Share buffer
    windows[1].viewport_height = E.screenrows / 2;
    // Independent viewports, same content!
}

// Each window has independent cursor, scroll position
editor_refresh_screen(windows[0].ctx);  // Render top pane
editor_refresh_screen(windows[1].ctx);  // Render bottom pane
```

### 2. Multiple Buffers (Tabs) Become Clean

```c
editor_ctx_t tabs[10];
int current_tab = 0;

void switch_to_tab(int idx) {
    current_tab = idx;  // Just change pointer!
}

// No state swapping needed - each tab IS a complete context
editor_process_keypress(&tabs[current_tab], key);
```

### 3. Better Testability

```c
// Unit tests can create isolated contexts
void test_insert_char(void) {
    editor_ctx_t ctx = {0};
    editor_init(&ctx);
    editor_insert_row(&ctx, 0, "hello", 5);
    editor_insert_char(&ctx, 'x');
    assert(strcmp(ctx.row[0].chars, "xhello") == 0);
    editor_free(&ctx);
}
```

### 4. Multiple Editor Instances

```c
// Could even have multiple independent editors
editor_ctx_t editor1, editor2;
editor_init(&editor1);
editor_init(&editor2);
editor_open(&editor1, "file1.c");
editor_open(&editor2, "file2.c");
```

---

## Migration Strategy

### Phase 1: Create Context Structure (1-2 hours) [x] COMPLETED

**Status:** Complete (2025-10-11)

**Steps:**
1. [x] Created `editor_ctx_t` in `src/loki_internal.h` (lines 161-184)
2. [x] Copied all fields from `struct loki_editor_instance`
3. [x] Kept global `E` for gradual migration
4. [x] Added initialization functions and declarations

**Implementation:**
```c
// src/loki_internal.h:161-184
typedef struct editor_ctx {
    int cx, cy;
    int rowoff, coloff;
    int screenrows, screencols;
    int screenrows_total;
    int numrows;
    int rawmode;
    t_erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct t_editor_syntax *syntax;
    lua_State *L;
    t_lua_repl repl;
    EditorMode mode;
    int word_wrap;
    int sel_active;
    int sel_start_x, sel_start_y;
    int sel_end_x, sel_end_y;
    t_hlcolor colors[9];
} editor_ctx_t;

// src/loki_internal.h:207-211 (function declarations)
void editor_ctx_init(editor_ctx_t *ctx);
void editor_ctx_from_global(editor_ctx_t *ctx);
void editor_ctx_to_global(const editor_ctx_t *ctx);
void editor_ctx_free(editor_ctx_t *ctx);

// src/loki_core.c:94-202 (implementations)
// All four functions implemented with full field copying
```

**What was implemented:**
- `editor_ctx_init()`: Initialize empty context with defaults
- `editor_ctx_from_global()`: Copy state from global E to context
- `editor_ctx_to_global()`: Copy state from context back to global E
- `editor_ctx_free()`: Free all allocated memory in context

**Verification:**
- [x] Compiles cleanly with `make`
- [x] No new warnings or errors
- [x] All fields properly copied in both directions

### Phase 2: Update loki_core.c (~1000 sites, 4-6 hours)

**Strategy:** Update module-by-module, bottom-up (leaf functions first)

**Step-by-step:**

1. **Identify leaf functions** (don't call other editor functions):
   - `is_separator()`
   - `editor_syntax_to_color()`
   - `ab_append()`

2. **Update row operations** (next layer up):
   ```c
   // Before
   void editor_update_row(t_erow *row);
   void editor_insert_row(int at, char *s, size_t len);
   void editor_del_row(int at);

   // After (add ctx parameter)
   void editor_update_row(editor_ctx_t *ctx, t_erow *row);
   void editor_insert_row(editor_ctx_t *ctx, int at, char *s, size_t len);
   void editor_del_row(editor_ctx_t *ctx, int at);
   ```

3. **Update editing operations**:
   ```c
   // Before
   void editor_insert_char(int c);
   void editor_insert_newline(void);
   void editor_del_char(void);

   // After
   void editor_insert_char(editor_ctx_t *ctx, int c);
   void editor_insert_newline(editor_ctx_t *ctx);
   void editor_del_char(editor_ctx_t *ctx);
   ```

4. **Update cursor/viewport operations**:
   ```c
   // Before
   void editor_move_cursor(int key);
   void editor_scroll(void);

   // After
   void editor_move_cursor(editor_ctx_t *ctx, int key);
   void editor_scroll(editor_ctx_t *ctx);
   ```

5. **Update rendering**:
   ```c
   // Before
   void editor_draw_rows(struct abuf *ab);
   void editor_draw_status_bar(struct abuf *ab);
   void editor_refresh_screen(void);

   // After
   void editor_draw_rows(editor_ctx_t *ctx, struct abuf *ab);
   void editor_draw_status_bar(editor_ctx_t *ctx, struct abuf *ab);
   void editor_refresh_screen(editor_ctx_t *ctx);
   ```

6. **Update modal editing**:
   ```c
   // Before
   static void process_normal_mode(int fd, int c);
   static void process_insert_mode(int fd, int c);
   static void process_visual_mode(int fd, int c);
   void editor_process_keypress(int fd);

   // After
   static void process_normal_mode(editor_ctx_t *ctx, int fd, int c);
   static void process_insert_mode(editor_ctx_t *ctx, int fd, int c);
   static void process_visual_mode(editor_ctx_t *ctx, int fd, int c);
   void editor_process_keypress(editor_ctx_t *ctx, int fd);
   ```

**Automated assistance:**
```bash
# Generate list of functions to update
grep -n "^void editor_\|^static void editor_\|^int editor_\|^static int editor_" src/loki_core.c > /tmp/functions_to_update.txt

# Count E. references
grep -o "E\." src/loki_core.c | wc -l
# (Expect ~1000-1500 in loki_core.c)
```

### Phase 3: Update loki_lua.c (~300 sites, 2-3 hours)

**Lua API functions need special handling:**

**Option A: Keep global E for Lua API** (easier, acceptable for now)
```c
// Lua API continues using global E
static int lua_loki_get_lines(lua_State *L) {
    lua_pushinteger(L, E.numrows);  // Still uses global
    return 1;
}
```

**Option B: Store context in Lua registry** (cleaner, more work)
```c
// Store context pointer in Lua registry
static int lua_loki_get_lines(lua_State *L) {
    lua_pushlightuserdata(L, (void *)&editor_ctx_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    editor_ctx_t *ctx = lua_touserdata(L, -1);
    lua_pop(L, 1);

    lua_pushinteger(L, ctx->numrows);
    return 1;
}

// Set context when initializing Lua
void loki_lua_bootstrap(editor_ctx_t *ctx, const char *config_path) {
    lua_pushlightuserdata(E.L, (void *)&editor_ctx_key);
    lua_pushlightuserdata(E.L, ctx);
    lua_settable(E.L, LUA_REGISTRYINDEX);
    // ...
}
```

**Recommendation:** Use Option A initially, refactor to Option B later if needed.

### Phase 4: Update loki_editor.c (~100 sites, 1-2 hours)

**Main entry point:**

**Before:**
```c
int loki_editor_main(int argc, char **argv) {
    if (argc >= 2) {
        editor_open(argv[1]);
    }

    editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while(1) {
        editor_refresh_screen();
        editor_process_keypress(STDIN_FILENO);
    }

    return 0;
}
```

**After:**
```c
int loki_editor_main(int argc, char **argv) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    if (argc >= 2) {
        editor_open(&ctx, argv[1]);
    }

    editor_set_status_message(&ctx, "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while(1) {
        editor_refresh_screen(&ctx);
        editor_process_keypress(&ctx, STDIN_FILENO);
    }

    editor_ctx_free(&ctx);
    return 0;
}
```

### Phase 5: Remove Global E (1 hour)

**Once all functions updated:**

```c
// src/loki_internal.h

/* DELETE THIS: */
// struct editorConfig E;

/* Context is now always passed explicitly */
```

**Verify:**
```bash
# Should return nothing
grep "^struct editorConfig E;" src/loki_internal.h
```

### Phase 6: Add Cleanup Function (1 hour)

```c
// src/loki_core.c

void editor_ctx_init(editor_ctx_t *ctx) {
    memset(ctx, 0, sizeof(editor_ctx_t));
    ctx->mode = MODE_NORMAL;
    ctx->cx = 0;
    ctx->cy = 0;
    ctx->rowoff = 0;
    ctx->coloff = 0;
    ctx->numrows = 0;
    ctx->row = NULL;
    ctx->dirty = 0;
    ctx->filename = NULL;
    ctx->statusmsg[0] = '\0';
    ctx->statusmsg_time = 0;
    ctx->syntax = NULL;
    // ... initialize all fields
}

void editor_ctx_free(editor_ctx_t *ctx) {
    // Free all dynamically allocated memory
    for (int i = 0; i < ctx->numrows; i++) {
        free(ctx->row[i].chars);
        free(ctx->row[i].render);
        free(ctx->row[i].hl);
    }
    free(ctx->row);
    free(ctx->filename);

    // Free clipboard
    for (int i = 0; i < ctx->clipboard_len; i++) {
        free(ctx->clipboard[i].chars);
        free(ctx->clipboard[i].render);
        free(ctx->clipboard[i].hl);
    }
    free(ctx->clipboard);

    // Note: Don't free ctx->L (Lua state) - it's shared
}
```

---

## Detailed File-by-File Analysis

### src/loki_core.c

**Functions to update:** ~80 functions
**E. references:** ~1000-1500 sites

**High-impact functions (update first):**
1. `editor_move_cursor()` - 20 E. references
2. `editor_refresh_screen()` - 30 E. references
3. `editor_insert_char()` - 15 E. references
4. `editor_del_char()` - 10 E. references
5. `editor_insert_row()` - 15 E. references
6. `editor_del_row()` - 10 E. references
7. `editor_scroll()` - 10 E. references
8. `editor_draw_rows()` - 30 E. references
9. `process_normal_mode()` - 40 E. references
10. `process_insert_mode()` - 20 E. references

**Estimated changes:**
- Add `editor_ctx_t *ctx` parameter: 80 functions
- Replace `E.field` with `ctx->field`: ~1200 sites
- Pass `ctx` to function calls: ~400 sites
- **Total changes:** ~1700 edits

### src/loki_lua.c

**Functions to update:** ~30 functions
**E. references:** ~200-300 sites

**Key functions:**
1. `lua_loki_*()` API functions - 15 functions, can defer to Phase 3B
2. `lua_repl_*()` functions - 8 functions, need context
3. `loki_lua_bootstrap()` - needs context parameter
4. `loki_poll_async_http()` - needs context parameter

**Estimated changes:**
- Add `editor_ctx_t *ctx` parameter: ~15 functions (excluding Lua API)
- Replace `E.field` with `ctx->field`: ~250 sites
- Pass `ctx` to function calls: ~50 sites
- **Total changes:** ~320 edits

### src/loki_editor.c

**Functions to update:** ~10 functions
**E. references:** ~80-100 sites

**Key functions:**
1. `loki_editor_main()` - main entry point
2. `run_ai_command()` - AI command handler
3. `start_async_http_request()` - HTTP request initiation
4. `editor_update_repl_layout()` - REPL layout management
5. `editor_cleanup_resources()` - cleanup

**Estimated changes:**
- Add `editor_ctx_t *ctx` parameter: 10 functions
- Replace `E.field` with `ctx->field`: ~90 sites
- Create and initialize context in main: ~10 lines
- **Total changes:** ~110 edits

---

## Testing Strategy

### Unit Tests (Create New)

```c
// tests/test_context.c

void test_context_init(void) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    assert(ctx.cx == 0);
    assert(ctx.cy == 0);
    assert(ctx.numrows == 0);
    assert(ctx.row == NULL);
    assert(ctx.mode == MODE_NORMAL);

    printf("[x] Context initialization\n");
}

void test_multiple_contexts(void) {
    editor_ctx_t ctx1, ctx2;
    editor_ctx_init(&ctx1);
    editor_ctx_init(&ctx2);

    editor_insert_row(&ctx1, 0, "Context 1", 9);
    editor_insert_row(&ctx2, 0, "Context 2", 9);

    assert(ctx1.numrows == 1);
    assert(ctx2.numrows == 1);
    assert(strcmp(ctx1.row[0].chars, "Context 1") == 0);
    assert(strcmp(ctx2.row[0].chars, "Context 2") == 0);

    editor_ctx_free(&ctx1);
    editor_ctx_free(&ctx2);

    printf("[x] Multiple independent contexts\n");
}

void test_context_isolation(void) {
    editor_ctx_t ctx1, ctx2;
    editor_ctx_init(&ctx1);
    editor_ctx_init(&ctx2);

    // Modify ctx1
    ctx1.cx = 10;
    ctx1.cy = 5;
    ctx1.dirty = 1;

    // ctx2 should be unaffected
    assert(ctx2.cx == 0);
    assert(ctx2.cy == 0);
    assert(ctx2.dirty == 0);

    editor_ctx_free(&ctx1);
    editor_ctx_free(&ctx2);

    printf("[x] Context isolation\n");
}
```

### Integration Tests (Manual)

**Test 1: Basic editing still works**
```bash
./build/loki-editor test.txt
# Type some text
# Save with Ctrl-S
# Verify file saved correctly
```

**Test 2: Multiple files (groundwork for tabs)**
```c
// Temporarily modify main() to test multiple contexts
int loki_editor_main(int argc, char **argv) {
    editor_ctx_t ctx1, ctx2;
    editor_ctx_init(&ctx1);
    editor_ctx_init(&ctx2);

    editor_open(&ctx1, "file1.txt");
    editor_open(&ctx2, "file2.txt");

    // Verify both loaded
    printf("ctx1: %d lines from %s\n", ctx1.numrows, ctx1.filename);
    printf("ctx2: %d lines from %s\n", ctx2.numrows, ctx2.filename);

    editor_ctx_free(&ctx1);
    editor_ctx_free(&ctx2);
    return 0;
}
```

### Regression Tests

**Run existing tests:**
```bash
make test
./build/loki-editor /tmp/test_file.txt
# Verify:
# - Modal editing works (h/j/k/l, i/a/o, v)
# - REPL works (Ctrl-L)
# - Search works (Ctrl-F)
# - Save/quit work (Ctrl-S, Ctrl-Q)
```

---

## Risk Mitigation

### High-Risk Areas

1. **Global state hidden in static variables**
   - Search for: `static` variables in functions
   - Example: `static int last_match = -1;` in `editor_find()`
   - **Mitigation:** Move static state into context structure

2. **Lua state sharing**
   - `lua_State *L` needs special handling (could be shared across contexts)
   - **Mitigation:** Keep `L` as shared global initially, refactor later

3. **Terminal state (tcsetattr, window size)**
   - Currently in `editorConfig` but really global to terminal
   - **Mitigation:** Extract terminal state into separate global `terminal_state`

4. **Signal handlers (SIGWINCH)**
   - Currently updates global `E.screenrows`/`E.screencols`
   - **Mitigation:** Keep terminal dimensions global, copy to contexts as needed

### Rollback Plan

**Checkpoint after each phase:**
```bash
# Before starting phase
git checkout -b context-passing-phase1
git commit -m "Checkpoint before Phase 1"

# If phase fails
git checkout master
git branch -D context-passing-phase1
```

**Feature flag approach:**
```c
// Temporarily support both paths
#define USE_CONTEXT_PASSING 0

#if USE_CONTEXT_PASSING
    editor_ctx_t ctx;
    editor_refresh_screen(&ctx);
#else
    editor_refresh_screen();  // Old global path
#endif
```

---

## Effort Breakdown

| Phase | Component | Functions | E. Sites | Estimated Time |
|-------|-----------|-----------|----------|----------------|
| 1 | Create context structure | 2 new | 0 | 1-2 hours |
| 2 | loki_core.c | ~80 | ~1200 | 4-6 hours |
| 3 | loki_lua.c | ~15 | ~250 | 2-3 hours |
| 4 | loki_editor.c | ~10 | ~90 | 1-2 hours |
| 5 | Remove global E | 0 | 1 | 1 hour |
| 6 | Add cleanup | 2 new | 0 | 1 hour |
| 7 | Testing & fixes | - | - | 3-4 hours |
| **Total** | **~107 functions** | **~1540 sites** | **15-20 hours** |

**Calendar time:** 2-3 days (with breaks, debugging, testing)

---

## Alternative: Phased Coexistence

**If full migration is too risky, use hybrid approach:**

### Keep Global E, Add Context Layer

```c
// Global E still exists
struct editorConfig E;

// But new functions use context
typedef struct editor_ctx {
    struct editorConfig *global;  // Points to E
    // Override fields for viewports
    int viewport_cx, viewport_cy;
    int viewport_rowoff, viewport_coloff;
    int viewport_screenrows, viewport_screencols;
} editor_ctx_t;

// Access macro
#define CTX_CX(ctx) ((ctx)->viewport_cx != -1 ? (ctx)->viewport_cx : (ctx)->global->cx)

// Gradually migrate functions
void editor_refresh_screen_v2(editor_ctx_t *ctx);  // New version
```

**Benefits:**
- Less risky (can migrate incrementally)
- Old code still works
- New features use context

**Drawbacks:**
- More complex (two systems)
- Technical debt
- Still need full migration eventually

---

## Next Steps After Migration

### 1. Implement Tab Support

```c
// src/loki_tabs.c (new file)

#define MAX_TABS 10

typedef struct {
    editor_ctx_t tabs[MAX_TABS];
    int num_tabs;
    int current_tab;
} tab_manager_t;

void tab_manager_init(tab_manager_t *tm);
int tab_manager_new(tab_manager_t *tm);
void tab_manager_close(tab_manager_t *tm, int idx);
void tab_manager_switch(tab_manager_t *tm, int idx);
editor_ctx_t* tab_manager_current(tab_manager_t *tm);
```

### 2. Implement Split Windows

```c
// src/loki_windows.c (new file)

typedef struct {
    editor_ctx_t *ctx;  // Buffer context (may be shared)
    int screen_row, screen_col;  // Position on terminal
    int screen_height, screen_width;  // Size of window
    int is_active;  // Currently focused?
} window_t;

typedef struct {
    window_t windows[4];
    int num_windows;
    int active_window;
} window_manager_t;

void window_manager_init(window_manager_t *wm);
int window_manager_split_horizontal(window_manager_t *wm);
int window_manager_split_vertical(window_manager_t *wm);
void window_manager_close(window_manager_t *wm, int idx);
void window_manager_focus_next(window_manager_t *wm);
void window_manager_render_all(window_manager_t *wm);
```

---

## Conclusion

**Is it worth it?**

**YES, if you want:**
- Split windows (impossible without this)
- Clean tab implementation (very awkward without this)
- Better testability (multiple contexts in tests)
- Future-proof architecture

**NO, if you want:**
- Minimal single-buffer editor (current architecture is fine)
- Quick feature additions without large refactor
- Simplicity over extensibility

**Recommendation:**

Given that you've just completed a major refactoring to achieve modularity, **wait 1-2 months** before doing this migration. Let the current architecture stabilize, implement the 2 remaining feature TODOs (visual delete, command mode), and see if users actually request tabs/splits.

If tabs/splits become a real requirement, this document provides the roadmap to implement them.

**Estimated ROI:**
- **Cost:** 2-3 days of focused work
- **Benefit:** Unlocks entire category of features (tabs, splits, multi-view)
- **Risk:** Medium (touches core, but mostly mechanical changes)
- **Timeline:** Best done after 1-2 months of stability

---

## Implementation History

### 2025-10-11: Phase 1 Completed
- Created `editor_ctx_t` structure in `src/loki_internal.h`
- Implemented four context management functions in `src/loki_core.c`:
  - `editor_ctx_init()` - Initialize context with defaults
  - `editor_ctx_from_global()` - Copy from global E to context
  - `editor_ctx_to_global()` - Copy from context to global E
  - `editor_ctx_free()` - Free context resources
- Updated documentation to reflect actual structure names
- Verified compilation with no errors

**Next Steps:** Phase 2-6 remain to be implemented when needed

---

**Document Status:** Phase 1 complete; infrastructure ready for migration
**Last Updated:** 2025-10-11
