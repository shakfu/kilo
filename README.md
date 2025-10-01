Kilo
===

Kilo is a small text editor with embedded Lua scripting, built on top of antirez's original kilo editor (< 1K lines). It uses VT100 escape sequences and has no external dependencies - Lua 5.4.7 is statically compiled into the binary.

A screencast of the original is available here: https://asciinema.org/a/90r2i9bq8po03nazhqtsifksb

## Features

- **Minimalist design**: Core editor in ~1.5K lines of C
- **Lua scripting**: Embedded Lua 5.4.7 for extensibility and automation
- **No dependencies**: Statically compiled, self-contained binary
- **Syntax highlighting**: C/C++ language support built-in
- **VT100 compatible**: Works in any terminal emulator
- **Project-local config**: `.kilo/init.lua` for project-specific customization

## Building

```bash
make
```

Requires: C99 compiler, POSIX system (Linux, macOS, BSD)

## Usage

```bash
./kilo <filename>
```

### Keybindings

- `CTRL-S`: Save file
- `CTRL-Q`: Quit (with unsaved changes warning)
- `CTRL-F`: Find string in file (ESC to exit, arrows to navigate)
- `CTRL-L`: Execute Lua command (interactive prompt)

## Lua Scripting

Kilo includes an embedded Lua interpreter for customization and automation.

### Configuration

Kilo loads Lua configuration with local override support:

1. `.kilo/init.lua` - Project-specific config (current directory)
2. `~/.kilo/init.lua` - Global config (home directory)

If a local `.kilo/init.lua` exists, the global config is **not** loaded.

**Quick start:**
```bash
# Copy example configuration
cp -r .kilo.example .kilo

# Edit to customize
vim .kilo/init.lua
```

### Interactive Lua Console

Press `Ctrl-L` to open the Lua command prompt. Type any Lua expression:

```lua
count_lines()              -- Show line count
insert_timestamp()         -- Insert current date/time
kilo.status("Hello!")      -- Set status message
```

### Lua API

The `kilo` global table provides these functions:

- `kilo.status(msg)` - Set status bar message
- `kilo.get_lines()` - Get total number of lines
- `kilo.get_line(row)` - Get line content (0-indexed)
- `kilo.get_cursor()` - Get cursor position (row, col)
- `kilo.insert_text(text)` - Insert text at cursor
- `kilo.get_filename()` - Get current filename

See `.kilo.example/init.lua` for complete examples.

## Project Status

This is a fork with enhancements:
- [x] All critical bugs fixed (buffer overflows, NULL checks, signal safety)
- [x] Lua 5.4.7 embedded scripting
- [x] Project-local configuration
- [x] Binary file protection
- [x] Improved error handling


## Credits

Original kilo editor by Salvatore Sanfilippo (antirez).
Lua integration and enhancements added 2025.

Released under the BSD 2 clause license.
