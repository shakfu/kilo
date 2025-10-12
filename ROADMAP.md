# Loki Roadmap

This roadmap outlines future development ideas for Loki, organized around the **minimal core principle**: keep `loki_core.c` focused on essential editor infrastructure while adding capabilities through feature modules.

## Philosophy

**Minimal Core Principle:**

- Core remains < 1,500 lines (currently 1,336 lines)
- New features implemented as separate modules
- Each module has single, well-defined responsibility
- Core provides only: terminal I/O, buffers, rendering, syntax infrastructure, file I/O

**Module Design Guidelines:**

- Self-contained with clear API boundaries
- Testable in isolation
- Optional (can be compiled out if not needed)
- No direct dependencies on other feature modules

---

## Near-Term Improvements (v0.5.x)

### New Feature Modules

#### [x] 1. Undo/Redo Module (`loki_undo.c`) ⭐ High Priority

**Impact:** Essential for production use
**Complexity:** Medium (~200-300 lines)

**Implementation:**

- Circular buffer of edit operations (insert/delete with position)
- Store: operation type, position, content, reverse operation
- Commands: `u` (undo), `Ctrl-R` (redo) in NORMAL mode
- Memory overhead: ~100KB for 1000 operations

**API:**

```c
void undo_push(editor_ctx_t *ctx, undo_op_t *op);
int undo_pop(editor_ctx_t *ctx);
int redo_pop(editor_ctx_t *ctx);
void undo_clear(editor_ctx_t *ctx);
```

**Integration Points:**

- Hook into `editor_insert_char()`, `editor_del_char()`, `editor_insert_newline()`
- Store before-state in undo buffer
- No changes to core required

---

#### [x] 2. Multiple Buffers Module (`loki_buffers.c`) ⭐ High Priority

**Impact:** Edit multiple files simultaneously
**Complexity:** Medium (~250-350 lines)

**Implementation:**

- Array of `editor_ctx_t` structures (one per buffer)
- Tab-like interface in status bar
- Commands: `Ctrl-T` new buffer, `Ctrl-W` close, `Ctrl-Tab` switch
- Each buffer maintains independent state (cursor, content, syntax)

**API:**

```c
int buffer_create(const char *filename);
void buffer_close(int buffer_id);
void buffer_switch(int buffer_id);
int buffer_count(void);
editor_ctx_t *buffer_get(int buffer_id);
```

**Benefits:**

- Leverages existing context-passing architecture
- No global state conflicts (each buffer is independent `editor_ctx_t`)
- Natural fit with current design

---

#### 3. Clipboard Integration Module (`loki_clipboard.c`)

**Impact:** System clipboard copy/paste over SSH
**Complexity:** Low (~50-100 lines)

**Implementation:**

- Extend OSC 52 support (already in `loki_selection.c`)
- Add OSC 52 query support for paste (terminal-dependent)
- Commands: `p`/`P` (paste before/after) in NORMAL mode
- Fallback: internal clipboard buffer

**API:**

```c
void clipboard_set(const char *text);
char *clipboard_get(void);
int clipboard_available(void);
```

**Note:** OSC 52 read support varies by terminal emulator

---

### Feature Module Enhancements

#### [x] 4. Modal Editing Enhancements (`loki_modal.c`)

**Impact:** More vim-like editing power
**Complexity:** Low-Medium (incremental additions)

**Additions:**

- **Motion commands:**
  - `w`/`b` - word forward/backward
  - `0`/`$` - start/end of line
  - `gg`/`G` - start/end of file
  - `%` - matching bracket
- **Text objects:**
  - `diw` - delete inner word
  - `ci"` - change inside quotes
  - `dap` - delete around paragraph
- **More insert commands:**
  - `A` - append at end of line
  - `I` - insert at start of line
  - `C` - change to end of line
- **Visual mode enhancements:**
  - `V` - line-wise visual mode
  - `Ctrl-V` - block visual mode (column editing)

**Lines Added:** ~150-200 to `loki_modal.c`

---

#### 5. Search Enhancements (`loki_search.c`)

**Impact:** More powerful text finding
**Complexity:** Medium (~100-150 lines)

**Additions:**

- **POSIX regex support** via `<regex.h>` (standard library)
- **Replace functionality:** Find-and-replace with confirmation
- **Search history:** Up/down arrows to recall previous searches
- **Case sensitivity toggle:** `Ctrl-I` during search
- **Commands in NORMAL mode:**
  - `/` - forward search
  - `?` - backward search
  - `n`/`N` - next/previous match

**Example:**

```c
int search_regex(editor_ctx_t *ctx, const char *pattern, int flags);
void search_replace(editor_ctx_t *ctx, const char *find, const char *replace);
```

---

#### 6. Language Module Enhancements (`loki_languages.c`)

**Impact:** Better syntax highlighting
**Complexity:** Low-Medium (incremental)

**Additions:**

