-- Modal Editing Module for Loki
-- Implements vim-like modal editing with extensible command system

local M = {}

-- Initialize command registry
M.commands = {}

-- Register a normal mode command
function M.register_command(key, callback)
    if type(key) ~= "string" or #key == 0 then
        error("Command key must be a non-empty string")
    end
    if type(callback) ~= "function" then
        error("Command callback must be a function")
    end

    M.commands[key] = callback
    loki.register_command(key, callback)
end

-- Enable modal mode (sets to normal mode)
function M.enable()
    loki.set_mode("normal")
    loki.status("Modal editing enabled - NORMAL mode")
end

-- Disable modal mode (sets to insert mode)
function M.disable()
    loki.set_mode("insert")
    loki.status("Modal editing disabled - INSERT mode")
end

-- Get current mode
function M.get_mode()
    return loki.get_mode()
end

-- Set mode
function M.set_mode(mode)
    loki.set_mode(mode)
end

-- ============================================================================
-- Command Dispatcher (called from C for unknown keys in normal mode)
-- ============================================================================

function loki_process_normal_key(key)
    -- Convert key code to string if it's a printable character
    local key_str = nil
    if key >= 32 and key <= 126 then
        key_str = string.char(key)
    end

    -- Look up command in registry
    if key_str and M.commands[key_str] then
        local success, err = pcall(M.commands[key_str])
        if not success then
            loki.status("Command error: " .. tostring(err))
        end
        return true -- Handled
    end

    return false -- Not handled
end

-- ============================================================================
-- Built-in Commands
-- ============================================================================

-- Word motion forward
M.register_command("w", function()
    local row, col = loki.get_cursor()
    local line = loki.get_line(row)
    if not line then return end

    local pos = col + 1 -- Start after current position
    local len = #line

    -- Skip whitespace
    while pos <= len and line:sub(pos,pos):match("%s") do
        pos = pos + 1
    end

    -- Skip word characters
    while pos <= len and line:sub(pos,pos):match("[%w_]") do
        pos = pos + 1
    end

    -- Skip non-word, non-whitespace characters
    while pos <= len and not line:sub(pos,pos):match("[%w_%s]") do
        pos = pos + 1
    end

    if pos > len then
        -- Move to start of next line
        if row < loki.get_lines() - 1 then
            loki.set_mode("normal") -- Ensure we stay in normal mode
            -- Move down and to start of line
            -- (This is a workaround since we don't have direct cursor setting yet)
        end
    end
end)

-- Word motion backward
M.register_command("b", function()
    local row, col = loki.get_cursor()
    local line = loki.get_line(row)
    if not line or col == 0 then return end

    local pos = col -- Start at current position

    -- Move back one position
    pos = pos - 1
    if pos < 1 then return end

    -- Skip whitespace backwards
    while pos > 0 and line:sub(pos,pos):match("%s") do
        pos = pos - 1
    end

    -- Skip word characters backwards
    while pos > 0 and line:sub(pos,pos):match("[%w_]") do
        pos = pos - 1
    end

    -- Move forward one to land on start of word
    if pos < col - 1 then
        pos = pos + 1
    end
end)

-- Move to end of word
M.register_command("e", function()
    local row, col = loki.get_cursor()
    local line = loki.get_line(row)
    if not line then return end

    local pos = col + 1
    local len = #line

    -- Skip whitespace
    while pos <= len and line:sub(pos,pos):match("%s") do
        pos = pos + 1
    end

    -- Find end of word
    while pos <= len and line:sub(pos,pos):match("[%w_]") do
        pos = pos + 1
    end

    -- Move back one to land on last character of word
    if pos > col + 1 then
        pos = pos - 1
    end
end)

-- Move to start of line
M.register_command("0", function()
    -- Note: We can't directly set cursor position yet
    -- This would need to be implemented in C or via multiple arrow key movements
    loki.status("0 - move to start of line (not yet implemented)")
end)

-- Move to end of line
M.register_command("$", function()
    -- Note: We can't directly set cursor position yet
    loki.status("$ - move to end of line (not yet implemented)")
end)

-- Delete line
M.register_command("d", function()
    -- This is a two-character command (dd)
    -- For now, just show a message
    loki.status("Press 'd' again to delete line (not yet implemented)")
end)

-- Yank (copy) line
M.register_command("y", function()
    -- This is a two-character command (yy)
    loki.status("Press 'y' again to yank line (not yet implemented)")
end)

-- Paste
M.register_command("p", function()
    loki.status("Paste (not yet implemented - need clipboard integration)")
end)

-- Insert at start of line
M.register_command("I", function()
    -- Move to start of line and enter insert mode
    -- For now, just enter insert mode
    loki.set_mode("insert")
end)

-- Append at end of line
M.register_command("A", function()
    -- Move to end of line and enter insert mode
    -- For now, just enter insert mode
    loki.set_mode("insert")
end)

-- ============================================================================
-- Help and Status
-- ============================================================================

function M.show_help()
    local help = {
        "Modal Editing Commands:",
        "",
        "Normal Mode:",
        "  h/j/k/l - Move left/down/up/right",
        "  w/b/e   - Word motions (forward/backward/end)",
        "  {/}     - Previous/next empty line (paragraph motion)",
        "  0/$     - Start/end of line (WIP)",
        "  i       - Insert before cursor",
        "  a       - Insert after cursor",
        "  o/O     - Insert line below/above",
        "  v       - Enter visual mode",
        "  x       - Delete character",
        "",
        "Visual Mode:",
        "  h/j/k/l - Extend selection",
        "  y       - Yank (copy) selection",
        "  d/x     - Delete selection (WIP)",
        "  ESC     - Return to normal mode",
        "",
        "Insert Mode:",
        "  ESC     - Return to normal mode",
        "  (normal typing)",
        "",
        "Press ESC to close"
    }

    print(table.concat(help, "\n"))
end

M.help = M.show_help

-- ============================================================================
-- Register help for REPL
-- ============================================================================

if loki.repl and loki.repl.register then
    loki.repl.register("modal.enable", "Enable modal editing (normal mode)")
    loki.repl.register("modal.disable", "Disable modal editing (insert mode)")
    loki.repl.register("modal.get_mode", "Get current editor mode")
    loki.repl.register("modal.set_mode", "Set editor mode (normal/insert/visual)")
    loki.repl.register("modal.register_command", "Register a custom normal mode command")
    loki.repl.register("modal.help", "Show modal editing help")
end

return M
