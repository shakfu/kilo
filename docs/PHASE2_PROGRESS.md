# Phase 2 Migration Progress

**Status:** PARTIAL - Core functions updated, modal/UI layers need updates
**Date:** 2025-10-11
**Estimated Remaining:** 2-3 hours

## Completed ✅

### Type System (100%)
- Made `loki_editor_instance` a typedef of `editor_ctx_t`
- Changed global `E` to type `editor_ctx_t`
- Updated public API header to use `editor_ctx_t*` parameters

### Core Functions Updated (100%)
1. **Context management** (src/loki_core.c:94-202)
   - `editor_ctx_init()`, `editor_ctx_from_global()`, `editor_ctx_to_global()`, `editor_ctx_free()`

2. **Leaf functions** (src/loki_core.c)
   - `editor_format_color()` - Line 1271
   - `editor_update_syntax()` - Line 862
   - `editor_update_row()` - Line 1375

3. **Row operations** (src/loki_core.c)
   - `editor_insert_row()` - Line 1415
   - `editor_del_row()` - Line 1459
   - `editor_rows_to_string()` - Line 1475
   - `editor_row_insert_char()` - Line 1500
   - `editor_row_append_string()` - Line 1534
   - `editor_row_del_char()` - Line 1549

4. **Editing operations** (src/loki_core.c)
   - `editor_insert_char()` - Line 1559
   - `editor_insert_newline()` - Line 1581
   - `editor_del_char()` - Line 1617

5. **File I/O** (src/loki_core.c)
   - `editor_open()` - Line 1653
   - `editor_save()` - Line 1702

### Header Updates (100%)
- `src/loki_internal.h` - Updated declarations
- `include/loki/core.h` - Updated public API

## In Progress ⏳

### Modal Editing Functions (src/loki_core.c)
**Status:** Forward declarations updated, implementations need ctx parameter added and call sites fixed

Functions that need updating:
- `process_normal_mode()` - Line 2277 (17 E. references)
- `process_insert_mode()` - Line 2363 (17 E. references)
- `process_visual_mode()` - Line 2450 (14 E. references)

Current errors (8 total):
```
/Users/sa/projects/loki/src/loki_core.c:2306:35: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2312:35: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2329:29: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2333:34: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2374:35: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2380:29: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2391:34: error: too few arguments to function call
/Users/sa/projects/loki/src/loki_core.c:2444:33: error: too few arguments to function call
```

## Not Started ❌

### Remaining loki_core.c Functions
These functions still use global E and need ctx parameter:

1. **Cursor/Viewport** (~80 E. references total)
   - `editor_move_cursor()` - Line 2103 (34 E. references)
   - `editor_find()` - Line 2177 (19 E. references)
   - `move_to_next_empty_line()` - Line 2032 (13 E. references)
   - `move_to_prev_empty_line()` - Line 2067 (10 E. references)
   - `is_empty_line()` - Line 2020 (2 E. references)

2. **Rendering** (37 E. references)
   - `editor_refresh_screen()` - Line 1759 (37 E. references)
   - Calls `editor_format_color()` - already updated ✅

3. **Utility Functions** (~30 E. references total)
   - `init_editor()` - Line 2561 (14 E. references)
   - `init_default_colors()` - Line 1956 (9 E. references)
   - `update_window_size()` - Line 1978 (3 E. references)
   - `handle_windows_resize()` - Line 2002 (2 E. references)
   - `editor_select_syntax_highlight()` - Line 1280 (2 E. references)

4. **Selection Functions** (~15 E. references total)
   - `is_selected()` - Line 207 (5 E. references)
   - `copy_selection_to_clipboard()` - Line 272 (10 E. references)

5. **Main Entry Point**
   - `editor_process_keypress()` - Line 2518 (3 E. references)

### Other Files Need Updates

1. **src/loki_editor.c** - All call sites need to pass `&E`
   - Calls to `editor_insert_char(&E, c)`
   - Calls to `editor_del_char(&E)`
   - Calls to `editor_insert_newline(&E)`
   - Calls to `editor_open(&E, filename)`
   - Calls to `editor_save(&E)`
   - Calls to `editor_refresh_screen()` (when updated)
   - Calls to `editor_process_keypress()` (when updated)

2. **src/loki_lua.c** - Lua API bindings need context
   - `lua_loki_insert_text()` - calls `editor_insert_char()`
   - Other Lua functions that interact with editor

3. **Lua-related functions in loki_core.c** (optional - can keep using E)
   - `lua_apply_span_table()` - Line 714 (29 E. references)
   - `lua_apply_highlight_row()` - Line 790 (30 E. references)

## Quick Fix Script for Remaining Work

```bash
# Update process_*_mode function signatures (lines 2277, 2363, 2450)
# Add editor_ctx_t *ctx as first parameter

# Update all call sites in these functions:
# - editor_insert_char(...) → editor_insert_char(ctx, ...)
# - editor_del_char(...) → editor_del_char(ctx, ...)
# - editor_insert_newline(...) → editor_insert_newline(ctx, ...)

# Then update editor_process_keypress to:
# 1. Take editor_ctx_t *ctx parameter
# 2. Pass ctx to process_*_mode functions
```

## Estimated Remaining Effort

- **Modal editing fix:** 30 minutes (8 errors, straightforward)
- **Cursor/viewport functions:** 1 hour (~80 E. references)
- **Rendering functions:** 30 minutes (1 big function)
- **Utility functions:** 30 minutes (straightforward)
- **loki_editor.c call sites:** 30-45 minutes
- **loki_lua.c updates:** 30 minutes
- **Testing and fixes:** 30-45 minutes

**Total:** 2-3 hours

## Current E. Reference Count

- **Before Phase 2:** 421 E. references
- **After current work:** ~200 E. references remaining
- **Reduction:** ~53% complete

## Next Steps

1. Fix the 8 compilation errors in process_*_mode functions
2. Update editor_process_keypress and its callers
3. Update remaining loki_core.c functions
4. Fix loki_editor.c and loki_lua.c call sites
5. Test compilation and run test suite
6. Update docs/remove_global.md with Phase 2 completion

## Notes

- All updated functions follow pattern: add `editor_ctx_t *ctx` as first parameter
- Replace `E.field` with `ctx->field`
- Pass `ctx` or `&E` to called functions
- Public API breaking change is intentional (enables future features)
