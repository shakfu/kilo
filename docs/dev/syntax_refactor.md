# Syntax Highlighting Module Extraction

## Analysis: Moving Syntax Highlighting from Core to Separate Module

**Date:** 2025-10-17
**Status:** Proposed
**Priority:** Medium-High
**Estimated Effort:** 2-3 hours

---

## Executive Summary

The syntax highlighting code currently in `loki_core.c` (~235 lines) can be cleanly extracted into a separate module `loki_syntax.c`. This would reduce the core from ~1,152 lines to ~917 lines, bringing it well below the 1,000-line milestone and aligning with the project's modular architecture principles.

**Feasibility:** ✅ **HIGHLY FEASIBLE**
**Risk:** Low (straightforward refactoring, no circular dependencies)
**Reward:** High (20% core reduction, improved separation of concerns)

---

## Current State

### Code Distribution

The syntax highlighting code is currently distributed across three files:

#### 1. `loki_core.c` (lines 173-410, ~237 lines)

| Function | Lines | Purpose |
|----------|-------|---------|
| `is_separator()` | 175-177 | Character separator detection |
| `editor_row_has_open_comment()` | 182-187 | Multi-line comment state tracking |
| `hl_name_to_code()` | 193-205 | String to HL_* constant mapping |
| `editor_update_syntax()` | 209-361 | Main syntax highlighting logic |
| `editor_format_color()` | 366-371 | RGB color escape sequence formatting |
| `editor_select_syntax_highlight()` | 374-409 | File extension to syntax mapping |

#### 2. `loki_languages.c`

- `editor_update_syntax_markdown()` - Markdown-specific highlighter (called from core)
- Language database (HLDB array)
- Dynamic language registration system

#### 3. `loki_internal.h`

- HL_* constants (`HL_NORMAL`, `HL_COMMENT`, `HL_KEYWORD1`, etc.)
- `t_editor_syntax` structure
- `t_hlcolor` structure (RGB color definition)

---

## Dependency Analysis

### Functions Called BY Syntax Highlighting

**Standard library only:**

- Memory: `realloc()`, `memset()`, `memcmp()`
- Strings: `strcmp()`, `strcasecmp()`, `strstr()`, `strlen()`
- Character classification: `isspace()`, `isdigit()`, `isprint()`
- Formatting: `snprintf()`

**No core editor dependencies** - syntax highlighting is self-contained.

### Functions that CALL Syntax Highlighting

1. **`editor_update_row()`** (loki_core.c:449)
   - Calls `editor_update_syntax()` after rendering a row

2. **`editor_refresh_screen()`** (loki_core.c:878)
   - Calls `editor_format_color()` to render colored text

3. **`editor_open()`** (via `editor_select_syntax_highlight()`)
   - Sets syntax based on file extension

4. **Lua API** (loki_editor.c:716, 725)
   - Calls `hl_name_to_code()` for custom highlighting

### Data Dependencies

- `ctx->syntax` - Pointer to current `t_editor_syntax` rules
- `ctx->colors[]` - Array of RGB colors for each HL_* type
- `ctx->row[].hl` - Per-character highlight type array
- `ctx->row[].render` - Rendered text (with tabs expanded)
- `ctx->row[].rsize` - Rendered text size
- Global HLDB array and dynamic language registry

**All accessed through `editor_ctx_t*` - clean interface.**

---

## Proposed Module Structure

### `loki_syntax.h` (Public API)

```c
#ifndef LOKI_SYNTAX_H
#define LOKI_SYNTAX_H

#include "loki_internal.h"

/* Syntax highlighting functions */
void syntax_update_row(editor_ctx_t *ctx, t_erow *row);
int syntax_format_color(editor_ctx_t *ctx, int hl, char *buf, size_t bufsize);
void syntax_select_for_filename(editor_ctx_t *ctx, char *filename);
int syntax_name_to_code(const char *name);
int syntax_is_separator(int c, char *separators);
int syntax_row_has_open_comment(t_erow *row);

/* Color initialization */
void syntax_init_default_colors(editor_ctx_t *ctx);

#endif /* LOKI_SYNTAX_H */
```

### `loki_syntax.c` (Implementation)

```c
#include "loki_syntax.h"
#include "loki_languages.h"
#include <string.h>
#include <ctype.h>

/* Move all 6 functions from loki_core.c (lines 175-409) */
/* Rename with syntax_* prefix */
/* Keep implementation identical, just change function names */
```

### Size Impact

**Before:**

- `loki_core.c`: 1,152 lines

**After:**

- `loki_core.c`: ~917 lines (20% reduction)
- `loki_syntax.c`: ~250 lines (new module)

**Net effect:** Core shrinks below 1,000 lines 🎉

---

## Benefits

### 1. Architectural Alignment ✅

Syntax highlighting is clearly a **feature**, not core infrastructure. Extracting it aligns with the "minimal core principle."

### 2. Core Size Reduction ✅

Removes ~235 lines from core, bringing it to ~917 lines (below the 1,000-line psychological milestone).

### 3. Separation of Concerns ✅

`loki_core.c` focuses purely on:

