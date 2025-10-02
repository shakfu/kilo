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
