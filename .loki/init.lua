-- Loki Editor Lua Configuration Example
--
-- Copy this directory to .loki/ to customize your editor:
--   cp -r .loki.example .loki
--
-- Loading priority:
--   1. .loki/init.lua (local, project-specific)
--   2. ~/.loki/init.lua (global, home directory)
--
-- If .loki/init.lua exists, global config is NOT loaded.

-- ===========================================================================
-- Global mode detection
-- ===========================================================================
-- Detect context: "editor" or "repl"
MODE = loki.get_lines and "editor" or "repl"

-- ===========================================================================
-- Auto-load language definitions from .loki/languages/
-- ===========================================================================
local lang_dir = ".loki/languages"
local loaded_count = 0

-- Try to load all language definition files
local handle = io.popen("ls " .. lang_dir .. "/*.lua 2>/dev/null")
if handle then
    for filepath in handle:lines() do
        local success, result = pcall(dofile, filepath)
        if success and result then
            loaded_count = loaded_count + 1
        elseif not success then
            -- Report error but continue loading other languages
            if MODE == "editor" then
                loki.status("Warning: Failed to load " .. filepath)
            else
                print("Warning: Failed to load " .. filepath .. ": " .. tostring(result))
            end
        end
    end
    handle:close()
end

-- Set a welcome message
if loaded_count > 0 then
    loki.status(string.format("Lua scripting enabled! Loaded %d language(s).", loaded_count))
else
    loki.status("Lua scripting enabled! Press Ctrl-L to run commands.")
end

-- ===========================================================================
-- Load custom color theme (optional)
-- ===========================================================================
-- Uncomment one of these to use a pre-made theme:
-- dofile(".loki/themes/dracula.lua")
-- dofile(".loki/themes/monokai.lua")
-- dofile(".loki/themes/nord.lua")
-- dofile(".loki/themes/github-light.lua")

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

-- ===========================================================================
-- Editor utility functions namespace (only available in editor, not REPL)
-- ===========================================================================
editor = editor or {}

-- Only define editor functions if we're in the editor context (not REPL)
if MODE == "editor" then
    -- Count lines in buffer
    function editor.count_lines()
        local n = loki.get_lines()
        loki.status(string.format("File has %d lines", n))
    end

    -- Show cursor position
    function editor.cursor()
        local row, col = loki.get_cursor()
        loki.status(string.format("Cursor at row %d, col %d", row, col))
    end

    -- Insert current timestamp
    function editor.timestamp()
        local timestamp = os.date("%Y-%m-%d %H:%M:%S")
        loki.insert_text(timestamp)
        loki.status("Inserted timestamp")
    end

    -- Print first line
    function editor.first_line()
        local line = loki.get_line(0)
        if line then
            loki.status("First line: " .. line)
        else
            loki.status("No lines in file")
        end
    end

    -- Backward compatibility aliases (deprecated)
    count_lines = editor.count_lines
    show_cursor = editor.cursor
    insert_timestamp = editor.timestamp
    first_line = editor.first_line
end

local hl = loki.hl or {}
local HL_TYPE_MARKDOWN = 1
local TODO_TAGS = { "TODO", "FIXME" }

local function add_span(spans, start_col, length, style)
    if not (start_col and length and length > 0) then return end
    table.insert(spans, { start = start_col, length = length, style = style })
end

local function add_span_range(spans, start_col, stop_col_after, style)
    if not (start_col and stop_col_after and stop_col_after > start_col) then return end
    add_span(spans, start_col, stop_col_after - start_col, style)
end

