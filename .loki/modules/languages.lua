-- Language Loading Module
-- Auto-loads language definitions from .loki/languages/

local M = {}

-- Add languages directory to package.path
local function setup_language_path()
    local lang_paths = {
        ".loki/languages/?.lua",
        (os.getenv("HOME") or "") .. "/.loki/languages/?.lua"
    }

    for _, path in ipairs(lang_paths) do
        if not package.path:find(path, 1, true) then
            package.path = package.path .. ";" .. path
        end
    end
end

setup_language_path()

-- Load all language definition files from the languages directory
function M.load_all(lang_dir)
    lang_dir = lang_dir or ".loki/languages"
    local loaded_count = 0
    local MODE = loki.get_lines and "editor" or "repl"

    -- Get list of language files
    local handle = io.popen("ls " .. lang_dir .. "/*.lua 2>/dev/null")
    if handle then
        for filepath in handle:lines() do
            -- Extract module name from filepath (e.g., ".loki/languages/rust.lua" -> "rust")
            local module_name = filepath:match("([^/]+)%.lua$")

            if module_name then
                -- Clear from cache to allow reloading
                package.loaded[module_name] = nil

                local success, result = pcall(require, module_name)
                if success and result then
                    loaded_count = loaded_count + 1
                elseif not success then
                    -- Report error but continue loading other languages
                    if MODE == "editor" then
                        loki.status("Warning: Failed to load " .. module_name)
                    else
                        print("Warning: Failed to load " .. module_name .. ": " .. tostring(result))
                    end
                end
            end
        end
        handle:close()
    end

    return loaded_count
end

return M
