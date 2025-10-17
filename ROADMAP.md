# Loki Roadmap

This roadmap outlines future development for Loki, organized around the **modular architecture principle**: keep core components focused on essential infrastructure while adding capabilities through feature modules.

## Current Status (v0.5.0)

**Project Metrics:**

- **Core:** **891 lines** (loki_core.c) - **33% below 1,000 line milestone!** 🎉
- **Total codebase:** ~10,500 lines across modular components
- **Binary size:** ~300KB (editor), ~316KB (REPL)
- **Module count:** 13 separate modules (syntax, indent, buffers, undo, modal, search, markdown, etc.)
- **Test coverage:** 12 test suites, 100% pass rate

**Recently Completed (v0.5.0):**

- ✅ **Auto-indentation module** - Smart indentation with electric dedent (`loki_indent.c`, 280 lines, 25 tests)
- ✅ **Syntax highlighting extraction** - Moved to dedicated `loki_syntax.c` module (291 lines)
- ✅ Undo/Redo system with circular buffer
- ✅ Multiple buffers (tabs) with Ctrl-T/Ctrl-W navigation
- ✅ Modal editing with vim-like motions (hjkl, word motions, visual mode)
- ✅ CommonMark markdown support via cmark (parse, render, analysis)
- ✅ Tab completion in Lua REPL
- ✅ Async HTTP client for AI/API integration
- ✅ Dynamic language registration system
- ✅ Advanced Lua scripting with modular configuration

**Architectural Milestones:**

1. **Core Below 1,000 Lines**: The syntax highlighting extraction brought `loki_core.c` to 891 lines (22.6% reduction from 1,152 lines)
2. **13 Modules**: Auto-indent module marks the 13th feature module, demonstrating sustained modular architecture

## Philosophy

**Modular Architecture Principle:**

- Core remains focused on essential editor infrastructure (**now < 900 lines!** ✅)
- Features implemented as separate, composable modules
- Each module has single, well-defined responsibility
- Clear API boundaries between components
- Testable in isolation with zero tolerance for test failures

**Current Module Breakdown:**

1. **`loki_core.c`** (891 lines) - Terminal I/O, buffer management, file I/O, rendering
2. **`loki_syntax.c`** (291 lines) - Syntax highlighting and color formatting
3. **`loki_indent.c`** (280 lines) - Auto-indentation and electric dedent
4. **`loki_languages.c`** (494 lines) - Language definitions and markdown highlighting
5. **`loki_buffers.c`** (426 lines) - Multiple buffer (tab) management
6. **`loki_undo.c`** (474 lines) - Undo/redo with circular buffer
7. **`loki_modal.c`** (510 lines) - Vim-like modal editing
8. **`loki_selection.c`** (156 lines) - Text selection and OSC 52 clipboard
9. **`loki_search.c`** (128 lines) - Incremental search
10. **`loki_command.c`** (491 lines) - Ex-mode command system
11. **`loki_terminal.c`** (125 lines) - Terminal control and window size
12. **`loki_markdown.c`** (421 lines) - CommonMark parsing and rendering
13. **`loki_lua.c`** (1,400+ lines) - Lua integration and API bindings

**Module Design Guidelines:**

- Self-contained with minimal coupling
- Optional (can be compiled out via CMake if not needed)
- No direct dependencies on other feature modules
- Comprehensive error handling and bounds checking

---

## Near-Term Improvements (v0.5.x - v0.6.x)

### Completed in v0.5.0

#### ✅ Auto-Indent Module (`loki_indent.c`) - **COMPLETED**

**Implementation achieved:**

- ✅ Copy indentation from previous line on Enter
- ✅ Electric dedent for closing braces `}`, `]`, `)`
- ✅ Tab/space detection (smart tabs vs spaces heuristic)
- ✅ Bracket matching for correct dedent target
- ✅ Nested brace support
- ✅ Configurable width (1-8 spaces, default 4)
- ✅ Enable/disable toggles

**Actual implementation:** 280 lines (exceeded initial estimate of 150-200)

**Testing:** 25 unit tests, 100% pass rate

**API delivered:**

