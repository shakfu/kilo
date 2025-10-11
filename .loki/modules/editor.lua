-- Editor Utilities Module
-- Provides common editor operations and utilities

local M = {}

-- Count lines in buffer
function M.count_lines()
    local n = loki.get_lines()
    loki.status(string.format("File has %d lines", n))
end

-- Show cursor position
function M.cursor()
    local row, col = loki.get_cursor()
    loki.status(string.format("Cursor at row %d, col %d", row, col))
end

-- Insert current timestamp
function M.timestamp()
    local timestamp = os.date("%Y-%m-%d %H:%M:%S")
    loki.insert_text(timestamp)
    loki.status("Inserted timestamp")
end

-- Print first line
function M.first_line()
    local line = loki.get_line(0)
    if line then
        loki.status("First line: " .. line)
    else
        loki.status("No lines in file")
    end
end

-- Register help for REPL
if loki.repl and loki.repl.register then
    loki.repl.register("editor.count_lines", "Show line count")
    loki.repl.register("editor.cursor", "Show cursor position")
    loki.repl.register("editor.timestamp", "Insert current date/time")
    loki.repl.register("editor.first_line", "Display first line")
end

return M
