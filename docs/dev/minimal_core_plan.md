# Minimal Core Plan

**Goal**: Extract feature code from loki_core.c to achieve a truly minimal, focused core

**Date**: 2025-10-12
**Status**: Planning

---

## Current State

- **Total lines**: 1,993 (1,487 actual code per cloc)
- **Core has grown beyond minimal** - includes feature layers on top of essential functionality
- **Problem**: loki_core.c mixes essential editor infrastructure with optional features

---

## Section Analysis

### Current Distribution

| Section | Lines | Status | Notes |
|---------|-------|--------|-------|
| Context Management | 54 | [x] Keep | Essential state management |
| Terminal handling | 193 | [x] Keep | Raw mode, window size, key reading |
| Editor rows | 357 | [x] Keep | Buffer manipulation core |
| Terminal rendering | 286 | [x] Keep | Screen updates, VT100 sequences |
| Cursor movement | 173 | [!] Partial | Basic movement (keep), find (extract) |
| Syntax highlighting | 236 | [!] Partial | Core infrastructure (keep), dynamic lang (extract) |
| **Modal editing** | **543** | [X] Extract | Feature layer, not essential |
| **Selection & Clipboard** | **127** | [X] Extract | Feature code, OSC 52 utilities |
| **Dynamic lang registration** | **57** | [X] Extract | Belongs in loki_languages.c |
| **Search/Find** | **~100** | [X] Extract | Feature tool, not core |

### Core Essentials (Keep)

**~660-700 lines of essential functionality**:

1. Terminal I/O - raw mode, key reading, window size detection
2. Buffer management - rows, insert/delete, file I/O
3. Screen rendering - VT100 sequences, refresh, status bar
4. Basic cursor movement - arrow keys, home/end, page up/down
5. Syntax highlighting infrastructure - update_syntax, format_color, select_syntax

---

## Extraction Plan

### Phase 1: Dynamic Language Registration → `loki_languages.c`

**Lines to extract**: 57
**Difficulty**: Easy
**Impact**: Logical consistency

**Functions**:

- `add_dynamic_language()` (16 lines)
- `free_dynamic_language()` (27 lines)
- `cleanup_dynamic_languages()` (8 lines)
- `HLDB_dynamic` array and management

**Rationale**:

- Language infrastructure belongs with language definitions
- Core shouldn't manage language registry
- All language code in one module

**Files affected**:

- `src/loki_core.c` - Remove functions
- `src/loki_languages.c` - Add functions
- `src/loki_languages.h` - Add declarations
- `src/loki_internal.h` - Update declarations

---

### Phase 2: Selection & Clipboard → `loki_selection.c`

**Lines to extract**: 127
**Difficulty**: Medium
**Impact**: Clean separation

**Functions**:

- `is_selected(editor_ctx_t *ctx, int row, int col)` (35 lines)
- `base64_encode(const char *input, size_t len)` (29 lines)
- `copy_selection_to_clipboard(editor_ctx_t *ctx)` (60 lines)
- `base64_table[]` constant

**Rationale**:

- Visual selection is a modal editing feature
- OSC 52 clipboard is a utility, not core functionality
- Base64 encoding is generic utility code
- Core doesn't need selection awareness

**Files to create**:

- `src/loki_selection.c` - Selection and clipboard implementation
- `src/loki_selection.h` - Public API declarations

**Public API**:

```c
/* src/loki_selection.h */
int is_selected(editor_ctx_t *ctx, int row, int col);
void copy_selection_to_clipboard(editor_ctx_t *ctx);
```

**Benefits**:

- Core becomes selection-agnostic
- Easier to add other clipboard methods (X11, Wayland, pbcopy)
- Base64 utility reusable for other features

---

### Phase 3: Search → `loki_search.c`

**Lines to extract**: ~100
**Difficulty**: Medium
**Impact**: Feature isolation

**Functions**:

- `editor_find(editor_ctx_t *ctx, int fd)` (~100 lines)

**Rationale**:

- Search is a tool/feature, not fundamental to editing
- Core focuses on buffer manipulation, not searching
- Clear feature boundary enables enhancements