- **More built-in languages:**
  - Rust, Go, TypeScript (move from `.loki/languages/` to built-in)
  - Shell scripts, Makefiles
  - HTML, CSS, JSON
- **Semantic highlighting hooks:**
  - Allow Lua to override/extend syntax rules
  - Cache compiled regex patterns
- **Tree-sitter integration (future):**
  - Optional module for advanced parsing
  - Separate from core, loaded dynamically

---

## Mid-Term Improvements (v0.6.x)

### New Feature Modules

#### 7. Split Windows Module (`loki_windows.c`)

**Impact:** View multiple locations/files simultaneously
**Complexity:** High (~300-400 lines)

**Implementation:**

- Horizontal/vertical panes
- Each pane has independent viewport into buffer
- Commands: `Ctrl-X 2` (split horizontal), `Ctrl-X 3` (split vertical)
- Window navigation: `Ctrl-X o` (cycle windows)

**Architecture:**

```c
typedef struct window {
    editor_ctx_t *ctx;  // Buffer being displayed
    int row, col;       // Window position on screen
    int rows, cols;     // Window dimensions
} window_t;

void window_split_horizontal(void);
void window_split_vertical(void);
void window_close(window_t *win);
void window_switch(window_t *win);
```

**Rendering:**

- Each window calls `editor_refresh_screen()` with adjusted viewport
- Status bar shows active window indicator
- Cursor only shown in active window

---

#### 8. Macro Recording Module (`loki_macros.c`)

**Impact:** Automate repetitive edits
**Complexity:** Low-Medium (~150-200 lines)

**Implementation:**

- Record keystroke sequences (vim-style)
- Commands: `q{register}` to record, `@{register}` to replay
- Store as command arrays in named registers (a-z)
- Multiple named registers for different macros

**API:**

```c
void macro_record_start(char register_name);
void macro_record_stop(void);
void macro_replay(char register_name);
int macro_is_recording(void);
```

**Storage:** In-memory only (not persisted across sessions)

---

#### 9. Auto-Indent Module (`loki_indent.c`)

**Impact:** Developer quality-of-life
**Complexity:** Low (~100-150 lines)

**Implementation:**

- Copy indentation from previous line on Enter
- Electric dedent for closing braces `}`
- Tab/space aware (detect from file or config)
- Language-specific rules (optional)

**API:**

```c
int indent_get_level(editor_ctx_t *ctx, int row);
void indent_apply(editor_ctx_t *ctx, int row);
void indent_electric_char(editor_ctx_t *ctx, char c);
```

**Integration:** Hook into `editor_insert_newline()` and `editor_insert_char()`

---

### Architecture Improvements

#### 10. Configuration System

**Impact:** Per-module configuration without touching core
**Complexity:** Medium (~200 lines)

**Implementation:**

- Configuration registry (key-value store)
- Modules register their config options
- Loaded from `~/.loki/config.toml` or `.loki/config.toml`
- Exposed to Lua via `loki.config` table

**Example config:**

```toml
[core]
tab_width = 4
line_numbers = false

[modal]
enabled = true
leader_key = "\\"

[search]
case_sensitive = false
use_regex = true

[indent]
auto_indent = true
detect_indent = true
```

**API:**

```c
void config_register(const char *module, const char *key, config_type_t type, void *default_value);
void config_set(const char *module, const char *key, void *value);
void *config_get(const char *module, const char *key);
```

---

#### 11. Module Plugin System

**Impact:** User extensibility without core changes
**Complexity:** Medium-High (~250-300 lines)

**Implementation:**

- Modules as shared objects (.so/.dylib)
- Load from `~/.loki/plugins/` at startup
- Modules export standard interface:

  ```c
  struct loki_plugin {
      const char *name;
      const char *version;
      int (*init)(editor_ctx_t *ctx);
      void (*cleanup)(editor_ctx_t *ctx);
      int (*on_keypress)(editor_ctx_t *ctx, int key);
      void (*on_save)(editor_ctx_t *ctx);
  };
  ```

- Plugin discovery via `dlopen()`/`dlsym()`

**Benefits:**

- Community can contribute plugins without core merges
- Experimental features stay separate
- Users choose which plugins to load

---

## Long-Term Vision (v0.7.x+)

### Advanced Features

#### 12. LSP Client Module (`loki_lsp.c`)

**Impact:** IDE-like intelligence (autocomplete, diagnostics, go-to-definition)
**Complexity:** Very High (~600-800 lines)

**Implementation:**

- Minimal Language Server Protocol over stdio
- JSON-RPC parsing (simple state machine, no library needed)
- Features: autocomplete, diagnostics, hover, go-to-definition
- Per-language LSP server configuration

**Note:** Most transformative feature but significant complexity

---

#### 13. Git Integration Module (`loki_git.c`)

**Impact:** VCS awareness in editor
**Complexity:** Medium-High (~200-400 lines)

