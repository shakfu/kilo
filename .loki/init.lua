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

-- Set a welcome message
loki.status("Lua scripting enabled! Press Ctrl-L to run commands.")

-- Example function: count lines
function count_lines()
    local n = loki.get_lines()
    loki.status(string.format("File has %d lines", n))
end

-- Example function: show cursor position
function show_cursor()
    local row, col = loki.get_cursor()
    loki.status(string.format("Cursor at row %d, col %d", row, col))
end

-- Example function: insert current timestamp
function insert_timestamp()
    local timestamp = os.date("%Y-%m-%d %H:%M:%S")
    loki.insert_text(timestamp)
    loki.status("Inserted timestamp")
end

-- Example function: print first line
function first_line()
    local line = loki.get_line(0)
    if line then
        loki.status("First line: " .. line)
    else
        loki.status("No lines in file")
    end
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
-- AI Completion Example (requires OPENAI_API_KEY environment variable)
-- ===========================================================================

-- Main AI completion function
-- Sends entire buffer to OpenAI and inserts response asynchronously
function ai_complete()
    -- Get all buffer content
    local lines = {}
    for i = 0, loki.get_lines() - 1 do
        table.insert(lines, loki.get_line(i))
    end
    local prompt = table.concat(lines, "\n")

    -- Get API key from environment
    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        loki.status("Error: OPENAI_API_KEY environment variable not set")
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

    loki.status("AI request sent... (will appear when ready)")
end

-- Callback function called when AI response arrives
function ai_response_handler(response)
    if not response then
        loki.status("Error: No response from AI")
        return
    end

    -- Check for API errors first
    local error_msg = response:match('"error"%s*:%s*{.-"message"%s*:%s*"(.-)"')
    if error_msg then
        loki.status("API Error: " .. error_msg)
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

        -- Insert response into buffer at cursor position
        loki.insert_text("\n\n--- AI Response ---\n" .. content .. "\n---\n")
        loki.status("AI response inserted!")
    else
        -- Show first 100 chars of response for debugging
        local preview = response:sub(1, math.min(100, #response))
        loki.status("Error parsing response: " .. preview)
    end
end

-- Example: AI completion for specific task
function ai_explain()
    local lines = {}
    for i = 0, loki.get_lines() - 1 do
        table.insert(lines, loki.get_line(i))
    end
    local code = table.concat(lines, "\n")

    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        loki.status("Error: OPENAI_API_KEY not set")
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

    loki.status("Requesting code explanation...")
end

-- Example: Test the async HTTP with a simple GET request
function test_http()
    loki.async_http(
        "https://api.github.com/zen",
        "GET",
        nil,
        {"User-Agent: loki-editor"},
        "test_http_handler"
    )
    loki.status("Testing HTTP...")
end

function test_http_handler(response)
    if response then
        loki.insert_text("\n\nGitHub Zen: " .. response .. "\n")
        loki.status("HTTP test successful!")
    else
        loki.status("HTTP test failed")
    end
end

print("Loki Lua init.lua loaded successfully")
print("Available commands:")
print("  count_lines()      - Show line count")
print("  show_cursor()      - Show cursor position")
print("  insert_timestamp() - Insert current date/time")
print("  ai_complete()      - Send buffer to AI (requires OPENAI_API_KEY)")
print("  ai_explain()       - Get code explanation (requires OPENAI_API_KEY)")
print("  test_http()        - Test async HTTP with GitHub API")