**Files to create**:

- `src/loki_search.c` - Search implementation
- `src/loki_search.h` - Public API

**Public API**:

```c
/* src/loki_search.h */
void editor_find(editor_ctx_t *ctx, int fd);
```

**Benefits**:

- Search can be enhanced independently (regex, case-insensitive, etc.)
- Core doesn't manage search state
- Could add incremental search, search/replace without touching core

---

### Phase 4: Modal Editing → `loki_modal.c`

**Lines to extract**: 543 (largest extraction)
**Difficulty**: Large
**Impact**: Maximum cleanup

**Functions**:

- `process_normal_mode(editor_ctx_t *ctx, int fd, int c)` (~86 lines)
- `process_insert_mode(editor_ctx_t *ctx, int fd, int c)` (~87 lines)
- `process_visual_mode(editor_ctx_t *ctx, int fd, int c)` (~68 lines)
- `is_empty_line(editor_ctx_t *ctx, int row)` (14 lines)
- `move_to_next_empty_line(editor_ctx_t *ctx)` (34 lines)
- `move_to_prev_empty_line(editor_ctx_t *ctx)` (34 lines)
- Modal mode dispatching logic from `editor_process_keypress()`

**Rationale**:

- Modal editing is a feature layer on top of core editing
- Not essential to buffer/terminal management
- Users could theoretically disable modal mode (insert-only)
- Core becomes mode-agnostic

**Files to create**:

- `src/loki_modal.c` - Modal editing implementation
- `src/loki_modal.h` - Public API

**Public API**:

```c
/* src/loki_modal.h */
void modal_process_keypress(editor_ctx_t *ctx, int fd, int c);
```

**Core Integration**:

```c
/* In editor_process_keypress - simplified */
void editor_process_keypress(editor_ctx_t *ctx, int fd) {
    int c = editor_read_key(fd);

    /* Delegate to modal module */
    modal_process_keypress(ctx, fd, c);
}
```

**Benefits**:

- Core is mode-agnostic - doesn't know about vim-like modes
- Modal editing is opt-in architecture
- Easier to add other editing modes (Emacs-style, etc.)
- Could support multiple modal schemes
- Paragraph motions ({, }) isolated from core cursor logic

---

## Result After All Extractions

| Metric | Before | After | Reduction |
|--------|--------|-------|-----------|
| **Total lines** | 1,993 | ~1,166 | 827 (41%) |
| **Code lines (cloc)** | 1,487 | ~660 | 827 (56%) |

### New Module Structure

```text
src/
├── loki_core.c          # ~660 lines - Essential editor core
├── loki_languages.c     # 474 lines - Language definitions + dynamic registry
├── loki_selection.c     # 127 lines - Selection & clipboard (NEW)
├── loki_search.c        # 100 lines - Search functionality (NEW)
├── loki_modal.c         # 543 lines - Modal editing (NEW)
├── loki_editor.c        # Main entry point
├── loki_lua.c           # Lua integration
└── main_*.c             # Entry points
```

---

## Core After Cleanup

**loki_core.c becomes truly minimal (~660 lines)**:

### Essential Functionality Only

1. **Context Management** (54 lines)
   - `editor_ctx_init()` - Initialize editor state
   - `editor_ctx_free()` - Cleanup resources

2. **Terminal I/O** (193 lines)
   - `enable_raw_mode()` / `disable_raw_mode()` - Terminal setup
   - `editor_read_key()` - Read keyboard input
   - `get_window_size()` / `get_cursor_position()` - Terminal queries
   - Window resize handling

3. **Buffer Management** (357 lines)
   - `editor_update_row()` - Update rendered row
   - `editor_insert_row()` / `editor_del_row()` - Row operations
   - `editor_row_insert_char()` / `editor_row_del_char()` - Character operations
   - `editor_insert_char()` / `editor_del_char()` - User-facing edits
   - `editor_insert_newline()` - Line breaks
   - `editor_open()` / `editor_save()` - File I/O
   - `editor_rows_to_string()` - Buffer serialization