- Row management (insert, delete, update)
- File I/O
- Context management
- Integration glue

Syntax becomes an optional layer on top.

### 4. Modularity ✅

New module can be:

- Compiled out for minimal builds
- Replaced with alternative highlighters (tree-sitter, LSP semantic tokens)
- Extended without touching core

### 5. Testability ✅

Easier to unit test syntax highlighting in isolation:

- Mock row structures
- Test all language rules
- Verify color output
- No need for full editor context

### 6. Future Extensibility ✅

Clear place for future enhancements:

- Tree-sitter integration
- LSP semantic tokens
- Custom user-defined syntax
- Syntax plugins

---

## Migration Path

### Step 1: Create Module Files

**Create `src/loki_syntax.h`:**

- Public API header
- Function declarations with `syntax_*` prefix
- Include guards

**Create `src/loki_syntax.c`:**

- Copy functions from `loki_core.c` lines 175-409
- Rename functions:
  - `is_separator()` → `syntax_is_separator()`
  - `editor_row_has_open_comment()` → `syntax_row_has_open_comment()`
  - `hl_name_to_code()` → `syntax_name_to_code()`
  - `editor_update_syntax()` → `syntax_update_row()`
  - `editor_format_color()` → `syntax_format_color()`
  - `editor_select_syntax_highlight()` → `syntax_select_for_filename()`
- Add includes: `<string.h>`, `<ctype.h>`, `loki_languages.h`

**Move `init_default_colors()`:**

- From `loki_core.c` (line 1008) to `loki_syntax.c`
- Rename to `syntax_init_default_colors()`

### Step 2: Update `loki_core.c`

**Remove syntax functions:**

- Delete lines 175-409 (syntax functions)
- Delete line 1008-1027 (`init_default_colors()`)

**Add include:**

```c
#include "loki_syntax.h"
```

**Update call sites:**

- Line 449: `editor_update_syntax(ctx, row)` → `syntax_update_row(ctx, row)`
- Line 878: `editor_format_color(ctx, color, buf, sizeof(buf))` → `syntax_format_color(ctx, color, buf, sizeof(buf))`
- Line 1130: `init_default_colors(ctx)` → `syntax_init_default_colors(ctx)`
- Line 374-409: Remove `editor_select_syntax_highlight()`, use `syntax_select_for_filename()`

### Step 3: Update Other Files

**`loki_editor.c`:**

- Add `#include "loki_syntax.h"`
- Line 716, 725: `hl_name_to_code()` → `syntax_name_to_code()`

**`loki_languages.c`:**

- Update forward declaration comment for `editor_update_syntax_markdown()`
- Consider moving markdown highlighter to `loki_syntax.c` (optional)

**`loki_internal.h`:**

- Keep HL_* constants (shared between core and syntax)
- Add comment: "Syntax highlighting constants - used by loki_syntax.c"

### Step 4: Update Build System

**`CMakeLists.txt`:**

```cmake
# Add to loki_core library sources
set(LOKI_CORE_SOURCES
    src/loki_core.c
    src/loki_editor.c
    src/loki_terminal.c
    src/loki_syntax.c        # NEW
    src/loki_languages.c
    # ... rest
)
```

**`src/CMakeLists.txt`** (if separate):

```cmake
add_library(loki_syntax STATIC loki_syntax.c)
target_link_libraries(loki_core PRIVATE loki_syntax)
```

### Step 5: Testing

**Verification checklist:**

- [ ] All tests pass: `make test`
- [ ] No compiler warnings: `make` with `-Wall -Wextra -pedantic`
- [ ] Syntax highlighting works for all built-in languages (C, Python, Lua, Markdown)
- [ ] Dynamic language registration still works
- [ ] Color themes apply correctly
- [ ] Lua API `loki.set_color()` and `loki.set_theme()` work
- [ ] Search highlighting (HL_MATCH) works
- [ ] Multi-line comment spanning works correctly
- [ ] No performance regression (benchmark with 10K line file)

**Test cases:**

```bash
# Test basic syntax highlighting
./loki test.c
./loki test.py
./loki test.lua
./loki test.md

# Test dynamic language
# (ensure .loki/languages/javascript.lua loads)
./loki test.js

# Test color customization
./loki  # then Ctrl-L: theme.load("dracula")

# Performance test
time ./loki large_file.c
```

---

## Challenges & Solutions

### Challenge 1: Markdown Highlighter Location

**Problem:** `editor_update_syntax_markdown()` is defined in `loki_languages.c` but called from `editor_update_syntax()`.

**Options:**

1. **Move to `loki_syntax.c`** (recommended)
   - Clean: all syntax highlighting in one module
   - `loki_languages.c` becomes pure data (language definitions)

2. **Keep in `loki_languages.c`**
   - Requires forward declaration in `loki_syntax.h`
   - Slight coupling between modules

3. **Use callback pointer**
   - Add `void (*highlighter)(editor_ctx_t*, t_erow*)` to `t_editor_syntax`
   - Most flexible but adds complexity

**Recommendation:** Option 1 - move markdown highlighter to `loki_syntax.c`

### Challenge 2: HL_* Constants Location

**Problem:** HL_* constants are used by:

