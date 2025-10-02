# kilo -- a minimal lua-powered text editor with builtin async http

A lightweight, Lua-powered text editor built on antirez's [kilo](https://github.com/antirez/kilo) (~1K lines). Features async HTTP for AI completions, dynamic scripting, and zero configuration needed - just compile and run.

## Features

### Core Editor
- **Minimalist**: ~1.5K lines of C99 code
- **Fast**: Direct VT100 escape sequences, no curses overhead
- **Robust**: All critical bugs fixed (buffer overflows, NULL checks, signal safety)
- **Safe**: Binary file detection, proper error handling
- **Syntax highlighting**: C/C++ built-in, extensible via Lua

### Lua Scripting Engine
- **Lua/LuaJIT**: Full Lua environment for extensibility
- **Project-local config**: `.kilo/init.lua` overrides `~/.kilo/init.lua`
- **Interactive console**: Press `Ctrl-L` to execute Lua commands
- **Full standard library**: io, os, math, string, table, etc.

### Async HTTP Integration
- **Non-blocking requests**: Editor stays responsive during API calls
- **AI-ready**: OpenAI, Anthropic Claude, local LLM integration
- **Multi-concurrent**: Up to 10 simultaneous requests
- **Callback-based**: Clean async pattern with Lua callbacks
- **libcurl-powered**: Reliable, battle-tested HTTP client

### Smart Configuration
- **Auto-detection**: Finds Homebrew Lua/LuaJIT and libcurl automatically
- **Local override**: Project-specific `.kilo/` config takes precedence
- **Zero-config**: Works out of the box with sensible defaults
- **Example configs**: Complete AI integration examples included

## Building

### Install Dependencies (macOS)

```bash
brew install lua curl  # or: brew install luajit curl
```

### Compile

```bash
make
```

The Makefile automatically detects Homebrew-installed Lua/LuaJIT and libcurl.

Requires: C99 compiler, POSIX system (Linux, macOS, BSD)

## Usage

### Interactive Mode

```bash
./kilo <filename>
```

Opens the file in the interactive editor.

### CLI Mode (AI Commands)

```bash
# Run AI completion on a file and save the result
./kilo --complete <filename>

# Run AI explanation on a file and print to stdout
./kilo --explain <filename>

# Show help
./kilo --help
```

**Requirements for AI commands:**
- Set `OPENAI_API_KEY` environment variable
- Configure `.kilo/init.lua` or `~/.kilo/init.lua` with AI functions (see `.kilo.example/`)

### Keybindings (Interactive Mode)

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

**Synchronous Functions:**
- `kilo.status(msg)` - Set status bar message
- `kilo.get_lines()` - Get total number of lines
- `kilo.get_line(row)` - Get line content (0-indexed)
- `kilo.get_cursor()` - Get cursor position (row, col)
- `kilo.insert_text(text)` - Insert text at cursor
- `kilo.get_filename()` - Get current filename

**Async HTTP:**
- `kilo.async_http(url, method, body, headers, callback)` - Non-blocking HTTP requests

The async HTTP function enables powerful integrations:
- AI completions (OpenAI, Claude, local models)
- Code formatting/linting services
- Real-time documentation lookup
- Git/GitHub API integration
- Any HTTP API without blocking the editor

Example usage:
```lua
function ai_complete()
    local text = get_buffer_text()
    kilo.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        {"Content-Type: application/json", "Authorization: Bearer ..."},
        "ai_response_handler"
    )
end

function ai_response_handler(response)
    -- Called automatically when response arrives
    kilo.insert_text(parse_response(response))
end
```

See `.kilo.example/init.lua` for complete examples including AI integration.

## Project Status

This is a fork with enhancements:
- [x] All critical bugs fixed (buffer overflows, NULL checks, signal safety)
- [x] Lua/LuaJIT scripting (via Homebrew, dynamically linked)
- [x] Async HTTP support (non-blocking, libcurl-based)
- [x] AI integration examples (OpenAI, compatible APIs)
- [x] Project-local configuration (`.kilo/` override)
- [x] Binary file protection
- [x] Improved error handling

**Dependencies (via Homebrew):**
- Lua or LuaJIT
- libcurl

**Binary size:** ~72KB (dynamically linked)


## Credits

Original kilo editor by Salvatore Sanfilippo (antirez).
Lua integration and enhancements added 2025.

Released under the BSD 2 clause license.