4. **Screen Rendering** (286 lines)
   - `editor_refresh_screen()` - Main rendering loop
   - `ab_append()` / `ab_free()` - Append buffer for VT100 sequences
   - Status bar rendering
   - Scroll management

5. **Cursor Movement Basics** (~70 lines)
   - `editor_move_cursor()` - Arrow keys, home/end, page up/down
   - Viewport scrolling

6. **Syntax Highlighting Infrastructure** (~150 lines)
   - `editor_update_syntax()` - Apply syntax rules
   - `editor_format_color()` - RGB color codes
   - `editor_select_syntax_highlight()` - Choose language
   - `is_separator()` - Utility function
   - `editor_row_has_open_comment()` - Multi-line comment tracking

---

## Architecture Benefits

### 1. Modularity

- Features can be enabled/disabled independently
- Clear boundaries between core and features
- Optional features don't bloat core

### 2. Maintainability

- Each module has single responsibility
- Easier to understand what's essential vs. optional
- Reduced cognitive load when reading core

### 3. Extensibility

- Add features without touching core
- Core remains stable while features evolve
- Clear extension points

### 4. Testing

- Test features in isolation
- Core tests focus on essential functionality
- Feature tests independent of core

### 5. Clarity

- Core is truly minimal - obvious what's essential
- Features are clearly opt-in
- Architecture matches design intent

---

## Implementation Strategy

### Recommended Order

**Phase 1** (Easy Win):

- Extract dynamic language registration to `loki_languages.c` (57 lines)
- Low risk, logical consistency improvement

**Phase 2** (Medium Impact):

- Extract selection & clipboard to `loki_selection.c` (127 lines)
- Extract search to `loki_search.c` (100 lines)
- Moderate complexity, clear boundaries

**Phase 3** (Maximum Impact):

- Extract modal editing to `loki_modal.c` (543 lines)
- Largest change, biggest cleanup
- Requires careful refactoring of `editor_process_keypress()`

### Testing Strategy

After each phase:

1. `make clean && make` - Verify compilation
2. `make test` - Run test suite
3. Manual testing - Open files, edit, save, search, modal operations
4. Integration testing - Ensure all features still work

### Risk Mitigation

- **One phase at a time** - Don't combine extractions
- **Commit after each phase** - Easy rollback if issues arise
- **Preserve public API** - No breaking changes to include/loki/*.h
- **Test thoroughly** - Each feature must work after extraction

---

## Success Criteria

### Quantitative

- [ ] loki_core.c reduced to ~660 lines of code
- [ ] 56% reduction in core complexity
- [ ] 4 new focused modules created
- [ ] All tests passing
- [ ] Clean compilation (no new warnings)

### Qualitative

- [ ] Core contains only essential editor functionality
- [ ] Features clearly separated into dedicated modules
- [ ] Architecture matches "minimal core" design goal
- [ ] Code organization reflects separation of concerns
- [ ] Easy to identify what's core vs. what's optional

---

## Future Considerations

### Potential Further Extractions

Once initial plan complete, consider:

1. **Color Management** → `loki_colors.c`
   - `init_default_colors()`
   - `editor_format_color()`
   - Color scheme management

2. **Syntax Highlighting** → Move more to `loki_languages.c`
   - `editor_update_syntax()` might be language-specific
   - Keep only interface in core

3. **REPL Integration** → Already in `loki_lua.c`
   - Verify no REPL code in core
   - REPL should be completely separate

### Alternative Architectures

If further minimization needed:

- **Plugin System**: Make features loadable modules
- **Core + Facade**: Thin core, feature facade layer
- **Event-Driven**: Core emits events, features respond

---

## Notes

- This plan aligns with the original kilo design: minimal editor core
- Phase 6 completed context passing - architecture ready for extraction
- Dynamic language registration is already in wrong place (should be with languages)
- Modal editing is the biggest opportunity for cleanup (543 lines)
- Goal is maintainability and clarity, not just line count reduction

---

## References

- Original analysis: 2025-10-12 conversation
- Context passing migration: CHANGELOG.md 0.4.3
- Language extraction: CHANGELOG.md 0.4.4
- Project philosophy: CLAUDE.md "minimalist text editor"
