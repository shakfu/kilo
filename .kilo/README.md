# Kilo Lua Configuration Example

This directory contains example Lua configuration for the kilo editor.

## Setup

To use this configuration:

```bash
# Copy to .kilo directory (project-specific)
cp -r .kilo.example .kilo

# Or copy to home directory (global)
mkdir -p ~/.kilo
cp .kilo.example/init.lua ~/.kilo/
```

## Configuration Priority

Kilo loads Lua configuration in this order:

1. `.kilo/init.lua` - Local, project-specific (current working directory)
2. `~/.kilo/init.lua` - Global, home directory

**Note:** If a local `.kilo/init.lua` exists, the global config is **NOT** loaded.

## Available Functions

See `init.lua` for example functions:

- `count_lines()` - Display total number of lines
- `show_cursor()` - Show current cursor position
- `insert_timestamp()` - Insert current date/time at cursor
- `first_line()` - Display content of first line

## Lua API

Access editor functionality via the `kilo` global table:

- `kilo.status(msg)` - Set status bar message
- `kilo.get_lines()` - Get total line count
- `kilo.get_line(row)` - Get line content (0-indexed)
- `kilo.get_cursor()` - Get cursor position (row, col)
- `kilo.insert_text(text)` - Insert text at cursor
- `kilo.get_filename()` - Get current filename

## Usage

Press `Ctrl-L` in kilo to open the Lua command prompt, then call any function:

```lua
count_lines()
insert_timestamp()
kilo.status("Hello, World!")
```
