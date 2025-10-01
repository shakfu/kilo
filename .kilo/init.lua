-- Kilo Editor Lua Configuration Example
--
-- Copy this directory to .kilo/ to customize your editor:
--   cp -r .kilo.example .kilo
--
-- Loading priority:
--   1. .kilo/init.lua (local, project-specific)
--   2. ~/.kilo/init.lua (global, home directory)
--
-- If .kilo/init.lua exists, global config is NOT loaded.

-- Set a welcome message
kilo.status("Lua scripting enabled! Press Ctrl-L to run commands.")

-- Example function: count lines
function count_lines()
    local n = kilo.get_lines()
    kilo.status(string.format("File has %d lines", n))
end

-- Example function: show cursor position
function show_cursor()
    local row, col = kilo.get_cursor()
    kilo.status(string.format("Cursor at row %d, col %d", row, col))
end

-- Example function: insert current timestamp
function insert_timestamp()
    local timestamp = os.date("%Y-%m-%d %H:%M:%S")
    kilo.insert_text(timestamp)
    kilo.status("Inserted timestamp")
end

-- Example function: print first line
function first_line()
    local line = kilo.get_line(0)
    if line then
        kilo.status("First line: " .. line)
    else
        kilo.status("No lines in file")
    end
end

print("Kilo Lua init.lua loaded successfully")