```c
void indent_init(editor_ctx_t *ctx);
int indent_get_level(editor_ctx_t *ctx, int row);
int indent_detect_style(editor_ctx_t *ctx);
void indent_apply(editor_ctx_t *ctx);
int indent_electric_char(editor_ctx_t *ctx, int c);
void indent_set_enabled(editor_ctx_t *ctx, int enabled);
void indent_set_width(editor_ctx_t *ctx, int width);
```

**Integration:** Hooked into `editor_insert_newline()` and `editor_insert_char()` as planned

---

### Priority Features for v0.6.x

#### 1. Enhanced Clipboard Integration (`loki_clipboard.c`) ⭐ **HIGHEST PRIORITY**

**Status:** Partial (OSC 52 in selection module, needs expansion)

**Impact:** Better system clipboard integration
**Complexity:** Low (~100 lines)

**Enhancements needed:**

- OSC 52 query support for paste (terminal-dependent)
- Dedicated clipboard commands: `"+y` (yank to system), `"+p` (paste from system)
- Internal clipboard buffer as fallback
- Support for multiple clipboard registers (vim-style)

**API expansion:**

```c
void clipboard_set(const char *text, char register);
char *clipboard_get(char register);
int clipboard_system_available(void);  // Check if OSC 52 works
```

**Integration:** Extend existing `loki_selection.c` module

---

#### 2. Configuration File System ⭐ **HIGH PRIORITY**

**Impact:** User customization without recompilation
**Complexity:** Medium (~200-250 lines)
**Status:** Not started (currently Lua-only config)

**Implementation:**

- TOML configuration file support
- Load from `~/.loki/config.toml` (global) and `.loki/config.toml` (project)
- Register/validate config options per module
- Expose to Lua via `loki.config` table
- Hot-reload on save (optional)

**Example config:**

```toml
[editor]
tab_width = 4
line_numbers = true
auto_indent = true

[modal]
enabled = true
leader_key = "\\"

[theme]
name = "dracula"

[search]
case_sensitive = false
use_regex = true
```

**API:**

```c
void config_register(const char *module, const char *key, config_type_t type, void *default_value);
void config_set(const char *module, const char *key, void *value);
void *config_get(const char *module, const char *key);
int config_load_file(const char *path);
```

**Benefits:** Easier onboarding, standard config format, no Lua required for basics

---

### Completed Features (v0.4.x)

#### ✅ Syntax Highlighting Module (`loki_syntax.c`)

**Status:** Completed in v0.4.8

- Extracted all syntax highlighting logic from `loki_core.c`
- **Core reduction:** 1,152 → 891 lines (22.6% reduction, 261 lines removed)
- 7 functions with consistent `syntax_*` naming:
  - `syntax_is_separator()` - Character separator detection
  - `syntax_row_has_open_comment()` - Multi-line comment state tracking
  - `syntax_name_to_code()` - String to HL_* constant mapping
  - `syntax_update_row()` - Main syntax highlighting logic
  - `syntax_format_color()` - RGB color escape sequence formatting
  - `syntax_select_for_filename()` - File extension to syntax mapping
  - `syntax_init_default_colors()` - Default color initialization
- Supports all existing languages (C, Python, Lua, Markdown, etc.)
- Supports dynamic language registration via Lua
- True color (24-bit RGB) rendering
- ~290 lines in dedicated module

**Benefits:**
- Clean separation: syntax is a feature, not core infrastructure
- Extensible: Future enhancements (tree-sitter, LSP) stay in module
- Testable: Isolated unit testing possible
- Optional: Can be compiled out for minimal builds

---

#### ✅ Undo/Redo Module (`loki_undo.c`)

**Status:** Completed in v0.4.x

- Circular buffer with 1000 operation capacity
- Commands: `u` (undo), `Ctrl-R` (redo) in NORMAL mode
- Integrated with all edit operations
- ~450 lines including tests

---

#### ✅ Multiple Buffers Module (`loki_buffers.c`)

**Status:** Completed in v0.4.x

- Tab-style buffer switching
- Commands: `Ctrl-T` (new), `Ctrl-W` (close), `Ctrl-Tab` (switch)
- Status bar shows buffer list
- ~400 lines

---

#### ✅ Modal Editing (`loki_modal.c`)

**Status:** Completed in v0.4.x

**Implemented motions:**