**Implementation:**

- Show diff markers in gutter (+/-/~ for add/delete/modify)
- Commands: `Ctrl-G s` (stage/unstage hunks), `Ctrl-G c` (commit)
- Use `libgit2` or shell out to `git` command
- Status line shows branch name and modification count

---

#### 14. Tree-sitter Integration Module (`loki_treesitter.c`)

**Impact:** Accurate syntax highlighting and structure
**Complexity:** Very High (~500-700 lines)

**Implementation:**

- Optional module (not required for basic use)
- Language parsers loaded dynamically
- Provides AST for precise highlighting
- Enables features: fold/unfold, smart navigation

---

## Core Optimizations (No Feature Additions)

### Performance Improvements

#### 15. Rendering Optimizations

**Target:** Faster screen refresh for large files
**Changes to core:** Yes, but no new features

**Optimizations:**

- **Incremental rendering:** Only redraw changed lines
- **Dirty region tracking:** Track which screen regions need update
- **Double buffering:** Build screen buffer once, diff before writing
- **Smart scrolling:** Use VT100 scroll sequences instead of full redraw

**Benefit:** 2-5x faster rendering for large files (10K+ lines)

---

#### 16. Memory Efficiency

**Target:** Reduce memory footprint
**Changes to core:** Yes, but no new features

**Optimizations:**

- **Lazy row rendering:** Don't render rows outside viewport
- **Compact row storage:** Use gap buffer for row chars
- **Shared syntax state:** Don't duplicate syntax info for identical rows
- **Memory pools:** Reuse allocated row structures

**Benefit:** 30-50% less memory for large files

---

#### 17. Large File Support

**Target:** Handle files > 1MB efficiently
**Changes to core:** Yes, but no new features

**Improvements:**

- **Piece table data structure:** Replace row array with piece table
- **Lazy loading:** Load file in chunks, not all at once
- **Virtual scrolling:** Only keep visible + buffer rows in memory
- **Async file I/O:** Load/save in background thread

**Benefit:** Can edit 100MB+ files smoothly

---

## Non-Goals

Things we explicitly **won't** add (to maintain minimalism):

❌ **GUI version** - Terminal-native is core identity
❌ **Mouse-heavy features** - Keyboard-first design
❌ **Complex project management** - Use external tools
❌ **Debugger integration** - Scope creep
❌ **Email/web browser** - We're an editor, not Emacs
❌ **Built-in terminal** - Use tmux/screen
❌ **AI training** - Integration yes, training no

---

## Implementation Strategy

### Prioritization Matrix

**High Impact, Low Complexity (Do First):**

1. Undo/Redo module ⭐
2. Modal editing enhancements ⭐
3. Auto-indent module ⭐
4. Clipboard integration ⭐

**High Impact, Medium Complexity (Do Second):**

1. Multiple buffers module
2. Search enhancements (regex, replace)
3. Configuration system
4. Macro recording

**High Impact, High Complexity (Long-term):**

1. Split windows module
2. LSP client module
3. Plugin system
4. Git integration

**Core Optimizations (Ongoing):**

- Rendering optimizations
- Memory efficiency
- Large file support

### Release Cadence

- **v0.5.x** - Undo/redo, modal enhancements, auto-indent (3-4 months)
- **v0.6.x** - Multiple buffers, search enhancements, configuration (3-4 months)
- **v0.7.x** - Split windows, macros, plugin system (4-6 months)
- **v0.8.x** - LSP client, Git integration (6-12 months)

### Community Contributions

**Areas welcoming contributions:**

- New language definitions (add to `.loki/languages/`)
- Color themes (add to `.loki/themes/`)
- Lua modules (editor utilities, AI integrations)
- Bug fixes and optimizations
- Documentation improvements

**Module contribution guidelines:**

- Must not require changes to core
- Must be self-contained with clear API
- Must include tests and documentation
- Must compile independently (can be disabled via CMake option)

---

## Measuring Success

**Minimal Core Maintained:**

- `loki_core.c` stays < 1,500 lines ✅
- Core has zero feature-specific code ✅
- All new capabilities in separate modules ✅

**Quality Metrics:**

- All tests passing (zero tolerance for failures)
- No memory leaks (valgrind clean)
- No compiler warnings (-Wall -Wextra -pedantic)
- Binary size < 150KB (with all modules)

**User Experience:**

- Fast: < 50ms to open 10K line file
- Responsive: No lag during editing
- Reliable: No crashes, no data loss
- Intuitive: Vim users feel at home

---

## Conclusion

The modular architecture provides a strong foundation for growth. By keeping the core minimal and adding capabilities through feature modules, we can build a powerful editor that remains maintainable, understandable, and true to its minimalist roots.

**Next Steps:**

1. Implement undo/redo module (highest priority)
2. Enhance modal editing with basic vim motions
3. Add configuration system for module settings
4. Begin work on multiple buffers support

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