- Syntax module (for highlighting)
- Core (for color array sizing)
- Lua API (for `loki.set_color()`)

**Solution:** Keep in `loki_internal.h` as shared types

- These are fundamental type definitions
- Not implementation details of syntax module
- Similar to how `t_erow` is shared

### Challenge 3: Color Array in Context

**Problem:** `ctx->colors[]` is part of `editor_ctx_t` structure.

**Solution:** No change needed

- Syntax module accesses via `ctx` pointer (clean interface)
- Core owns the data, syntax module operates on it
- Standard pattern (like `ctx->row[]`)

---

## Timeline & Prioritization

### Estimated Effort: 2-3 hours

- **1 hour:** Extract functions, create module files
- **30 min:** Update call sites in `loki_core.c` and `loki_editor.c`
- **30 min:** Update build system (CMakeLists.txt)
- **1 hour:** Test all syntax highlighting functionality, verify no regressions

### Priority: Medium-High

**Do AFTER:**

- ✅ Auto-indent module (v0.5.0 - highest user value)
- ✅ Config system (v0.5.0 - ease of use)

**Do BEFORE:**

- Split windows (v0.6.0 - complex feature, want clean base)
- LSP integration (v0.7.0 - builds on syntax foundation)

**Why this ordering:**

1. User-facing features first (auto-indent, config)
2. Clean up architecture before adding complexity
3. Provides cleaner foundation for advanced features

### Ideal Timing

**Between v0.5.0 and v0.6.0:**

- After initial feature releases stabilize
- Before embarking on complex multi-window features
- Good "cleanup sprint" between feature development

---

## Success Criteria

### Code Quality ✅

- [ ] `loki_core.c` < 1,000 lines
- [ ] All tests pass (zero tolerance policy)
- [ ] No compiler warnings
- [ ] Valgrind clean (no memory leaks)

### Functionality ✅

- [ ] All built-in languages highlight correctly
- [ ] Dynamic language registration works
- [ ] Themes apply correctly
- [ ] Lua API functions work (`loki.set_color()`, `loki.set_theme()`)
- [ ] Search highlighting works

### Performance ✅

- [ ] No measurable performance regression
- [ ] Large file (10K+ lines) highlighting remains fast

### Architecture ✅

- [ ] Clean module boundaries (no circular dependencies)
- [ ] Clear API surface (well-defined public functions)
- [ ] Self-contained (minimal external dependencies)
- [ ] Documentation updated (CLAUDE.md)

---

## Alternative Approaches Considered

### 1. Leave in Core

**Pros:** No refactoring effort, works fine as-is
**Cons:** Core stays >1,000 lines, violates modular principles
**Verdict:** ❌ Not aligned with project goals

### 2. Move to `loki_languages.c`

**Pros:** Syntax and language definitions together
**Cons:** `loki_languages.c` becomes too large, mixes data and logic
**Verdict:** ❌ Worse separation of concerns

### 3. Create `loki_highlighting.c` (different name)

**Pros:** More specific name
**Cons:** Longer name, less consistent with `loki_syntax` types
**Verdict:** ⚠️ Possible, but `loki_syntax.c` is clearer

### 4. Extract Only to Header (inline functions)

**Pros:** No new .c file, minimal changes
**Cons:** Doesn't reduce core size, header bloat
**Verdict:** ❌ Doesn't achieve goals

---

## Post-Refactor Opportunities

Once syntax highlighting is in its own module, these become easier:

### 1. Alternative Highlighters

- Tree-sitter integration (AST-based highlighting)
- LSP semantic tokens (context-aware)
- User-defined syntax via Lua

### 2. Performance Optimizations

- Lazy highlighting (only visible rows)
- Cached regex compilation
- Incremental re-highlighting

### 3. Advanced Features

- Bracket matching visualization
- Semantic highlighting (function names, variables)
- Real-time linting integration

### 4. Plugin Architecture

- Custom syntax plugins
- Language-specific enhancements
- Theme marketplace

---

## References

- **ROADMAP.md** - Module architecture principles
- **CLAUDE.md** - Code review findings (syntax highlighting bounds checking)
- **loki_internal.h** - Current structure definitions
- **loki_languages.c** - Language database and markdown highlighter

---

## Conclusion

Extracting syntax highlighting from `loki_core.c` into `loki_syntax.c` is:

- ✅ **Feasible** - Clean separation, no circular dependencies
- ✅ **Valuable** - 20% core reduction, improved modularity
- ✅ **Low Risk** - Straightforward refactoring, well-tested code
- ✅ **Aligned** - Supports modular architecture goals
- ✅ **Timely** - Good cleanup task between feature releases

**Recommendation: Proceed with extraction during v0.5.x development cycle.**

This refactoring would bring the core below 1,000 lines for the first time, a significant architectural milestone that demonstrates the success of the modular approach.

---

**Next Steps:**

1. Schedule refactoring sprint (2-3 hour block)
2. Create feature branch: `refactor/extract-syntax-module`
3. Follow migration path (Steps 1-5)
4. Submit PR with test results
5. Update CLAUDE.md and ROADMAP.md with new module