- `hjkl` - directional movement
- `w`/`b`/`e` - word motions
- `0`/`$` - line start/end
- `gg`/`G` - file start/end
- `{`/`}` - paragraph motions
- `%` - bracket matching

**Implemented commands:**

- `i`/`a` - insert before/after
- `o`/`O` - open line below/above
- `x`/`X` - delete char
- `dd` - delete line
- `yy` - yank line
- Visual mode (`v`) with selections

**Lines:** ~510 lines

---

#### ✅ Markdown Support (`loki_markdown.c`)

**Status:** Completed in v0.4.x

- CommonMark parsing via cmark library
- Render to HTML, XML, LaTeX, man pages
- Document analysis (count headings, links, code blocks)
- Extract structure (headings, links)
- Exposed to Lua API
- ~300 lines

---

### Enhancement Priorities

#### 4. Search Enhancements (`loki_search.c`) - **MEDIUM PRIORITY**

**Status:** Basic search exists, needs enhancement
**Impact:** More powerful text finding
**Complexity:** Medium (~150-200 lines)

**Planned additions:**

- **POSIX regex support** via `<regex.h>` (standard library)
- **Replace functionality:** Find-and-replace with confirmation (`:%s/find/replace/gc`)
- **Search history:** Up/down arrows to recall previous searches
- **Case sensitivity toggle:** Smart-case (case-insensitive unless uppercase present)
- **Search commands in NORMAL mode:**
  - `/` - forward search (already exists as Ctrl-F)
  - `?` - backward search
  - `n`/`N` - next/previous match
  - `*`/`#` - search word under cursor

**API additions:**

```c
int search_regex(editor_ctx_t *ctx, const char *pattern, int flags);
void search_replace(editor_ctx_t *ctx, const char *find, const char *replace, int flags);
void search_word_under_cursor(editor_ctx_t *ctx, int forward);
```

---

#### 5. Line Numbers Module (`loki_linenumbers.c`) - **MEDIUM PRIORITY**

**Impact:** Navigation and reference
**Complexity:** Low (~120-150 lines)
**Status:** Not started

**Implementation:**

- Optional gutter display with configurable width
- Relative/absolute number modes (`:set relativenumber`)
- Adjust screen column offset for gutter
- Highlight current line number
- Toggle via command or config

**API:**

```c
void linenumbers_enable(editor_ctx_t *ctx, int relative);
void linenumbers_disable(editor_ctx_t *ctx);
int linenumbers_get_width(editor_ctx_t *ctx);
void linenumbers_render(editor_ctx_t *ctx, int row, char *buf);
```

**Integration:** Hook into screen rendering in `loki_editor.c`

---

#### 6. Language Enhancements (`loki_languages.c`) - **LOW PRIORITY**

**Status:** Dynamic registration working well
**Complexity:** Low-Medium (incremental)

**Potential additions:**

- **More built-in languages:**
  - Move popular `.loki/languages/` to built-in (Rust, Go, TypeScript)
  - Shell scripts, Makefiles, JSON, YAML
  - HTML, CSS (basic)
- **Semantic highlighting hooks:**
  - Allow Lua scripts to override/extend syntax rules
  - Context-aware highlighting (function names, variables)
- **Performance:**
  - Cache compiled regex patterns
  - Lazy syntax highlighting (only visible rows)

**Note:** Current dynamic system via `.loki/languages/` works well; built-in expansion is low priority

---

## Mid-Term Improvements (v0.6.x)

### Advanced Editing Features

#### 7. Split Windows Module (`loki_windows.c`) - **HIGH IMPACT**

**Impact:** View multiple locations/files simultaneously
**Complexity:** High (~350-450 lines)
**Status:** Not started

**Implementation:**

- Horizontal/vertical splits (vim-style)
- Each window has independent viewport + cursor into buffer
- Commands: `:split` / `:vsplit` or `Ctrl-W s` / `Ctrl-W v`
- Window navigation: `Ctrl-W hjkl` (vim-style)
- Resize: `Ctrl-W +/-/</>` or `:resize N`

**Architecture:**

```c
typedef struct window {
    editor_ctx_t *ctx;      // Buffer being displayed
    int screen_row, screen_col;  // Window position
    int rows, cols;         // Window dimensions
    int is_active;          // Active window flag
} window_t;

window_t *window_split(window_t *win, int vertical);
void window_close(window_t *win);
void window_switch(int direction);  // HJKL
void window_resize(window_t *win, int delta_rows, int delta_cols);
```

