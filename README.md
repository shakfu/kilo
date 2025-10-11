# loki -- a minimal lua-powered text editor with builtin async http

A lightweight, Lua-powered text editor built on antirez's [kilo](https://github.com/antirez/kilo) (~1K lines). Features async HTTP for AI completions, lua scripting, and zero configuration needed - just compile and run.

## Features

### Core Editor
- **Minimalist**: ~1.5K lines of C99 code
- **Fast**: Direct VT100 escape sequences, no curses overhead
- **Robust**: All critical bugs fixed (buffer overflows, NULL checks, signal safety)
- **Safe**: Binary file detection, proper error handling
- **Syntax highlighting**: C/C++ built-in, extensible via Lua

### Lua Scripting Engine
- **Lua/LuaJIT**: Full Lua environment for extensibility
- **Project-local config**: `.loki/init.lua` overrides `~/.loki/init.lua`
- **Interactive console**: Toggle the collapsible Lua REPL with `Ctrl-L`
- **Built-in helpers**: `help`, `history`, `clear`, `clear-history`, `exit`
- **Full standard library**: io, os, math, string, table, etc.

### Async HTTP Integration
- **Non-blocking requests**: Editor stays responsive during API calls
- **AI-ready**: OpenAI, Anthropic Claude, local LLM integration
- **Multi-concurrent**: Up to 10 simultaneous requests
- **Callback-based**: Clean async pattern with Lua callbacks
- **libcurl-powered**: Reliable, battle-tested HTTP client

### Smart Configuration
- **Auto-detection**: Finds Homebrew Lua/LuaJIT and libcurl automatically
- **Local override**: Project-specific `.loki/` config takes precedence
- **Zero-config**: Works out of the box with sensible defaults
- **Example configs**: Complete AI integration examples included

## Building

### Install Dependencies (macOS)

```bash
brew install lua curl  # or: brew install luajit curl
# Optional: brew install readline  # enables enhanced CLI history/highlighting
```

### Compile

```bash
# Build the editor (build/loki-editor)
make editor

# Build the REPL (build/loki-repl)
make repl

# Build everything (library + both binaries)
make all
```

The Makefile is a thin wrapper over CMake and drops artifacts under `build/`. Use `make lib` if you only need `libloki`.

Prefer direct CMake invocations? Run `cmake -S . -B build` once, then `cmake --build build --target <target>`. Available targets match the Makefile aliases (`libloki`, `loki-editor`, `loki-repl`, `show-config`).

Requires: C99 compiler, POSIX system (Linux, macOS, BSD)

## Usage

### Interactive Mode

```bash
./build/loki-editor <filename>
```

Opens the file in the interactive editor.

### CLI Mode (AI Commands)

```bash
# Run AI completion on a file and save the result
./build/loki-editor --complete <filename>

# Run AI explanation on a file and print to stdout
./build/loki-editor --explain <filename>

# Show help
./build/loki-editor --help
```

**Requirements for AI commands:**
- Set `OPENAI_API_KEY` environment variable
- Configure `.loki/init.lua` or `~/.loki/init.lua` with AI functions (see `.loki.example/`)

### Standalone REPL

```bash
./build/loki-repl            # Interactive shell
./build/loki-repl script.lua # Execute a Lua script and exit
```

- Shares the same Lua bootstrap/config loader as the editor.
- Use `--trace-http` to mirror `KILO_DEBUG` logging for async HTTP calls.
- Command history is persisted to `.loki/repl_history` when available.
- Type `help` (or `:help`) to list built-in commands.
- Uses GNU Readline/libedit when available (history, emacs keybindings, inline syntax colour); falls back to a minimal line editor otherwise.
- Lines are re-rendered with Lua-aware syntax highlighting as you execute them.

The REPL loads the same Lua modules as the editor and exposes a higher-level namespace:

```lua
ai.prompt "call the project bot" -- sends an async HTTP request via loki.async_http

-- Provide explicit options
ai.prompt("summarise README", {
  model = "gpt-5-nano",
  callback = "ai_response_handler", -- Lua function name
  url = os.getenv("LOKI_AI_URL"),
})

-- Use the editor namespace helpers
editor.status.set("hello from the repl")
print(editor.buffer.line_count())
```

