# REPL Line Editing Configuration

**Date:** 2025-10-11
**Feature:** Enhanced line editing support with editline and readline

## Overview

The loki-repl now has comprehensive line editing support with automatic detection of editline (libedit) or GNU readline libraries. This provides:
- **Command history** with up/down arrow navigation
- **Line editing** with cursor movement (left/right arrows, home/end)
- **History persistence** across sessions (saved to `.loki/repl_history`)
- **Tab completion** for Lua keywords, globals, and loki.* API functions
- **Multi-line input** with automatic detection of incomplete Lua code

## Library Priority

The build system tries libraries in this order:

1. **editline (libedit)** - Preferred (BSD license, more permissive)
2. **GNU readline** - Fallback (GPL license)
3. **Basic getline** - Minimal fallback if neither found

## Platform Support

### macOS
- **System editline**: Built-in (`/usr/lib/libedit.dylib`)
- **Homebrew readline**: `brew install readline`
- **Auto-detected**: Works out of the box on modern macOS

### Linux
- **Debian/Ubuntu**: `sudo apt-get install libedit-dev` or `libreadline-dev`
- **Fedora/RHEL**: `sudo dnf install libedit-devel` or `readline-devel`
- **Arch**: `sudo pacman -S libedit` or `readline`

### FreeBSD/OpenBSD
- **editline**: Usually pre-installed
- **readline**: `pkg install readline`

## Build Configuration

### Check Current Configuration

```bash
$ cmake --build build --target show-config
Lua include: /opt/homebrew/include/lua
Lua libraries: /opt/homebrew/lib/liblua5.4.dylib
libcurl include: ...
libcurl libraries: ...
Line editing: editline
Line edit library: /usr/lib/libedit.dylib
```

### Force Readline (if editline found but you want readline)

Currently, the CMakeLists.txt prefers editline. To force readline, you would need to edit CMakeLists.txt to check readline first, or uninstall/hide editline.

### Build Without Line Editing

If neither library is available, the build automatically falls back to basic `getline()` input:

```bash
Line editing library not found; loki-repl will use basic fallback (no history/completion)
  To enable features: brew install readline (or apt-get install libedit-dev on Linux)
```

## Features

### With editline/readline:

```bash
$ ./build/loki-repl
loki-repl 0.4.1 (Lua 5.4). Type :help for commands.
Line editing: editline (history + tab completion + multi-line enabled)
loki>
```

**Available features:**

**Navigation:**
- ⬆ **Up arrow**: Navigate to previous command
- ⬇ **Down arrow**: Navigate to next command
- ⬅ **Left arrow**: Move cursor left
-  **Right arrow**: Move cursor right
- **Home/End**: Jump to start/end of line
- **Ctrl-A**: Jump to start of line
- **Ctrl-E**: Jump to end of line

**Editing:**
- **Ctrl-K**: Delete from cursor to end
- **Ctrl-U**: Delete entire line
- **Ctrl-W**: Delete word backward
- **Ctrl-_**: Undo last edit

**History:**
- **Ctrl-R**: Reverse incremental search through history
- **Ctrl-D**: Exit REPL (EOF)

**Tab Completion:**
- **TAB**: Complete Lua keywords (if, then, function, etc.)
- **TAB**: Complete Lua globals from _G table (print, string, table, etc.)
- **TAB**: Complete loki.* API functions (loki.status, loki.get_lines, etc.)

**Multi-line Input:**
- Incomplete Lua code automatically triggers continuation prompt `cont>`
- Works for: functions, tables, control structures, incomplete expressions
- Example:
  ```lua
  loki> function factorial(n)
  cont>   if n <= 1 then return 1
  cont>   else return n * factorial(n-1)
  cont>   end
  cont> end
  loki> factorial(5)
  120
  ```

### Without line editing:

```bash
$ ./build/loki-repl
loki-repl 0.4.1 (Lua 5.4). Type :help for commands.
Line editing: basic (no history)
loki>
```

**Limited features:**
- Only basic line input
- No command history
- No cursor movement
- No line editing shortcuts

## History File

Command history is saved to `.loki/repl_history` in the current directory.