**Rendering:**

- Each window renders independently with clipped viewport
- Status line per window (bottom border)
- Only active window shows cursor
- Shared buffer support (same file in multiple windows)

**Challenges:** Screen coordinate math, resize handling, terminal size changes

---

#### 8. Macro Recording Module (`loki_macros.c`) - **MEDIUM IMPACT**

**Impact:** Automate repetitive edits (vim muscle memory)
**Complexity:** Medium (~180-220 lines)
**Status:** Not started

**Implementation:**

- Record keystroke sequences exactly as vim does
- Commands: `q{a-z}` to record, `q` to stop, `@{a-z}` to replay, `@@` repeat last
- Store in named registers (a-z), up to 26 macros
- Count prefix: `10@a` repeats macro 'a' 10 times

**API:**

```c
void macro_record_start(char register_name);
void macro_record_stop(void);
void macro_record_key(int key);  // Called for each keystroke
void macro_replay(char register_name, int count);
int macro_is_recording(void);
```

**Storage:**

- In-memory during session (not persisted)
- Future: Persist to `~/.loki/macros` as Lua functions

**Integration:** Hook all key processing in modal mode

---

#### 9. Bracket/Paren Matching Visualization - **LOW COMPLEXITY, HIGH VALUE**

**Impact:** Visual pairing feedback
**Complexity:** Very Low (~80-100 lines)
**Status:** Not started

**Implementation:**

- Highlight matching `()`, `{}`, `[]`, `<>` when cursor on bracket
- Show match briefly in status if off-screen (with line number)
- Flash or underline the matching bracket
- Support multi-line matching with stack-based search

**API:**

```c
int find_matching_bracket(editor_ctx_t *ctx, int row, int col);
void highlight_bracket_pair(editor_ctx_t *ctx);
```

**Integration:** Call during cursor movement in `editor_refresh_screen()`

---

### Development Infrastructure

#### 10. Language Server Protocol (LSP) Client - **FUTURE CONSIDERATION**

**Impact:** IDE-like features (autocomplete, go-to-def, diagnostics)
**Complexity:** Very High (~800-1200 lines)
**Status:** Research phase

**Scope (minimal viable LSP):**

