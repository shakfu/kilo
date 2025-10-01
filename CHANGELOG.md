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
