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

-- ===========================================================================
-- AI Completion Example (requires OPENAI_API_KEY environment variable)
-- ===========================================================================

-- Main AI completion function
-- Sends entire buffer to OpenAI and inserts response asynchronously
function ai_complete()
    -- Get all buffer content
    local lines = {}
    for i = 0, kilo.get_lines() - 1 do
        table.insert(lines, kilo.get_line(i))
    end
    local prompt = table.concat(lines, "\n")

    -- Get API key from environment
    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        kilo.status("Error: OPENAI_API_KEY environment variable not set")
        return
    end

    -- Build JSON request body
    local json_body = string.format([[{
  "model": "gpt-4",
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
    kilo.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        headers,
        "ai_response_handler"  -- Callback function
    )

    kilo.status("AI request sent... (will appear when ready)")
end

-- Callback function called when AI response arrives
function ai_response_handler(response)
    if not response then
        kilo.status("Error: No response from AI")
        return
    end

    -- Simple JSON parsing to extract content field
    -- Pattern matches: "content":"..."
    local content = response:match('"content"%s*:%s*"(.-[^\\])"')

    if content then
        -- Unescape JSON string
        content = content:gsub('\\n', '\n')
        content = content:gsub('\\t', '\t')
        content = content:gsub('\\"', '"')
        content = content:gsub('\\\\', '\\')

        -- Insert response into buffer at cursor position
        kilo.insert_text("\n\n--- AI Response ---\n" .. content .. "\n---\n")
        kilo.status("AI response inserted!")
    else
        -- Show first 100 chars of response for debugging
        local preview = response:sub(1, math.min(100, #response))
        kilo.status("Error parsing response: " .. preview)
    end
end

-- Example: AI completion for specific task
function ai_explain()
    local lines = {}
    for i = 0, kilo.get_lines() - 1 do
        table.insert(lines, kilo.get_line(i))
    end
    local code = table.concat(lines, "\n")

    local api_key = os.getenv("OPENAI_API_KEY")
    if not api_key or api_key == "" then
        kilo.status("Error: OPENAI_API_KEY not set")
        return
    end

    local prompt = "Explain this code concisely:\n\n" .. code
    local json_body = string.format([[{
  "model": "gpt-4",
  "messages": [
    {"role": "user", "content": %s}
  ]
}]], string.format("%q", prompt))

    local headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " .. api_key
    }

    kilo.async_http(
        "https://api.openai.com/v1/chat/completions",
        "POST",
        json_body,
        headers,
        "ai_response_handler"
    )

    kilo.status("Requesting code explanation...")
end

-- Example: Test the async HTTP with a simple GET request
function test_http()
    kilo.async_http(
        "https://api.github.com/zen",
        "GET",
        nil,
        {"User-Agent: kilo-editor"},
        "test_http_handler"
    )
    kilo.status("Testing HTTP...")
end

function test_http_handler(response)
    if response then
        kilo.insert_text("\n\nGitHub Zen: " .. response .. "\n")
        kilo.status("HTTP test successful!")
    else
        kilo.status("HTTP test failed")
    end
end

print("Kilo Lua init.lua loaded successfully")
print("Available commands:")
print("  count_lines()      - Show line count")
print("  show_cursor()      - Show cursor position")
print("  insert_timestamp() - Insert current date/time")
print("  ai_complete()      - Send buffer to AI (requires OPENAI_API_KEY)")
print("  ai_explain()       - Get code explanation (requires OPENAI_API_KEY)")
print("  test_http()        - Test async HTTP with GitHub API")
