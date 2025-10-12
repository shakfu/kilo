# Language Registration API Design

## Overview

This document describes the Lua-based language registration API for loki, implementing Option 4 from SYNTAX_LSP_RESEARCH.md.

## Design Goals

1. **Simple**: Easy to add new languages without C knowledge
2. **Flexible**: Support all existing syntax highlighting features
3. **Backward compatible**: Built-in languages (C, Python, Lua, etc.) continue to work
4. **Lua-native**: Use Lua tables for configuration
5. **Minimal code**: Keep implementation under 300 lines

## Lua API

### Function: `loki.register_language(config)`

Registers a new language for syntax highlighting.

**Parameters:**

- `config` (table): Language configuration with the following fields:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Display name (e.g., "Python") |
| `extensions` | table | Yes | File extensions (e.g., `{".py", ".pyw"}`) |
| `keywords` | table | No | Language keywords |
| `types` | table | No | Type keywords (highlighted differently) |
| `line_comment` | string | No | Single-line comment start (e.g., `"#"`) |
| `block_comment_start` | string | No | Multi-line comment start (e.g., `"/*"`) |
| `block_comment_end` | string | No | Multi-line comment end (e.g., `"*/"`) |
| `separators` | string | No | Separator characters (e.g., `",.()+-/*"`) |
| `highlight_strings` | boolean | No | Enable string highlighting (default: true) |
| `highlight_numbers` | boolean | No | Enable number highlighting (default: true) |

**Returns:**

- `true` on success
- `nil, error_message` on failure

**Example:**

```lua
loki.register_language({
    name = "Python",
    extensions = {".py", ".pyw"},
    keywords = {
        "def", "class", "import", "if", "else", "elif", "for", "while",
        "return", "pass", "break", "continue", "try", "except", "finally",
        "with", "as", "lambda", "yield", "async", "await"
    },
    types = {
        "int", "str", "list", "dict", "tuple", "set", "bool", "float"
    },
    line_comment = "#",
    block_comment_start = '"""',
    block_comment_end = '"""',
    separators = ",.()+-/*=~%[]{}:",
    highlight_strings = true,
    highlight_numbers = true
})
```

### Function: `loki.list_languages()`

Returns a list of all registered languages.

**Returns:**

- Table of language names (e.g., `{"C", "Python", "Lua", "Markdown"}`)

**Example:**

```lua
local langs = loki.list_languages()
for _, lang in ipairs(langs) do
    print(lang)
end
```

## Directory Structure

Language definitions can be stored in `.loki/languages/`:

```text
.loki/
├── init.lua                    # Main config (loads languages)
└── languages/
    ├── python.lua              # Python language definition
    ├── javascript.lua          # JavaScript language definition
    ├── rust.lua                # Rust language definition
    └── ...
```

**Example language file** (`.loki/languages/python.lua`):

```lua
return loki.register_language({
    name = "Python",
    extensions = {".py", ".pyw"},
    keywords = {
        "def", "class", "import", "if", "else", "elif", "for", "while",
        "return", "pass", "break", "continue", "try", "except", "finally",
        "with", "as", "lambda", "yield", "async", "await", "in", "is", "not",
        "and", "or", "del", "from", "global", "nonlocal", "assert", "raise"
    },
    types = {
        "int", "str", "list", "dict", "tuple", "set", "bool", "float",
        "bytes", "bytearray", "complex", "frozenset", "object", "None",
        "True", "False"
    },
    line_comment = "#",
    block_comment_start = "",  -- Python doesn't have true block comments
    block_comment_end = "",    -- Triple-quoted strings are not comments
    separators = ",.()+-/*=~%[]{}:;",
    highlight_strings = true,
    highlight_numbers = true
})
```

**Loading in init.lua:**

```lua
-- Auto-load all language definitions
local lang_dir = ".loki/languages"
local handle = io.popen("ls " .. lang_dir .. "/*.lua 2>/dev/null")
if handle then
    for file in handle:lines() do
        dofile(file)
    end
    handle:close()
end
```

## C Implementation Details

### Memory Management

**Current approach**: Static `HLDB[]` array with fixed entries.

**New approach**:

1. Keep static HLDB for built-in languages
2. Add dynamic `HLDB_dynamic[]` array that grows as needed
3. `editor_select_syntax_highlight()` checks both arrays

**Alternative**: Convert entire HLDB to dynamic allocation (more invasive).

