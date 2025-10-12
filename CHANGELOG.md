# CHANGELOG

All notable project-wide changes will be documented in this file. Note that each subproject has its own CHANGELOG.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) and [Commons Changelog](https://common-changelog.org). This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Types of Changes

- Added: for new features.
- Changed: for changes in existing functionality.
- Deprecated: for soon-to-be removed features.
- Removed: for now removed features.
- Fixed: for any bug fixes.
- Security: in case of vulnerabilities.

---

## [Unreleased]

## [0.4.5]

### Changed

- **Minimal Core Refactoring**: Extracted feature code from loki_core.c into dedicated modules
  - **Phase 1 - Dynamic Language Registration**:
    - Extracted dynamic language registry to `src/loki_languages.c` (77 lines)
    - Functions moved: `add_dynamic_language()`, `free_dynamic_language()`, `cleanup_dynamic_languages()`
    - Added getter functions: `get_dynamic_language(index)`, `get_dynamic_language_count()`
    - Updated `editor_select_syntax_highlight()` to use getters instead of direct array access
    - Made HLDB_dynamic array static (encapsulated within languages module)
    - Result: 61 lines removed from loki_core.c
  - **Phase 2 - Selection & Clipboard**:
    - Created `src/loki_selection.c` (156 lines) and `src/loki_selection.h`
    - Functions moved: `is_selected()`, `base64_encode()`, `copy_selection_to_clipboard()`
    - Isolated OSC 52 clipboard protocol implementation for SSH-compatible copy/paste
    - Result: 127 lines removed from loki_core.c
  - **Phase 3 - Search Functionality**:
    - Created `src/loki_search.c` (128 lines) and `src/loki_search.h`
    - Extracted `editor_find()` with complete incremental search implementation
    - Moved `KILO_QUERY_LEN` constant to search module
    - Added `editor_read_key()` declaration to `loki_internal.h` for module access
    - Result: 100 lines removed from loki_core.c
  - **Phase 4 - Modal Editing**:
    - Created `src/loki_modal.c` (407 lines) and `src/loki_modal.h`
    - Functions moved:
      - `is_empty_line()` - Check if line is blank/whitespace only
      - `move_to_next_empty_line()` - Paragraph motion (})
      - `move_to_prev_empty_line()` - Paragraph motion ({)
      - `process_normal_mode()` - NORMAL mode keypress handling
      - `process_insert_mode()` - INSERT mode keypress handling
      - `process_visual_mode()` - VISUAL mode keypress handling
      - `modal_process_keypress()` - Main entry point with mode dispatching
    - Replaced `editor_process_keypress()` implementation with delegation to modal module
    - Moved `KILO_QUIT_TIMES` constant to modal module
    - Added `editor_move_cursor()` declaration to `loki_internal.h`
    - Result: 369 lines removed from loki_core.c
  - **Overall Architecture Impact**:
    - **Total reduction**: 1,993 → 1,336 lines in loki_core.c (657 lines removed, 33% reduction)
    - **Module breakdown**:
      - `loki_core.c`: 1,336 lines (core functionality only)
      - `loki_languages.c`: 494 lines (language definitions + dynamic registry)
      - `loki_selection.c`: 156 lines (selection + OSC 52 clipboard)
      - `loki_search.c`: 128 lines (incremental search)
      - `loki_modal.c`: 407 lines (vim-like modal editing)
      - Total: 2,521 lines (organized into focused modules)
    - **Core now contains only**:
      - Terminal I/O and raw mode management
      - Buffer and row data structures
      - Syntax highlighting infrastructure
      - Screen rendering with VT100 sequences
      - File I/O operations
      - Cursor movement primitives
      - Basic editing operations (insert/delete char, newline)
    - **Feature modules** (cleanly separated):
      - Language support (syntax definitions + dynamic registration)
      - Selection and clipboard (OSC 52 protocol)
      - Search functionality (incremental find)
      - Modal editing (vim-like modes and keybindings)
  - **Benefits**:
    - **Maintainability**: Each module has single, well-defined responsibility
    - **Testability**: Features can be tested in isolation
    - **Extensibility**: New features don't bloat core
    - **Clarity**: Core editor logic no longer mixed with feature implementations
    - **Modularity**: Features can be enhanced independently without touching core
  - **Files Modified**:
    - Added: `src/loki_modal.c`, `src/loki_modal.h`, `src/loki_selection.c`, `src/loki_selection.h`, `src/loki_search.c`, `src/loki_search.h`
    - Modified: `src/loki_core.c`, `src/loki_internal.h`, `src/loki_languages.c`, `src/loki_languages.h`, `CMakeLists.txt`
    - All tests passing (2/2), clean compilation

## [0.4.4]

### Changed

- **Language Module Extraction**: Moved markdown-specific syntax highlighting from core to languages module
  - **Functions Moved**:
    - `highlight_code_line()` (~73 lines) - Highlights code blocks within markdown with language-specific rules
    - `editor_update_syntax_markdown()` (~173 lines) - Complete markdown syntax highlighting (headers, lists, bold, italic, inline code, links, code fences)
  - **Module Organization**:
    - Added function declarations to `src/loki_languages.h`
    - Added required includes to `loki_languages.c`: `<stdlib.h>`, `<string.h>`, `<ctype.h>`
    - Added `is_separator()` declaration to `src/loki_internal.h` for shared utility access
    - `loki_languages.c` now contains **all** language-specific code (417 lines total):
      - Language definitions (C, C++, Python, Lua, Cython, Markdown)
      - Keyword arrays and file extension mappings
      - Complete syntax highlighting logic
  - **Results**:
    - **252 lines removed from loki_core.c** (from ~2,245 to 1,993 lines)
    - Core now focuses exclusively on editor functionality (file I/O, cursor movement, rendering, input handling)
    - All language-specific code properly isolated in dedicated module
    - All tests passing (2/2), clean compilation
  - **Architecture Benefits**:
    - **Core remains minimal and maintainable** - language support doesn't bloat core
    - **Modular language support** - easy to add new languages without touching core
    - **Clear separation of concerns** - editor logic vs. language-specific highlighting
    - Adding new languages only requires changes to `loki_languages.c` and `loki_languages.h`

## [0.4.3]

### Changed

- **Context Passing Migration (Phase 6)**: Complete removal of global E singleton - multi-window support now possible
  - **Status Message Migration**:
    - Updated `editor_set_status_msg(ctx, fmt, ...)` to accept context parameter (breaking API change)
    - Updated ~35 call sites across loki_core.c (16), loki_lua.c (8), loki_editor.c (3)
    - Added context retrieval in Lua C API functions (`lua_loki_status`, `lua_loki_async_http`)
    - Fixed `loki_lua_status_reporter` to receive context via userdata parameter
  - **Exit Cleanup Refactor**:
    - Added static `editor_for_atexit` pointer set by `init_editor(ctx)`
    - Updated `editor_atexit()` to use static pointer instead of global E
    - Ensures proper cleanup without global dependency
  - **Global E Elimination**:
    - Moved `editor_ctx_t E` from global in loki_core.c to static in loki_editor.c
    - Removed `extern editor_ctx_t E;` declaration from loki_internal.h
    - Global E now completely eliminated - only one static instance in main()
  - **Typedef Warning Fix**:
    - Removed duplicate `typedef ... editor_ctx_t` from loki_internal.h
    - Moved typedef to public API header (include/loki/core.h) with forward declaration
    - Eliminated C11 typedef redefinition warning
  - **Public API Changes**:
    - `editor_set_status_msg(editor_ctx_t *ctx, const char *fmt, ...)` - breaking change in include/loki/core.h
    - Status messages now per-context, enabling independent status bars per window
  - **Results**:
    - **Zero global E references remaining** - complete elimination of singleton pattern
    - All functions use explicit context passing without exception
    - 6 files modified: loki_core.c, loki_editor.c, loki_lua.c, include/loki/core.h, src/loki_internal.h
    - All tests passing (2/2), clean compilation (only unused function warnings)
    - Global E moved to static in loki_editor.c:64 (only accessible to main())
  - **Architecture Milestone**:
    - **Multiple independent editor instances now architecturally possible**
    - No shared global state between editor contexts
    - Each context has independent: status messages, cursor position, buffers, syntax highlighting, REPL state
    - Foundation complete for implementing split windows, tabs, and multi-buffer editing
    - Example future usage:
      ```c
      editor_ctx_t window1, window2, window3;
      init_editor(&window1); init_editor(&window2); init_editor(&window3);
      editor_set_status_msg(&window1, "Window 1");  // Independent status
      editor_refresh_screen(&window2);              // Independent rendering
      ```
## [0.4.2]

### Added
- **Lua REPL Panel**: `Ctrl-L` toggles a console that hides when idle and uses a `>>` prompt
- **Lua REPL Helpers**: Built-in commands (help/history/clear) and the `loki.repl.register` hook
- **Documentation**: New `docs/REPL_EXTENSION.md` covering REPL customization strategies
- **Modular Targets**: New `libloki` library plus `loki-editor` and `loki-repl` executables built via CMake backend
- **REPL Enhancements**: `help` command now mirrors `:help` inside the standalone `loki-repl`
- **AI Namespace**: `ai.prompt(prompt[, opts])` wrapper exposed to Lua (editor + REPL) with sensible defaults and environment overrides
- **Readline Integration**: `loki-repl` uses GNU Readline/libedit when available (history, keybindings) and highlights commands on execution
- **Context Passing Infrastructure (Phase 1)**: Foundation for future split windows and multi-buffer support
  - New `editor_ctx_t` structure containing all editor state fields
  - Context management functions: `editor_ctx_init()`, `editor_ctx_from_global()`, `editor_ctx_to_global()`, `editor_ctx_free()`
  - Infrastructure allows creating independent editor contexts for split windows and tabs
  - Global singleton `E` retained temporarily for gradual migration
  - See `docs/remove_global.md` for complete migration plan and architecture

### Changed
- Makefile now wraps CMake (`build/` contains artifacts); editor binary renamed to `loki-editor`
- **Context Passing Migration (Phase 2)**: Migrated core editor functions to explicit context passing
  - Updated 25+ functions across `loki_core.c`, `loki_editor.c`, and `loki_lua.c`
  - Modified functions: `editor_move_cursor()`, `editor_find()`, `editor_refresh_screen()`, `init_editor()`, `update_window_size()`, `handle_windows_resize()`, `editor_select_syntax_highlight()`, `is_selected()`, `copy_selection_to_clipboard()`, and more
  - Replaced global `E` references with explicit `ctx->field` access in core functions
  - Updated public API in `include/loki/core.h` with breaking changes:
    - `editor_insert_char(ctx, c)` - now requires context parameter
    - `editor_del_char(ctx)` - now requires context parameter
    - `editor_insert_newline(ctx)` - now requires context parameter
    - `editor_save(ctx)` - now requires context parameter
    - `editor_open(ctx, filename)` - now requires context parameter
    - `editor_refresh_screen(ctx)` - now requires context parameter
    - `editor_select_syntax_highlight(ctx, filename)` - now requires context parameter
    - `init_editor(ctx)` - now requires context parameter
  - Reduced global E references by 65-75% across core modules (from ~421 to ~100-150)
  - All tests passing (2/2), compilation successful
- **Context Passing Migration (Phase 3)**: Migrated Lua integration to use context from registry
  - **Architecture**: Lua C API functions retrieve context from Lua registry (Phase 3 Option B)
  - **Implementation**:
    - Created `editor_ctx_registry_key` static variable as unique registry key
    - Implemented `lua_get_editor_context()` helper function for registry retrieval
    - Updated `loki_lua_bootstrap(ctx, opts)` to accept and store `editor_ctx_t *ctx` parameter
  - **Lua C API Functions** - Updated all 10 functions to use registry-based context:
    - `lua_loki_get_line()`, `lua_loki_get_lines()`, `lua_loki_get_cursor()`
    - `lua_loki_insert_text()`, `lua_loki_stream_text()`, `lua_loki_get_filename()`
    - `lua_loki_set_color()`, `lua_loki_set_theme()`
    - `lua_loki_get_mode()`, `lua_loki_set_mode()`
  - **Lua REPL Functions** - Updated to use explicit context passing:
    - `lua_repl_render(ctx, ab)`, `lua_repl_handle_keypress(ctx, key)`
    - `lua_repl_append_log(ctx, line)`, `editor_update_repl_layout(ctx)`
    - Internal helpers: `lua_repl_execute_current()`, `lua_repl_handle_builtin()`, `lua_repl_emit_registered_help()`, `lua_repl_push_history()`, `lua_repl_log_prefixed()`, `lua_repl_append_log_owned()`
  - **Public API Changes** in `include/loki/lua.h`:
    - `loki_lua_bootstrap(editor_ctx_t *ctx, const struct loki_lua_opts *opts)` - breaking change
    - Added forward declaration of `editor_ctx_t` (now includes `loki/core.h`)
  - **Call Site Updates**:
    - Updated `loki_editor.c`: 2 call sites to pass `&E` as context
    - Updated `main_repl.c`: 1 call site to pass `NULL` (standalone REPL)
    - Updated `loki_core.c`: All REPL function calls to pass context
  - **Results**:
    - Eliminated all global E references from `loki_lua.c` (0 remaining)
    - 6 files modified: +244 insertions, -187 deletions
    - All tests passing (2/2), compilation successful
    - Only harmless typedef redefinition warnings (C11 feature)
  - **Compatibility**: Architecture fully compatible with future opaque pointer conversion
  - **Backward Compatibility**: No breaking changes to Lua scripts (API unchanged from Lua perspective)
- **Context Passing Migration (Phase 4)**: Migrated loki_editor.c and async HTTP to explicit context passing
  - **Core Updates**:
    - Updated `check_async_requests(ctx, L)` to accept context for rawmode checking
    - Updated `loki_poll_async_http(ctx, L)` to accept context parameter
    - Updated `editor_cleanup_resources(ctx)` to accept context
    - Updated internal helpers: `exec_lua_command(ctx, fd)`, `lua_apply_span_table(ctx, row, table_index)`, `lua_apply_highlight_row(ctx, row, default_ran)`
  - **Public API Changes** in `include/loki/lua.h`:
    - `loki_poll_async_http(editor_ctx_t *ctx, lua_State *L)` - breaking change
  - **Standalone REPL Support**:
    - Updated `main_repl.c` to pass NULL context (standalone tools don't need editor context)
    - Added NULL-safe context checking pattern: `if (!ctx || !ctx->field)`
  - **Results**:
    - Reduced E. references in loki_editor.c from 78 to 5 (93% reduction)
    - Total E. references reduced from 198 to 125 across codebase
    - 6 files modified: +95/-91 lines
    - All tests passing (2/2), compilation successful
- **Context Passing Migration (Phase 5)**: Final cleanup - removed dead code and migrated remaining functions
  - **Code Cleanup**:
    - Deleted 146 lines of commented-out dead code (old `lua_apply_span_table` and `lua_apply_highlight_row` implementations)
    - Removed unused bridge functions: `editor_ctx_from_global()` and `editor_ctx_to_global()` (never called)
  - **Terminal Raw Mode**:
    - Updated `enable_raw_mode(ctx, fd)` to accept context parameter, uses `ctx->rawmode`
    - Updated `disable_raw_mode(ctx, fd)` to accept context parameter, uses `ctx->rawmode`
    - Updated call sites in loki_editor.c and loki_core.c
  - **Syntax Highlighting**:
    - Updated `editor_update_syntax_markdown(ctx, row)` to accept context parameter
    - Replaced `E.row` with `ctx->row` for accessing previous row's code block language
  - **Lua REPL**:
    - Updated `lua_repl_history_apply(ctx, repl)` to accept context parameter
    - Replaced `E.screencols` with `ctx->screencols` for input width calculations
  - **Documentation**:
    - Updated loki_internal.h to remove bridge function declarations
    - Enhanced global E comment to document current architecture and remaining use cases
  - **Results**:
    - Reduced E. references from 125 to 9 (93% reduction from Phase 4)
    - Total reduction: 412 references eliminated (97.8% reduction from start)
    - Only 9 E. references remain (all intentional: main(), atexit(), status messages)
    - 4 files modified: -204 lines in loki_core.c, +19/-7 lines across other files
    - All tests passing (2/2), compilation successful
  - **Architecture Achievement**:
    - Global singleton pattern effectively eliminated from active code paths
    - All core functions now use explicit context passing (`editor_ctx_t *ctx`)
    - Foundation complete for future split windows and multi-buffer support

## [0.4.1] - 2025-10-02

### Fixed
- **SSL/TLS Certificate Verification**: Fixed async HTTP requests timing out due to missing SSL configuration
  - Added proper CA bundle path (`/etc/ssl/cert.pem` for macOS)
  - Configured SSL peer and host verification
  - Increased timeout to 60 seconds with 10-second connection timeout
- **CURL Error Detection**: Properly detect and report CURL errors via `curl_multi_info_read()`
- **JSON Response Parsing**: Updated `.kilo/init.lua` to handle OpenAI's nested response format
  - Now parses `{"choices":[{"message":{"content":"..."}}]}`
  - Falls back to simple `{"content":"..."}` format
  - Detects and reports API errors from response
- **Non-Interactive Mode**: Terminal size defaults to 80x24 when screen query fails
- **Debug Output**: Enhanced error reporting in `--complete` and `--explain` modes
  - Shows HTTP status codes, response size, CURL errors
  - Validates content insertion before claiming success
  - Verbose mode now requires `KILO_DEBUG=1` environment variable (prevents API key leakage)

### Changed
- Increased HTTP timeout from 30 to 60 seconds
- Added 10-second connection timeout
- Verbose CURL output disabled by default (set `KILO_DEBUG=1` to enable)

## [0.4.0] - 2025-10-02

### Added
- **CLI Interface**: Comprehensive command-line argument parsing
  - `--help` / `-h` - Display usage information and available options
  - `--complete <file>` - Run AI completion on file in non-interactive mode and save result
  - `--explain <file>` - Run AI explanation on file and output to stdout
  - Support for creating new files (existing behavior now documented)
- **Non-Interactive Mode**: Execute AI commands from command line
  - Initializes editor without terminal raw mode
  - Waits for async HTTP requests to complete
  - Automatically saves results for --complete
  - Prints explanations to stdout for --explain
  - 60-second timeout with progress feedback
  - Comprehensive error handling and status messages

### Changed
- **Usage Model**: Now supports both interactive and CLI modes
  - Interactive: `kilo <filename>` (default, unchanged)
  - CLI: `kilo --complete <file>` or `kilo --explain <file>`
- **Help System**: Improved usage messages with detailed examples
- **Error Messages**: Enhanced feedback for missing API keys, Lua functions, and timeouts

### Documentation
- Updated README.md with CLI usage examples and requirements
- Added keybinding reference in help output
- Documented AI command prerequisites (OPENAI_API_KEY, init.lua)

### Technical Details
- Non-interactive mode tracks async request completion via `num_pending` counter
- Validates Lua function availability before execution
- Detects if async request was initiated to provide helpful error messages
- Uses `usleep(1000)` polling loop for async completion (1ms intervals)

## [0.3.0] - 2025-10-02

### Added
- **Async HTTP Support**: Non-blocking HTTP requests via libcurl multi interface
  - `kilo.async_http(url, method, body, headers, callback)` Lua API function
  - Up to 10 concurrent HTTP requests supported
  - 30-second timeout per request
  - Callback-based async pattern for response handling
  - Editor remains fully responsive during requests
- **AI Integration Examples**: Complete working examples in `.kilo.example/init.lua`
  - `ai_complete()` - Send buffer content to OpenAI/compatible APIs
  - `ai_explain()` - Get AI-powered code explanations
  - `test_http()` - Test async HTTP with GitHub API
  - Full JSON request/response handling examples
- **Homebrew Integration**: Automatic detection of system libraries
  - Auto-detects Lua or LuaJIT from Homebrew
  - Auto-detects libcurl from Homebrew
  - Prefers LuaJIT over Lua for better performance
  - `make show-config` target to display detected libraries
- **Example Configurations**: Enhanced `.kilo.example/` directory
  - Complete AI integration examples with OpenAI
  - Async HTTP usage examples
  - Updated README with setup instructions

### Changed
- **Build System**: Completely rewritten Makefile
  - Removed embedded Lua amalgamation approach
  - Now uses system Lua/LuaJIT via Homebrew (dynamic linking)
  - Reduced binary size from ~386KB to ~72KB
  - Added `HOMEBREW_PREFIX` detection
  - Simplified build process with automatic library detection
- **Lua Integration**: Switched from embedded to system Lua
  - Changed from `#include "lua.h"` to `#include <lua.h>` (system headers)
  - Removed `lua_one.c` amalgamation file (no longer needed)
  - Added `-lpthread` for curl multi-threading support
- **Main Event Loop**: Enhanced to support async operations
  - Added `check_async_requests()` call in main loop
  - Non-blocking request processing every iteration
  - Smooth integration with existing terminal I/O
- **Cleanup**: Added curl cleanup to `editor_atexit()`
  - Ensures proper curl_global_cleanup() on exit
  - Prevents memory leaks from pending requests

### Documentation
- Updated README.md with async HTTP features and AI integration examples
- Updated CLAUDE.md with complete `kilo.async_http()` API documentation
- Created `.kilo.example/README.md` with async HTTP setup guide
- Added AI integration workflow examples
- Documented non-blocking architecture and use cases

### Technical Details
- **Dependencies**: Now requires Lua/LuaJIT and libcurl from Homebrew
- **Architecture**: Uses libcurl multi interface for true async I/O
- **Event Loop**: Non-blocking, integrated with terminal input handling
- **Memory Management**: Proper cleanup of all async request structures
- **Error Handling**: User-friendly error messages in status bar

## [0.2.0] - 2025-10-02

### Added
- **Lua 5.4.7 Scripting**: Statically embedded Lua interpreter for extensibility and automation
  - Interactive Lua console accessible via `Ctrl-L` keybinding
  - Six API functions exposed via `kilo` global table:
    - `kilo.status(msg)` - Set status bar message
    - `kilo.get_lines()` - Get total line count
    - `kilo.get_line(row)` - Get line content (0-indexed)
    - `kilo.get_cursor()` - Get cursor position (row, col)
    - `kilo.insert_text(text)` - Insert text at cursor
    - `kilo.get_filename()` - Get current filename
  - Configuration file support with local override:
    - `.kilo/init.lua` (project-specific, highest priority)
    - `~/.kilo/init.lua` (global fallback)
  - Example configuration in `.kilo.example/` directory
  - Full Lua standard library available (io, os, math, string, table, etc.)
  - Error handling with user-friendly status bar messages
- Added Lua amalgamation source (`lua_one.c`) for single-file embedding
- Added example Lua functions: `count_lines()`, `show_cursor()`, `insert_timestamp()`, `first_line()`
- Added `.kilo.example/README.md` with complete Lua API documentation

### Changed
- Updated help message to include `Ctrl-L` Lua command keybinding
- Modified Makefile to compile Lua with `-lm -ldl` flags and POSIX support
- Increased binary size to ~386KB (from ~69KB) due to embedded Lua interpreter
- Extended `editorConfig` structure to include `lua_State *L` field
- Updated `.gitignore` to exclude Lua source directory, object files, and local `.kilo/` configs

### Documentation
- Added comprehensive Lua scripting section to CLAUDE.md
- Updated README.md with Lua features, API reference, and usage examples
- Created `.kilo.example/` with sample configuration and documentation

## [0.1.1]

### Security
- Fixed multiple buffer overflow vulnerabilities in syntax highlighting that could read/write beyond allocated buffers
- Fixed incorrect buffer size usage in comment highlighting preventing memory corruption
- Fixed keyword matching that could read past end of buffer

### Added
- Added binary file detection that refuses to open files with null bytes
- Added configurable separator list per syntax definition for better language support

### Fixed
- Fixed cursor position calculation bug causing incorrect cursor movement when wrapping from end of long lines
- Added NULL checks for all memory allocations to prevent crashes on allocation failure
- Fixed incomplete CRLF newline stripping for files with Windows line endings
- Fixed silent allocation failures in screen rendering that caused corrupted display
- Fixed infinite loop possibility in editorReadKey when stdin reaches EOF
- Fixed missing null terminator maintenance in character deletion operations
- Fixed integer overflow in row allocation for extremely large files
- Removed dead code in nonprint character calculation
- Fixed typos in comments: "commen" → "comment", "remainign" → "remaining", "escluding" → "excluding"
- Fixed typo in user-facing welcome message: "verison" → "version"
- Fixed number highlighting to only accept decimal points followed by digits (no trailing periods)

### Changed
- Refactored signal handler for window resize to be async-signal-safe (now uses flag instead of direct I/O)
- Moved atexit() registration to beginning of main() to ensure terminal cleanup on all exit paths
- Improved error messages for out-of-memory conditions
- Made separators configurable per syntax by adding field to editorSyntax structure
- Documented non-reentrant global state limitation in code comments


## [0.1.0] - Initial Release

- Project created