- JSON-RPC 2.0 over stdio (no websockets/TCP)
- Features: textDocument/completion, textDocument/hover, textDocument/definition
- Language-specific server config in `.loki/lsp.toml`
- Async message handling (don't block editor)

**Challenges:**

- JSON parsing without dependencies (write minimal parser or use single-header library)
- Message framing (Content-Length headers)
- State synchronization (incremental updates)
- Popup rendering for completions/hover

**Decision:** Defer to v0.7+ due to complexity; focus on simpler features first

---

#### 11. Plugin System via Lua - **ALTERNATIVE TO BINARY PLUGINS**

**Impact:** User extensibility (already partially exists)
**Complexity:** Low (~150 lines additional)
**Status:** Foundation exists, needs formalization

**Current state:**

- Lua already embedded with extensive API
- Users can write modules in `.loki/modules/`
- Can register commands, keybindings, ex commands

**Enhancements needed:**

- Formalize plugin discovery (scan `.loki/plugins/` for `plugin.lua`)
- Plugin metadata (`name`, `version`, `author`, `dependencies`)
- Plugin lifecycle hooks (`on_load`, `on_buffer_open`, `on_save`, `on_exit`)
- Plugin manager commands (`:PluginList`, `:PluginEnable`, `:PluginDisable`)

**Example plugin structure:**

```lua
-- .loki/plugins/my_plugin/plugin.lua
return {
    name = "my_plugin",
    version = "1.0.0",

    on_load = function()
        loki.register_command("myplugin", function() ... end)
    end,

    on_save = function(ctx)
        -- Auto-format on save, etc.
    end
}
```

**Benefits:**

- No binary compatibility issues (pure Lua)
- Easy to write and distribute (just .lua files)
- Full editor API access
- Simpler than C plugin system

---

## Long-Term Vision (v0.7.x+)

### Transformative Features

#### 12. Git Integration Module (`loki_git.c`) - **HIGH VALUE FOR DEVELOPERS**

**Impact:** VCS awareness in editor
**Complexity:** Medium-High (~300-400 lines)
**Status:** Not started

**Phase 1 - Read-only awareness:**

- Git status in status line (branch, dirty state)
- Diff markers in gutter (+/-/~ for add/delete/modify)
- Blame information (`:Git blame` shows author/date per line)
- Jump to next/prev hunk (`]c` / `[c`)

**Phase 2 - Interactive git:**

- Stage/unstage hunks (`:Gstage`, `:Gunstage`)
- Commit from editor (`:Gcommit` opens commit message buffer)
- View git log (`:Glog` shows commit history)
- Revert hunks (`:Grevert` undo changes)

**Implementation approach:**

- Shell out to `git` commands (no libgit2 dependency)
- Parse git output (simple parsing, stable formats)
- Cache git status per buffer (invalidate on save/external change)

**API:**

```c
int git_get_status(editor_ctx_t *ctx, git_status_t *status);
void git_get_hunks(editor_ctx_t *ctx, git_hunk_t **hunks, int *count);
int git_stage_hunk(git_hunk_t *hunk);
void git_render_gutter(editor_ctx_t *ctx, int row, char *buf);
```

---

#### 13. Tree-sitter Integration (Optional) - **RESEARCH PHASE**

**Impact:** Accurate syntax highlighting + structural navigation
**Complexity:** Very High (~600-800 lines)
**Status:** Research only

**Benefits:**

- Accurate, context-aware syntax highlighting
- Structural navigation (functions, classes)
- AST-based features (fold/unfold, refactoring)
- Language-agnostic (works for any tree-sitter grammar)

**Challenges:**

- Large dependency (~500KB+ per language)
- Complex API integration
- Incremental parsing complexity
- May conflict with existing simple highlighting

**Decision:** Evaluate after other features complete. May not be worth complexity for minimalist editor.

**Alternative:** Keep simple regex-based highlighting, add semantic hints via LSP instead

---

#### 14. Advanced Terminal Features

**Mouse Support** - Very Low Complexity (~100 lines)

- Enable SGR mouse mode: `\033[?1006h`
- Click to position cursor
- Drag to select text
- Scroll wheel support
- Right-click context menu (optional)

**Popup/Floating Windows** - Medium Complexity (~250 lines)

- Render popups over main editor area
- Use cases: autocomplete menus, hover info, command palette
- Z-order management
- Proper clipping and borders

**True Color & Style Support** - Low Complexity (~80 lines)

- Already have 24-bit RGB support
- Add: bold, italic, underline, strikethrough
- Undercurl for diagnostics (if terminal supports)
- Configurable per theme

---

## Performance & Scalability (Ongoing)

### Core Optimizations

#### 15. Rendering Performance - **MEDIUM PRIORITY**

**Current state:** Full screen redraw on every change
**Target:** 2-5x faster for large files
**Complexity:** Medium (core changes required)

**Optimizations:**

- **Dirty line tracking:** Only redraw modified lines
- **Viewport-only rendering:** Skip rendering off-screen rows
- **Smart scrolling:** Use VT100 scroll regions (`\033[L`, `\033[M`)
- **Diff-based updates:** Compare before/after buffers, emit minimal changes

**Benefit:** Smooth editing in 10K+ line files

**Measurement:** Benchmark with `time ./loki large_file.txt` + scripted edits

---

#### 16. Memory Efficiency - **LOW PRIORITY**

**Current state:** ~1KB per line (row struct + render + highlight)
**Target:** 30-50% reduction
**Complexity:** High (data structure changes)

**Optimizations:**

- **Lazy syntax highlighting:** Only highlight visible + buffer rows
- **Shared render strings:** Deduplicate identical rendered rows
- **Compact row storage:** Pack flags, reduce padding
- **On-demand allocations:** Delay `render` allocation until needed

**Current memory usage:** Acceptable for files < 100K lines

**Decision:** Defer until memory becomes actual bottleneck

---

#### 17. Large File Support - **LOW PRIORITY**

**Current limitation:** Entire file loaded into memory
**Target:** Handle 100MB+ files
**Complexity:** Very High (architectural change)

**Approaches:**

1. **Piece table:** Replace row array (major refactor)
2. **Memory-mapped I/O:** Use mmap() for large files
3. **Chunked loading:** Load visible portion + small buffer
4. **Line index:** Build index for fast row seeking

**Current approach:** Works well for < 1MB files (typical use case)

**Decision:** Not a priority; users editing 100MB files should use specialized tools (hexedit, less, stream editors)

**Note:** If implemented, would be separate module (`loki_largefile.c`), not core change

---

## Non-Goals

Things we explicitly **won't** add (preserves minimalist identity):

❌ **GUI version** - Terminal-native is core identity; if you want GUI, use VSCode
❌ **Complex project management** - Use external tools (tmux, make, IDEs)
❌ **Debugger integration** - Out of scope; use gdb/lldb directly
❌ **Email/web browser** - We're an editor, not Emacs
❌ **Built-in terminal** - Use tmux/screen for multiplexing
❌ **AI training** - Integration via API yes, local model training no
❌ **Package manager** - Lua plugins are just files; use git to distribute
❌ **WYSIWYG editing** - Plain text only; markdown preview via external tool

**Rationale:** These features either:

1. Violate terminal-first principle
2. Require massive dependencies
3. Are better handled by dedicated tools
4. Would bloat the codebase beyond maintainability

---

## Implementation Strategy

### Prioritization Framework

**Completed in v0.5.0:**

1. ✅ Auto-indent module - Most requested feature (**COMPLETED** - 280 lines, 25 tests)

**Immediate Priorities (v0.6.0 - Next Release):**

2. Enhanced clipboard - Better workflow
3. Configuration file system (TOML) - Easier onboarding
4. Line numbers module - Low complexity, high value

**Secondary Priorities (v0.6.x series):**

5. Search enhancements (regex, replace) - Improve existing feature
6. Bracket matching visualization - Quick win

**Future Major Features (v0.6.x+):**

7. Split windows - High complexity, plan carefully
8. Macro recording - Medium complexity, high utility
9. Git integration (phase 1) - Read-only awareness first
10. Plugin formalization - Leverage existing Lua foundation

### Development Principles

**Before adding any feature, ask:**

1. **Does it require core changes?** If yes, very strong justification needed
2. **Can it be a separate module?** If yes, make it optional
3. **Can it be done in Lua?** If yes, document as example instead
4. **Does it maintain < 1500 line core?** Non-negotiable

**Quality gates:**

- All tests must pass (zero tolerance)
- No compiler warnings (-Wall -Wextra -pedantic)
- Memory leak free (valgrind clean)
- Bounds checking complete
- Documentation updated

### Release Philosophy

**Semantic versioning:** MAJOR.MINOR.PATCH

- **PATCH (0.4.x):** Bug fixes, no new features
- **MINOR (0.x.0):** New features, backward compatible
- **MAJOR (x.0.0):** Breaking changes (config, API, behavior)

**Release cycle:**

- **v0.5.0** - ✅ **COMPLETED** - Auto-indent module with 25 comprehensive tests
- **v0.6.x** - Focus: Enhanced clipboard, config system, line numbers (~2-3 months)
- **v0.7.x** - Focus: Split windows, search improvements, bracket matching (~3-4 months)
- **v0.8.x** - Focus: Git integration, macros, plugin formalization (~4-6 months)
- **v1.0.0** - Stability milestone: Feature-complete for core use cases

**v1.0 criteria (proposed):**

- Auto-indent working ✅ **COMPLETED in v0.5.0**
- Enhanced clipboard (copy/paste)
- Config system (TOML)
- Split windows
- Line numbers
- Git awareness (phase 1)
- Comprehensive test coverage (>80%)
- Documentation complete
- No known critical bugs
- Used in production by multiple developers

### Community Contributions

**Ways to contribute:**

1. **Language definitions** - Add to `.loki/languages/` (JavaScript, Python, Rust, etc.)
2. **Themes** - Add to `.loki/themes/` (Dracula, Monokai, Nord, Solarized, etc.)
3. **Lua modules** - Editor utilities, AI integrations, formatters
4. **Core modules** - New C modules following architecture guidelines
5. **Bug fixes** - All contributions welcome, large or small
6. **Documentation** - Tutorials, examples, API docs
7. **Testing** - Test coverage, edge cases, platform testing

**Module contribution guidelines:**

- Must not require changes to `loki_core.c`
- Must be self-contained with clear API (`loki_*.h` header)
- Must include unit tests and pass all existing tests
- Must be optional (can be compiled out via CMake)
- Must follow coding style (C99, no VLAs, bounds checking)
- Must update CLAUDE.md with module documentation

**Pull request checklist:**

- [ ] All tests pass (`make test`)
- [ ] No compiler warnings (`make` with strict flags)
- [ ] Valgrind clean (no leaks, no errors)
- [ ] Documentation updated (CLAUDE.md, comments)
- [ ] Example usage provided (if new feature)

---

## Measuring Success

**Architecture Goals:**

- ✅ `loki_core.c` < 1,500 lines (current: ~1,150)
- ✅ Core has zero feature-specific code
- ✅ All features in separate modules
- ✅ Modular design with clean APIs

**Quality Metrics:**

- ✅ All tests passing (zero tolerance)
- ✅ No compiler warnings (-Wall -Wextra -pedantic)
- ⚠️ Binary size: ~300KB (target: <200KB for core features)
- ⏳ Valgrind clean (ongoing testing)

**Performance Targets:**

- ✅ < 50ms to open 10K line file
- ✅ Responsive editing (no lag)
- ✅ No crashes, no data loss
- ✅ Vim users feel at home

**Adoption Metrics (qualitative):**

- Daily use by core developers ✅
- GitHub stars/forks trending ⏳
- Community contributions (PRs, issues) ⏳
- Real-world usage reports ⏳

---

## Current Status Summary

**What works well (v0.4.x):**

- ✅ Stable core editor with robust error handling
- ✅ Modal editing with vim-like motions
- ✅ Multiple buffers (tabs)
- ✅ Undo/redo system
- ✅ Syntax highlighting (dynamic language support)
- ✅ Markdown parsing and rendering (cmark)
- ✅ Lua scripting with extensive API
- ✅ Async HTTP for AI integration
- ✅ Tab completion in REPL
- ✅ **Auto-indent** (electric dedent, bracket matching) - **v0.5.0**

**What needs work:**

- 🔨 Configuration file system (ease of use)
- 🔨 Enhanced clipboard (better copy/paste)
- 🔨 Line numbers (common request)
- 🔨 Search & replace (enhance existing)
- 🔨 Split windows (complex but valuable)

**What's stable and proven:**

- Architecture: Modular design scales well
- Testing: Zero tolerance policy prevents regressions
- API: Lua integration is powerful and extensible
- Performance: Fast enough for typical use cases

---

## v0.5.0 Release - ✅ COMPLETED

**Implemented:**

1. ✅ **Auto-indent module** - Comprehensive implementation with electric dedent, bracket matching, and 25 tests
   - **Impact:** Highest user value achieved
   - **Quality:** 100% test pass rate, zero compiler warnings
   - **Integration:** Seamless integration with existing editor workflow

**Success criteria met:**

- ✅ Auto-indent works for all common languages (C, Python, Lua, JavaScript, etc.)
- ✅ Smart bracket matching and electric dedent
- ✅ Configurable width and enable/disable toggles
- ✅ Tab/space detection heuristic
- ✅ Comprehensive test coverage

---

## Next Steps (v0.6.0)

**Immediate focus (prioritized by impact/effort ratio):**

1. **Enhanced clipboard** (~1-2 weeks) - Better copy/paste workflow
2. **TOML config system** (~1-2 weeks) - Easier onboarding
3. **Line numbers module** (~1 week) - Quick win, common request
4. **Enhanced search** (~2 weeks) - Regex and replace support

**Total estimated time for v0.6.0:** 5-7 weeks

**Success criteria for v0.6.0:**

- Enhanced clipboard with multiple registers
- Config file replaces most Lua config needs
- Line numbers toggle-able and performant
- Search supports regex and replace

**Looking forward:**

The modular architecture has proven successful. By maintaining discipline around core separation and module boundaries, Loki can evolve into a powerful editor while preserving its minimalist, hackable nature.

The v0.5.0 release demonstrates that complex features can be added without compromising core simplicity - the auto-indent module added 280 lines of well-tested code in a completely separate module.

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines (if it exists), or open an issue to discuss new features.