**Format:**
- editline uses `_HiStOrY_V2_` format
- readline uses its own format
- Both are human-readable

**Example:**
```
_HiStOrY_V2_
print('hello')
x = 42
print(x)
```

**Management:**
- Automatically loaded on REPL start
- Saved on clean exit (`:quit` or `quit`)
- Limited to 1000 entries (configurable in source)

## Implementation Details

### CMake Detection Logic

```cmake
# Try editline first (BSD license preferred)
find_library(EDITLINE_LIBRARY NAMES edit)

if (EDITLINE_FOUND)
    set(LINEEDIT_TYPE "editline")
else()
    # Try readline as fallback
    find_package(Readline)
    if (Readline_FOUND)
        set(LINEEDIT_TYPE "readline")
    endif()
endif()
```

### Source Code Integration

**Conditional compilation:**
```c
#if defined(LOKI_HAVE_EDITLINE)
    #include <editline/readline.h>  // Readline-compatible API
#elif defined(LOKI_HAVE_READLINE)
    #include <readline/readline.h>
    #include <readline/history.h>
#endif
```

**Runtime detection:**
```c
#ifdef LOKI_HAVE_LINEEDIT
    printf("Line editing: %s (history enabled)\n", "editline" or "readline");
#else
    printf("Line editing: basic (no history)\n");
#endif
```

## Troubleshooting

### Problem: "Line editing: basic (no history)"

**Solution:** Install a line editing library:

```bash
# macOS
brew install readline

# Debian/Ubuntu
sudo apt-get install libedit-dev

# Fedora/RHEL
sudo dnf install libedit-devel

# Arch
sudo pacman -S libedit
```

Then rebuild:
```bash
rm -rf build && make build
```

### Problem: History not saving

