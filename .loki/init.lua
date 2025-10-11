-- ==============================================================================
-- Loki Editor Configuration
-- ==============================================================================
--
-- This is the main configuration file for Loki editor.
-- It loads modular components and sets up your editing environment.
--
-- Loading priority:
--   1. .loki/init.lua (local, project-specific)
--   2. ~/.loki/init.lua (global, home directory)
--
-- If .loki/init.lua exists, the global config is NOT loaded.
--
-- ==============================================================================

-- Detect execution mode: "editor" or "repl"
MODE = loki.get_lines and "editor" or "repl"

-- ==============================================================================
-- Configure Module Path
-- ==============================================================================

-- Add .loki/modules to package.path for require()
package.path = package.path .. ";.loki/modules/?.lua"

-- Also add global config path if available
local home = os.getenv("HOME")
if home then
    package.path = package.path .. ";" .. home .. "/.loki/modules/?.lua"
end

-- ==============================================================================
-- Load Core Modules
-- ==============================================================================

-- Load language definitions
local languages = require("languages")
local lang_count = languages.load_all()

-- Load markdown and TODO highlighting
local markdown = require("markdown")
markdown.install()

-- Load theme utilities
theme = require("theme")

-- ==============================================================================
-- Load Feature Modules (only in editor mode)
-- ==============================================================================

if MODE == "editor" then
    -- Editor utilities
    editor = require("editor")

    -- Backward compatibility aliases (deprecated)
    count_lines = editor.count_lines
    show_cursor = editor.cursor
    insert_timestamp = editor.timestamp
    first_line = editor.first_line
end

-- AI functions (available in both editor and REPL)
ai = require("ai")

-- Backward compatibility aliases (deprecated)
ai_complete = ai.complete
ai_explain = ai.explain

-- Test/demo functions
test = require("test")

-- Backward compatibility alias (deprecated)
test_http = test.http

-- ==============================================================================
-- Color Theme Configuration
-- ==============================================================================

-- Uncomment one of these to load a pre-made theme (only works in editor mode):
if MODE == "editor" then
    -- theme.load("dracula")
    -- theme.load("monokai")
    -- theme.load("nord")
    -- theme.load("github-light")
end

-- Or define your own custom colors:
-- loki.set_theme({
--     normal = {r=200, g=200, b=200},
--     comment = {r=100, g=150, b=100},
--     keyword1 = {r=220, g=100, b=220},
--     keyword2 = {r=100, g=220, b=220},
--     string = {r=220, g=220, b=100},
--     number = {r=200, g=100, b=200},
--     match = {r=100, g=150, b=220}
-- })

-- ==============================================================================
-- Startup Message
-- ==============================================================================

if lang_count > 0 then
    loki.status(string.format("Loki initialized! Loaded %d language(s).", lang_count))
else
    loki.status("Loki initialized! Press Ctrl-L to run commands.")
end

print("Loki configuration loaded successfully")
print("Available modules: editor, ai, test, theme, languages, markdown")
print("")
print("Type :help in REPL for available commands")
