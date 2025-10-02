# Loki Lua Configuration Example

This directory contains example Lua configuration for the loki editor with async HTTP support.

## Setup

To use this configuration:

```bash
# Copy to .loki directory (project-specific)
cp -r .loki.example .loki

# Or copy to home directory (global)
mkdir -p ~/.loki
cp .loki.example/init.lua ~/.loki/
```

## Configuration Priority

Loki loads Lua configuration in this order:

1. `.loki/init.lua` - Local, project-specific (current working directory)
2. `~/.loki/init.lua` - Global, home directory

**Note:** If a local `.loki/init.lua` exists, the global config is **NOT** loaded.

## Available Functions

### Basic Functions

- `count_lines()` - Display total number of lines
- `show_cursor()` - Show current cursor position
- `insert_timestamp()` - Insert current date/time at cursor
- `first_line()` - Display content of first line

### Async HTTP Functions

- `test_http()` - Test async HTTP with GitHub API (simple GET request)
- `ai_complete()` - Send entire buffer to OpenAI and insert response
- `ai_explain()` - Get AI explanation of code in buffer

## AI Integration

The async HTTP API enables non-blocking AI completions:

### Prerequisites

Set your OpenAI API key as an environment variable:

```bash
export OPENAI_API_KEY="sk-..."
./loki myfile.txt
```

### Usage

1. Open a file with loki
2. Press `Ctrl-L` to open Lua command prompt
3. Type `ai_complete()` and press Enter
4. Continue editing - the response will appear automatically when ready
5. No blocking, no freezing!

### Example Workflow

```bash
# Write some text or code in loki
echo "Explain how async HTTP works" > question.txt
./loki question.txt

# In loki, press Ctrl-L and type:
ai_complete()

# Continue editing while AI processes
# Response appears automatically in buffer
```

## Lua API

Access editor functionality via the `loki` global table:

### Synchronous Functions

- `loki.status(msg)` - Set status bar message
- `loki.get_lines()` - Get total line count
- `loki.get_line(row)` - Get line content (0-indexed)
- `loki.get_cursor()` - Get cursor position (row, col)
- `loki.insert_text(text)` - Insert text at cursor
- `loki.get_filename()` - Get current filename

### Async HTTP Function

```lua
loki.async_http(url, method, body, headers, callback)
```

**Parameters:**
- `url` (string): The URL to request
- `method` (string): HTTP method ("GET", "POST", etc.)
- `body` (string or nil): Request body for POST requests
- `headers` (table): Array of header strings (e.g., `{"Content-Type: application/json"}`)
- `callback` (string): Name of Lua function to call with response

**Example:**
```lua
function my_callback(response)
    loki.status("Got: " .. response)
end

loki.async_http(
    "https://api.example.com/data",
    "GET",
    nil,
    {"User-Agent: loki"},
    "my_callback"
)
```

## Implementation Details

- **Non-blocking**: Uses libcurl multi interface for async I/O
- **Event loop**: Checks for completed requests every iteration
- **Callback-based**: Lua functions called when responses arrive
- **Max requests**: 10 concurrent requests (configurable in loki.c)
- **Timeout**: 30 seconds per request

## Customization

Edit `init.lua` to:
- Add custom keybindings
- Create project-specific functions
- Configure AI models or endpoints
- Add additional HTTP integrations

## Requirements

- Lua or LuaJIT (installed via Homebrew)
- libcurl (installed via Homebrew)
- OpenAI API key (for AI functions)

The binary is dynamically linked against system libraries, keeping it small (~72KB).