**Check:**
1. Does `.loki/` directory exist? (Create it: `mkdir -p .loki`)
2. Is `.loki/repl_history` writable?
3. Are you exiting with `:quit` or `quit`? (Ctrl-C doesn't save history)

### Problem: Arrows show escape codes

**Cause:** Terminal doesn't support editline/readline properly, or library not linked

**Solution:** Check linked libraries:
```bash
otool -L build/loki-repl | grep edit  # macOS
ldd build/loki-repl | grep edit        # Linux
```

If no editline/readline found, rebuild after installing.

## Comparison: editline vs readline

| Feature | editline (libedit) | GNU readline |
|---------|-------------------|--------------|
| **License** | BSD | GPL v3 |
| **Size** | ~200 KB | ~500 KB |
| **History** | Yes | Yes |
| **Completion** | Yes | Yes (more features) |
| **Vi/Emacs modes** | Yes | Yes |
| **macOS built-in** | Yes | No (need Homebrew) |
| **Linux repos** | Most distros | All distros |
| **API** | Readline-compatible | Native |

**Recommendation:** Use editline when available (better license, smaller).

## Future Enhancements

### Completed [x]
- [x] Tab completion for Lua globals
- [x] Tab completion for `loki.*` API functions
- [x] Multi-line editing for functions/tables
- [x] Tab completion for Lua keywords

### Planned
- [ ] Syntax-aware completion (table keys, methods)
- [ ] Vi mode toggle
- [ ] Smart indentation in multi-line mode

### Possible
- [ ] Completion from Lua documentation
- [ ] Completion from loaded modules
- [ ] Context-aware hints (like fish shell)
- [ ] Inline syntax highlighting (limited by terminal)
- [ ] Completion with function signatures/parameter hints

## Technical Notes

### Why editline is preferred

1. **License**: BSD is more permissive than GPL
2. **Size**: Smaller binary footprint
3. **macOS**: Built-in system library (no extra dependencies)
4. **Compatibility**: Provides readline-compatible API

### Code changes made

**Files modified:**
- `CMakeLists.txt`: Enhanced detection logic with editline priority
- `src/main_repl.c`: Added editline support, tab completion, multi-line input

**Lines changed:** ~280 lines total
- CMakeLists.txt: +65 lines (improved detection, status messages)
- main_repl.c: +215 lines (editline support, tab completion, multi-line input)

**New features implemented:**
1. **Tab completion system (lines 397-535)**:
   - `get_lua_completions()`: Queries Lua keywords, globals, and loki API
   - `completion_generator()`: State machine for readline completion
   - `loki_completion()`: Entry point for readline/editline
   - `repl_init_completion()`: Initialize completion system

2. **Multi-line input support (lines 189-209, 211-306)**:
   - `is_lua_complete()`: Checks if Lua code is syntactically complete
   - Modified main REPL loop with line buffering
   - Continuation prompt (`cont>`) for incomplete code
   - Proper handling of syntax errors vs incomplete code

3. **Enhanced help documentation** (lines 308-325):
   - Documents multi-line input feature
   - Documents tab completion
   - Documents readline shortcuts (Ctrl-R, Ctrl-_)

### Testing performed

```bash
# Build with editline
$ rm -rf build && make build
-- Found editline: /usr/lib/libedit.dylib
-- loki-repl will use editline for line editing (history, completion)

# Verify linkage
$ otool -L build/loki-repl | grep edit
	/usr/lib/libedit.3.dylib (compatibility version 2.0.0, current version 3.0.0)

# Test basic REPL
$ ./build/loki-repl
loki-repl 0.4.1 (Lua 5.4). Type :help for commands.
Line editing: editline (history + tab completion + multi-line enabled)
loki> print(1+1)
2
loki> quit

# Test multi-line input
$ ./build/loki-repl
loki> function factorial(n)
cont>   if n <= 1 then return 1
cont>   else return n * factorial(n-1)
cont>   end
cont> end
loki> factorial(5)
120
loki> quit

# Test multi-line table
$ ./build/loki-repl
loki> t = {
cont>   x = 1,
cont>   y = 2
cont> }
loki> print(t.x, t.y)
1	2
loki> quit

# Verify history (multi-line stored as single entry)
$ cat .loki/repl_history
_HiStOrY_V2_
print(1+1)
function factorial(n)\n  if n <= 1 then return 1\n  else return n * factorial(n-1)\n  end\nend
factorial(5)
t = {\n  x = 1,\n  y = 2\n}
print(t.x, t.y)
```

**Tab completion testing:**
- Type `fun` + TAB → completes to `function`
- Type `loki.` + TAB → shows loki.status, loki.get_lines, etc.
- Type `pri` + TAB → completes to `print`

[x] All tests passed

**Test results:**
- [x] Basic single-line evaluation
- [x] Multi-line functions with proper indentation
- [x] Multi-line tables
- [x] Incomplete expressions show `cont>` prompt
- [x] Tab completion for keywords
- [x] Tab completion for Lua globals
- [x] Tab completion for loki.* API
- [x] History persistence across sessions
- [x] Help documentation complete

---

## Quick Reference

### Installation Commands

| Platform | Editline | Readline |
|----------|----------|----------|
| **macOS** | (built-in) | `brew install readline` |
| **Debian/Ubuntu** | `apt-get install libedit-dev` | `apt-get install libreadline-dev` |
| **Fedora/RHEL** | `dnf install libedit-devel` | `dnf install readline-devel` |
| **Arch** | `pacman -S libedit` | `pacman -S readline` |
| **FreeBSD** | (built-in) | `pkg install readline` |

### Build Commands

```bash
# Clean rebuild
rm -rf build && make build

# Check configuration
cmake --build build --target show-config

# Test
make test
./build/loki-repl --version
```

### REPL Shortcuts

| Key | Action |
|-----|--------|
| ⬆ / ⬇ | Navigate history |
| ⬅ /  | Move cursor |
| **TAB** | Complete keywords/globals/loki.* API |
| **Ctrl-A** | Start of line |
| **Ctrl-E** | End of line |
| **Ctrl-K** | Delete to end |
| **Ctrl-U** | Delete line |
| **Ctrl-W** | Delete word back |
| **Ctrl-R** | Reverse search history |
| **Ctrl-_** | Undo last edit |
| **Ctrl-D** | Exit (EOF) |
| **Ctrl-C** | Cancel line |
| `:quit` | Exit REPL (saves history) |

---

**Status:** [x] COMPLETE AND TESTED

Line editing support is fully functional with automatic library detection and fallback support.