### Data Structure Changes

**Option A: Minimal changes** (recommended)

- Keep existing `struct t_editor_syntax`
- Allocate copies of strings passed from Lua
- Store in separate dynamic array

**Option B: Unified dynamic array**

- Convert HLDB to `struct t_editor_syntax **HLDB`
- Allocate all entries dynamically
- More flexible but requires refactoring

### Keyword Storage

Keywords are currently stored as:

```c
char *C_HL_keywords[] = {
    "if", "else", "while", ..., "int|", "long|", ..., NULL
};
```

**Type keywords** end with `|` to distinguish them from regular keywords.

**New approach**:

- Allocate keyword array dynamically
- Copy strings from Lua table
- Append `|` to type keywords
- Ensure NULL terminator

### String Lifecycle

1. Lua calls `loki.register_language({...})`
2. C function extracts strings from Lua table
3. C allocates memory and copies strings (via `strdup()`)
4. Strings remain valid for lifetime of editor
5. Cleanup on exit via `atexit()` handler

## Implementation Plan

### Phase 1: C Infrastructure (150 lines)

1. Add `HLDB_dynamic` array with realloc logic
2. Implement `loki_register_language()` C function
3. Modify `editor_select_syntax_highlight()` to check both arrays
4. Add cleanup function for dynamic allocations

### Phase 2: Lua Binding (50 lines)

1. Register `loki.register_language` function
2. Add Lua table parsing logic
3. Validate input and provide error messages
4. Add `loki.list_languages()` helper

### Phase 3: Port Existing Languages (100 lines Lua)

1. Create `.loki/languages/` directory
2. Port C/Python/Lua/Cython to Lua format
3. Update `init.lua` to auto-load language files
4. Test with existing files

### Phase 4: Documentation (this file + CLAUDE.md update)

## Estimated Impact

- **C code added**: ~200 lines
- **Lua code added**: ~150 lines (languages + init)
- **Binary size**: +2-3 KB (minimal, just array management)
- **Extensibility**: Users can add unlimited languages via Lua

## Example User Workflow

1. User wants to add Go syntax highlighting
2. Creates `.loki/languages/go.lua`:

```lua
return loki.register_language({
    name = "Go",
    extensions = {".go"},
    keywords = {
        "break", "case", "chan", "const", "continue", "default", "defer",
        "else", "fallthrough", "for", "func", "go", "goto", "if", "import",
        "interface", "map", "package", "range", "return", "select", "struct",
        "switch", "type", "var"
    },
    types = {
        "bool", "byte", "complex64", "complex128", "error", "float32",
        "float64", "int", "int8", "int16", "int32", "int64", "rune",
        "string", "uint", "uint8", "uint16", "uint32", "uint64", "uintptr"
    },
    line_comment = "//",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[]{}:;",
    highlight_strings = true,
    highlight_numbers = true
})
```

1. Restart loki or reload config
2. Go files now have syntax highlighting

## Error Handling

**Validation checks:**

- `name` must be non-empty string
- `extensions` must be non-empty table
- Extension strings must start with `.`
- Comment delimiters must fit in fixed-size buffers
- Keywords/types must be strings

**Error messages:**

```lua
-- Missing required field
"Error: 'name' field is required"

-- Invalid extension format
"Error: extensions must start with '.', got 'py'"

-- Comment delimiter too long
"Error: block_comment_start exceeds maximum length (5 chars)"
```

## Future Enhancements

1. **Regex support**: Allow regex patterns for keywords (requires library)
2. **Language injection**: Highlight SQL in Python strings (complex)
3. **Semantic highlighting**: Variable/function coloring (requires LSP)
4. **Theme support**: User-defined color schemes
5. **Syntax testing**: Validate language definitions with test files

## Comparison with Tree-sitter

| Feature | Lua-based | Tree-sitter |
|---------|-----------|-------------|
| Code complexity | +200 lines | +500-1000 lines |
| Binary size | +2-3 KB | +300-500 KB |
| Accuracy | Regex-like (good) | AST-based (perfect) |
| Performance | O(n) per line | O(log n) incremental |
| Languages | Manual (10-20) | Auto (100+) |
| Dependencies | None | libtree-sitter + grammars |

**Verdict**: Lua-based approach aligns with loki's minimalist philosophy while providing practical multi-language support.

---

**Status**: Design complete, ready for implementation