-- Highlight Markdown constructs and TODO/FIXME tags regardless of built-in rules.
function loki.highlight_row(idx, text, render, syntax_type, default_applied)
    local spans = {}

    if syntax_type == HL_TYPE_MARKDOWN then
        -- Headings: colour hashes and title separately.
        local hashes, whitespace = render:match("^(#+)(%s*)")
        if hashes then
            add_span(spans, 1, #hashes, "keyword1")
            local title_start = #hashes + #whitespace + 1
            if title_start <= #render then
                add_span(spans, title_start, #render - title_start + 1, "string")
            end
        end

        -- Block quotes (> ...) highlight leading markers.
        local quote = render:match("^(%s*>+)")
        if quote then
            add_span(spans, 1, #quote, "comment")
        end

        -- Fenced code blocks (```lang) highlight fence and language id.
        local indent, fence, lang = render:match("^(%s*)(```+)([%w%-_]*)")
        if fence then
            local fence_start = #indent + 1
            add_span(spans, fence_start, #fence, "keyword2")
            if lang and #lang > 0 then
                add_span(spans, fence_start + #fence, #lang, "number")
            end
        end

        -- Horizontal rules (---, ***, ___): colour entire line.
        if render:match("^%s*([*_%-])%1%1[%s*_%-]*$") then
            add_span(spans, 1, #render, "comment")
        end

        -- Task list checkboxes: highlight [ ] or [x].
        local checkbox_start = render:match("^%s*[-*+] ()%[[ xX]%]")
        if checkbox_start then
            add_span(spans, checkbox_start, 3, "number")
        end

        -- Inline code `code` (avoid triple backtick fences).
        local search_from = 1
        while true do
            local start_pos = string.find(render, "`", search_from, true)
            if not start_pos then break end
            if render:sub(start_pos, start_pos + 2) == "```" then
                search_from = start_pos + 3
            else
                local stop_pos = string.find(render, "`", start_pos + 1, true)
                if not stop_pos then break end
                add_span(spans, start_pos, stop_pos - start_pos + 1, "string")
                search_from = stop_pos + 1
            end
        end

        -- Bold emphasis **text**.
        for start_pos, stop_pos in render:gmatch("()%*%*[^%*]+%*%*()") do
            add_span_range(spans, start_pos, stop_pos, "keyword2")
        end

        -- Italic emphasis *text* (skip bold) and _text_.
        for start_pos, stop_pos in render:gmatch("()%*[^%*\n]+%*()") do
            local segment = render:sub(start_pos, stop_pos - 1)
            if not segment:find("%*%*") then
                add_span_range(spans, start_pos, stop_pos, "keyword1")
            end
        end
        for start_pos, stop_pos in render:gmatch("()%_[^_\n]+_()") do
            local segment = render:sub(start_pos, stop_pos - 1)
            if not segment:find("__") then
                add_span_range(spans, start_pos, stop_pos, "keyword1")
            end
        end

        -- Links [label](target) highlight whole sequence.
        for start_pos, stop_pos in render:gmatch("()%[[^%]]+%]%([^%)]+%)()") do
            add_span_range(spans, start_pos, stop_pos, "match")
        end
    end

    -- TODO/FIXME alerts for any language.
    for _, tag in ipairs(TODO_TAGS) do
        local search_from = 1
        while true do
            local start_pos = string.find(render, tag, search_from, true)
            if not start_pos then break end
            add_span(spans, start_pos, #tag, "match")
            search_from = start_pos + #tag
        end
    end

    if #spans > 0 then
        return { spans = spans }
    end
end

-- ===========================================================================
-- AI Completion namespace (requires OPENAI_API_KEY environment variable)
-- ===========================================================================
ai = {}

-- Internal: response handler for AI requests
local function ai_response_handler(response)
    if not response then
        if MODE == "editor" then
            loki.status("Error: No response from AI")
        else
            print("Error: No response from AI")
        end
        return
    end

    -- Check for API errors first
    local error_msg = response:match('"error"%s*:%s*{.-"message"%s*:%s*"(.-)"')
    if error_msg then
        if MODE == "editor" then
            loki.status("API Error: " .. error_msg)
        else
            print("API Error: " .. error_msg)
        end
        return
    end

    -- Parse OpenAI response format: {"choices":[{"message":{"content":"..."}}]}
    -- First try the nested format (OpenAI chat completions)
    local content = response:match('"message"%s*:%s*{.-"content"%s*:%s*"(.-[^\\])"')

    -- If that fails, try simple format: {"content":"..."}
    if not content then
        content = response:match('"content"%s*:%s*"(.-[^\\])"')
    end

    if content then
        -- Unescape JSON string
        content = content:gsub('\\n', '\n')
        content = content:gsub('\\t', '\t')
        content = content:gsub('\\"', '"')
        content = content:gsub('\\\\', '\\')

        -- Output response based on context
        if MODE == "editor" then
            -- Editor context: insert into buffer
            loki.insert_text("\n\n--- AI Response ---\n" .. content .. "\n---\n")
            loki.status("AI response inserted!")
        else
            -- REPL context: print to console
            print("\n--- AI Response ---")
            print(content)
            print("---\n")
        end
    else
        -- Show first 100 chars of response for debugging
        local preview = response:sub(1, math.min(100, #response))
        if MODE == "editor" then
            loki.status("Error parsing response: " .. preview)
        else
            print("Error parsing response: " .. preview)
        end
    end
end

-- Send entire buffer (or text) to OpenAI and insert response asynchronously
-- In editor: ai.complete() uses buffer content
-- In REPL: ai.complete("your prompt text") or ai.complete() prompts for input
function ai.complete(text)
    local prompt

    if text then
        -- Explicit text provided
        prompt = text
    elseif MODE == "editor" then
        -- Editor context: get buffer content
        local lines = {}
        for i = 0, loki.get_lines() - 1 do
            table.insert(lines, loki.get_line(i))
        end
        prompt = table.concat(lines, "\n")
    else
        -- REPL context without text: error
        print("Usage: ai.complete('your prompt text')")
        return
    end

    -- Get API key from environment
    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        if MODE == "editor" then
            loki.status("Error: OPENAI_API_KEY environment variable not set")
        else
            print("Error: OPENAI_API_KEY environment variable not set")
        end
        return
    end

    -- Build JSON request body
    local json_body = string.format([[{
  "model": "gpt-5-nano",
  "messages": [
    {"role": "user", "content": %s}
  ]
}]], string.format("%q", prompt))

    -- Set up headers
    local headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " .. api_key
    }

    -- Make async HTTP request (non-blocking)
    loki.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        headers,
        "ai_response_handler"  -- Callback function
    )

    if MODE == "editor" then
        loki.status("AI request sent... (will appear when ready)")
    else
        print("AI request sent... (will appear when ready)")
    end
end

-- Get code explanation from AI
-- In editor: ai.explain() uses buffer content
-- In REPL: ai.explain("code to explain")
function ai.explain(code)
    if not code then
        if MODE == "editor" then
            -- Editor context: get buffer content
            local lines = {}
            for i = 0, loki.get_lines() - 1 do
                table.insert(lines, loki.get_line(i))
            end
            code = table.concat(lines, "\n")
        else
            -- REPL context without text: error
            print("Usage: ai.explain('code to explain')")
            return
        end
    end

    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        if MODE == "editor" then
            loki.status("Error: OPENAI_API_KEY not set")
        else
            print("Error: OPENAI_API_KEY not set")
        end
        return
    end

    local prompt = "Explain this code concisely:\n\n" .. code
    local json_body = string.format([[{
  "model": "gpt-5-nano",
  "messages": [
    {"role": "user", "content": %s}
  ]
}]], string.format("%q", prompt))

    local headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " .. api_key
    }

    loki.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        headers,
        "ai_response_handler"
    )

    if MODE == "editor" then
        loki.status("Requesting code explanation...")
    else
        print("Requesting code explanation...")
    end
end

-- Backward compatibility aliases (deprecated)
ai_complete = ai.complete
ai_explain = ai.explain
ai_response_handler = ai_response_handler

-- ===========================================================================
-- Test namespace
-- ===========================================================================
test = {}

-- Internal: handler for HTTP test
local function test_http_handler(response)
    if response then
        loki.insert_text("\n\nGitHub Zen: " .. response .. "\n")
        loki.status("HTTP test successful!")
    else
        loki.status("HTTP test failed")
    end
end

-- Test the async HTTP with a simple GET request
function test.http()
    loki.async_http(
        "https://api.github.com/zen",
        "GET",
        nil,
        {"User-Agent: loki-editor"},
        "test_http_handler"
    )
    loki.status("Testing HTTP...")
end

-- Backward compatibility aliases (deprecated)
test_http = test.http
test_http_handler = test_http_handler

print("Loki Lua init.lua loaded successfully")
print("Available commands:")
print("")

-- Only show editor utilities if in editor context
if MODE == "editor" then
    print("Editor utilities:")
    print("  editor.count_lines()  - Show line count")
    print("  editor.cursor()       - Show cursor position")
    print("  editor.timestamp()    - Insert current date/time")
    print("  editor.first_line()   - Display first line")
    print("")
end

print("AI functions (requires OPENAI_API_KEY):")
if MODE == "editor" then
    print("  ai.complete()         - Send buffer to AI for completion")
    print("  ai.explain()          - Get code explanation from AI")
else
    print("  ai.complete('text')   - Send text to AI for completion")
    print("  ai.explain('code')    - Get code explanation from AI")
end
print("")
print("Test functions:")
print("  test.http()           - Test async HTTP with GitHub API")