### Keybindings (Interactive Mode)

- `CTRL-S`: Save file
- `CTRL-Q`: Quit (with unsaved changes warning)
- `CTRL-F`: Find string in file (ESC to exit, arrows to navigate)
- `CTRL-L`: Toggle Lua REPL (collapsed by default, type `help` for commands)

## Lua Scripting

Loki includes an embedded Lua interpreter for customization and automation.

### Configuration

Loki loads Lua configuration with local override support:

1. `.loki/init.lua` - Project-specific config (current directory)
2. `~/.loki/init.lua` - Global config (home directory)

If a local `.loki/init.lua` exists, the global config is **not** loaded.

**Quick start:**
```bash
# Copy example configuration
cp -r .loki.example .loki

# Edit to customize
vim .loki/init.lua
```

### Interactive Lua Console

Press `Ctrl-L` to toggle the Lua REPL docked below the status bar. The panel stays hidden when idle, so the editor keeps its full height until you need it.

- Type any expression at the `>> ` prompt and press `Enter` to evaluate it.
- Results (or errors) stream into the log above the prompt; `clear` wipes the log.
- Use `Up`/`Down` to browse command history, `Ctrl-U` to clear the current line, and `clear-history` to drop past entries.
- Built-in commands:`help`, `history`, `clear`, `clear-history`, `exit`.
- Press `Esc`, `Ctrl-C`, `Ctrl-L`, or type `exit` to return to normal editing.

```lua
>> count_lines()              -- Show line count
= 421
>> insert_timestamp()         -- Insert current date/time
>> loki.status("Hello!")      -- Set status message
```

#### Extending the REPL

- Call `loki.repl.register(name, description[, example])` inside `.loki/init.lua` to surface project-specific helpers in the REPL `help` output.
- See `docs/REPL_EXTENSION.md` for practical recipes and integration patterns.

### Lua API

The `loki` global table provides these functions:

**Synchronous Functions:**
- `loki.status(msg)` - Set status bar message
- `loki.get_lines()` - Get total number of lines
- `loki.get_line(row)` - Get line content (0-indexed)
- `loki.get_cursor()` - Get cursor position (row, col)
- `loki.insert_text(text)` - Insert text at cursor
- `loki.get_filename()` - Get current filename

**Async HTTP:**
- `loki.async_http(url, method, body, headers, callback)` - Non-blocking HTTP requests

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
    loki.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        {"Content-Type: application/json", "Authorization: Bearer ..."},
        "ai_response_handler"
    )
end

function ai_response_handler(response)
    -- Called automatically when response arrives
    loki.insert_text(parse_response(response))
end
```

See `.loki.example/init.lua` for complete examples including AI integration.

### Highlight Hook

Define `loki.highlight_row(row_index, line_text, render_text, syntax_type, default_applied)`
inside `.loki/init.lua` (or `~/.loki/init.lua`) to extend or replace syntax
highlighting from Lua. Return `nil` to keep the built-in rules, or return a
table of span descriptors to colour specific regions:

```lua
function loki.highlight_row(idx, text, render, syntax_type, default_applied)
    return {
        replace = true, -- optional: clear existing highlight first
        spans = {
            { start = 1, length = 3, style = "keyword1" },
            { start = 10, stop = 20, style = "comment" },
        },
    }
end
```

Style strings map to constants exposed under `loki.hl` (`match`, `string`,
`keyword1`, etc.). To layer extra cues without discarding the defaults, omit
`replace` and simply return `{ spans = {...} }`.

## Project Status

This is a fork with enhancements:
- [x] All critical bugs fixed (buffer overflows, NULL checks, signal safety)
- [x] Lua/LuaJIT scripting (via Homebrew, dynamically linked)
- [x] Async HTTP support (non-blocking, libcurl-based)
- [x] AI integration examples (OpenAI, compatible APIs)
- [x] Project-local configuration (`.loki/` override)
- [x] Binary file protection
- [x] Improved error handling

**Dependencies (via Homebrew):**
- Lua or LuaJIT
- libcurl

**Binary size:** ~72KB (dynamically linked)


## Credits

Original loki editor by Salvatore Sanfilippo (antirez).
Lua integration and enhancements added 2025.

Released under the BSD 2 clause license.
