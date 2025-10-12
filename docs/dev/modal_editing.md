# Modal Editing in Loki

Loki now supports vim-like modal editing with extensible commands through Lua.

## Overview

Modal editing allows you to have different "modes" where keys have different meanings:

- **NORMAL mode**: Navigate and manipulate text (vim's command mode)
- **INSERT mode**: Type text normally (like traditional editors)
- **VISUAL mode**: Select text visually
- **COMMAND mode**: Execute commands (future enhancement)

The mode is always displayed in the status bar at the bottom of the screen.

## Quick Start

### Default Behavior

By default, Loki starts in **NORMAL mode** (vim-like behavior). Keys navigate and execute commands rather than inserting text.

### Entering Insert Mode

Press `i` to start typing, or use any of these commands:

- `i` - Insert before cursor
- `a` - Insert after cursor
- `o` - Insert new line below
- `O` - Insert new line above

Press `ESC` to return to NORMAL mode.

### Disabling Modal Editing

If you prefer traditional editor behavior (always inserting text), you can start in INSERT mode:

```lua
-- In .loki/init.lua:
loki.set_mode("insert")  -- Start in insert mode instead

-- Or via Ctrl-L REPL:
modal.disable()
```

## Mode Reference

### NORMAL Mode

Navigate and execute commands without inserting text.

**Navigation:**

- `h` - Move left
- `j` - Move down
- `k` - Move up
- `l` - Move right
- Arrow keys also work

**Enter INSERT mode:**

- `i` - Insert before cursor
- `a` - Insert after cursor (append)
- `o` - Insert new line below
- `O` - Insert new line above
- `I` - Insert at start of line
- `A` - Insert at end of line

**Visual mode:**

- `v` - Enter VISUAL mode

**Edit commands:**

- `x` - Delete character under cursor

**Word motions (via Lua):**

- `w` - Next word
- `b` - Previous word
- `e` - End of word

**Paragraph motions:**

- `{` - Move to previous empty line (paragraph backward)
- `}` - Move to next empty line (paragraph forward)

**Global commands (work in all modes):**

- `Ctrl-S` - Save
- `Ctrl-F` - Find
- `Ctrl-Q` - Quit
- `Ctrl-L` - Lua REPL

### INSERT Mode

Type normally. All keys insert characters.

**Special keys:**

- `ESC` - Return to NORMAL mode
- `Enter` - New line
- `Backspace` / `Delete` - Delete character
- Arrow keys - Navigate
- `Ctrl-S` - Save
- `Ctrl-F` - Find
- `Ctrl-W` - Toggle word wrap
- `Ctrl-L` - Lua REPL
- `Ctrl-C` - Copy selection

### VISUAL Mode

Select text visually. Selection is highlighted.

**Movement (extends selection):**

- `h` / `j` / `k` / `l` - Extend selection
- Arrow keys also work

**Actions:**

- `y` - Yank (copy) selection to clipboard
- `d` or `x` - Delete selection (WIP)
- `ESC` - Return to NORMAL mode

**Global commands:**

- `Ctrl-C` - Copy selection

## Lua API

### Mode Control

```lua
-- Get current mode
local mode = loki.get_mode()  -- Returns: "normal", "insert", "visual", "command"

-- Set mode
loki.set_mode("normal")
loki.set_mode("insert")
loki.set_mode("visual")

-- Using modal module
modal.enable()   -- Switch to normal mode
modal.disable()  -- Switch to insert mode
modal.get_mode() -- Get current mode
modal.set_mode("normal") -- Set mode
```

### Custom Commands

Register custom normal mode commands:

```lua
-- Register a simple command
loki.register_command("g", function()
    loki.status("Custom 'g' command!")
end)

-- Or use modal module
modal.register_command("g", function()
    -- Your command implementation
    local row, col = loki.get_cursor()
    loki.status(string.format("Cursor at %d,%d", row, col))
end)
```

### Example: Custom Commands

```lua
-- .loki/init.lua or custom module

-- Go to line 1
modal.register_command("g", function()
    -- Move to first line (would need C support for direct positioning)
    loki.status("Go to first line")
end)

-- Save and quit
modal.register_command("Z", function()
    if loki.get_filename() then
        -- Save file
        loki.status("Saved!")
    end
end)

-- Insert current date
modal.register_command("D", function()
    loki.set_mode("insert")
    loki.insert_text(os.date("%Y-%m-%d"))
    loki.set_mode("normal")
end)
```

## Implementation Details

### C Level

**Core Files:**

- `src/loki_core.c` - Modal system implementation

**Key Components:**

1. **EditorMode enum**: Tracks current mode
2. **process_normal_mode()**: Handles normal mode keys
3. **process_insert_mode()**: Handles insert mode keys
4. **process_visual_mode()**: Handles visual mode keys
5. **Lua API functions**:
   - `loki.get_mode()` - Get current mode
   - `loki.set_mode(mode)` - Set mode
   - `loki.register_command(key, callback)` - Register command

**Dispatcher Pattern:**

```c
void editor_process_keypress(int fd) {
    int c = editor_read_key(fd);

    switch(E.mode) {
        case MODE_NORMAL:
            process_normal_mode(fd, c);
            break;
        case MODE_INSERT:
            process_insert_mode(fd, c);
            break;
        case MODE_VISUAL:
            process_visual_mode(fd, c);
            break;
    }
}
```

### Lua Level

**Modal Module:** `.loki/modules/modal.lua`

**Key Functions:**

- `loki_process_normal_key(key)` - Global function called from C for unknown keys
- `modal.register_command(key, callback)` - Register custom command
- `modal.enable()` / `modal.disable()` - Mode control
- `modal.help()` - Show help

**Command Registry:**
Commands are stored in Lua table `_loki_commands` and looked up when keys are pressed in normal mode.

## Extending the System

### Adding New Commands

Create a custom module:

```lua
-- .loki/modules/my_commands.lua
local M = {}

function M.install()
    -- Delete word command
    modal.register_command("w", function()
        -- Implementation
        loki.status("Delete word")
    end)

    -- Go to end of file
    modal.register_command("G", function()
        local lines = loki.get_lines()
        -- Would need cursor positioning API
        loki.status(string.format("Go to line %d", lines))
    end)

    -- Comment current line
    modal.register_command("c", function()
        local row = loki.get_cursor()
        local line = loki.get_line(row)
        -- Insert comment at start of line
        loki.status("Comment line")
    end)
end

return M
```

Load in `init.lua`:

```lua
local my_cmds = require("my_commands")
my_cmds.install()
```

### Multi-Character Commands

For commands like `dd` (delete line), `yy` (yank line), you need state tracking:

```lua
local pending_command = nil

modal.register_command("d", function()
    if pending_command == "d" then
        -- Execute dd (delete line)
        pending_command = nil
        loki.status("Delete line")
    else
        pending_command = "d"
        loki.status("d")  -- Show partial command
    end
end)
```

### Operator + Motion Pattern

For vim-style `d3w` (delete 3 words), implement an operator-pending mode:

```lua
local operator = nil
local count = ""

function handle_normal_key(key)
    if key >= "0" and key <= "9" then
        count = count .. key
        return
    end

    if key == "d" then
        operator = "delete"
        return
    end

    if key == "w" and operator then
        local n = tonumber(count) or 1
        -- Delete n words
        operator = nil
        count = ""
    end
end
```

## Current Limitations

These features are **not yet implemented** but could be added:

1. **No undo/redo** - Critical for modal editing
2. **No direct cursor positioning** - Can't implement `0`, `$`, `gg`, `G` properly
3. **No text objects** - Can't do `diw` (delete inner word), `ci"` (change inside quotes)
4. **No registers** - Only system clipboard for yank/paste
5. **No marks** - Can't mark positions and jump back
6. **No macros** - Can't record and replay sequences
7. **No search in normal mode** - `/` and `?` not implemented
8. **No command mode** - `:` commands not implemented

## Future Enhancements

### Near Term (Easy)

- Add more motions: `0`, `$`, `^`, `gg`, `G`
- Implement `dd`, `yy` properly
- Add `p` (paste) command
- Implement basic undo/redo

### Medium Term (Moderate Effort)

- Command mode (`:` commands)
- Search in normal mode (`/`, `?`)
- Text objects (`iw`, `i"`, `i(`, etc.)
- Marks (`m` to set, `'` to jump)
- Visual line mode (`V`)
- Visual block mode (`Ctrl-V`)

### Long Term (Significant Effort)

- Multiple registers (`"a`, `"b`, etc.)
- Macro recording (`q`, `@`)
- Operator composition (`d3w`, `c2j`, etc.)
- Count prefixes (`3j`, `5dd`, etc.)
- Dot repeat (`.` command)
- Full vim compatibility mode

## Design Philosophy

Loki's modal system is designed to be:

1. **Optional** - Start in insert mode by default, opt-in to modal
2. **Extensible** - Add commands via Lua without recompiling
3. **Minimal** - Core commands in C, extensions in Lua
4. **Compatible** - Vim users should feel comfortable
5. **Gradual** - Start simple, add features as needed

## Comparison with Vim

**What Loki has:**

- Basic modal editing (normal/insert/visual)
- hjkl navigation
- i/a/o/O insert commands
- Visual mode selection
- ESC to switch modes
- Extensible via Lua

**What Vim has that Loki doesn't:**

- Undo/redo
- Command mode (`:` commands)
- Text objects
- Registers
- Marks
- Macros
- Search (`/`, `?`)
- Much more...

Loki is **not trying to be Vim**. It's a lightweight editor with modal support for users who prefer that editing style.

## Troubleshooting

### Mode stuck or not switching

```lua
-- Force mode change via Lua REPL (Ctrl-L)
loki.set_mode("insert")  -- Go back to insert
loki.set_mode("normal")  -- Go to normal
```

### Command not working

```lua
-- Check if modal module loaded
if modal then
    print("Modal module loaded")
    print("Current mode:", modal.get_mode())
else
    print("Modal module not loaded!")
end

-- Check command registry
for k, v in pairs(_loki_commands or {}) do
    print("Command:", k)
end
```

### Status bar not showing mode

The mode should always show in the status bar. If not:

1. Check that Loki was compiled with the latest code
2. Verify the status bar is rendering
3. Try resizing the terminal

## Examples

### Basic Usage Session

```text
# Start editor (INSERT mode by default)
./loki-editor test.c

# Press ESC -> Switch to NORMAL mode
# Status bar shows: "NORMAL  test.c - 10 lines"

# Navigate with hjkl
h  # Move left
j  # Move down
k  # Move up
l  # Move right

# Enter insert mode
i  # Insert before cursor
# Type some text
ESC  # Back to normal

# Open new line
o  # Open line below, enter insert mode
# Type text
ESC  # Back to normal

# Visual mode
v  # Enter visual mode
jjj  # Extend selection down 3 lines
y  # Yank (copy) to clipboard
ESC  # Back to normal

# Save and quit
Ctrl-S  # Save
Ctrl-Q  # Quit
```

### Lua Customization

```lua
-- .loki/init.lua

-- Enable modal editing by default
modal.enable()

-- Add custom command to insert header
modal.register_command("H", function()
    loki.set_mode("insert")
    local header = string.format([[
/*
 * File: %s
 * Date: %s
 */

]], loki.get_filename() or "untitled", os.date("%Y-%m-%d"))
    loki.insert_text(header)
    loki.set_mode("normal")
end)

-- Save and quit command
modal.register_command("Z", function()
    -- Save if file has name
    if loki.get_filename() then
        loki.status("Saved!")
    end
end)
```

## See Also

- [CLAUDE.md](../CLAUDE.md) - Full Loki documentation
- [.loki/modules/modal.lua](../.loki/modules/modal.lua) - Modal module source
- [Lua API Documentation](lua_async_http.md) - Lua integration
