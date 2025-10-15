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

-- Get entire buffer content as string
function M.get_buffer_text()
    local lines = {}
    local num_lines = loki.get_lines()
    for i = 0, num_lines - 1 do
        local line = loki.get_line(i)
        if line then
            table.insert(lines, line)
        end
    end
    return table.concat(lines, "\n")
end

-- Render current markdown file to HTML and save to .html file
function M.render_markdown_to_html()
    local filename = loki.get_filename()

    if not filename then
        loki.status("Error: No filename (save file first)")
        return false
    end

    -- Check if file has markdown extension
    if not (filename:match("%.md$") or filename:match("%.markdown$")) then
        loki.status("Warning: File doesn't have .md or .markdown extension")
    end

    -- Get buffer content
    local content = M.get_buffer_text()

    if not content or content == "" then
        loki.status("Error: Empty buffer")
        return false
    end

    -- Parse markdown
    local doc = markdown.parse(content)
    if not doc then
        loki.status("Error: Failed to parse markdown")
        return false
    end

    -- Render to HTML
    local html = doc:render_html(markdown.OPT_SAFE)
    if not html then
        loki.status("Error: Failed to render HTML")
        return false
    end

    -- Generate output filename
    local html_filename = filename:gsub("%.md$", ".html"):gsub("%.markdown$", ".html")

    -- If filename didn't change, append .html
    if html_filename == filename then
        html_filename = filename .. ".html"
    end

    -- Write HTML file
    local file, err = io.open(html_filename, "w")
    if not file then
        loki.status("Error: Cannot write " .. html_filename .. ": " .. tostring(err))
        return false
    end

    -- Write HTML with basic document structure
    file:write('<!DOCTYPE html>\n')
    file:write('<html>\n')
    file:write('<head>\n')
    file:write('  <meta charset="UTF-8">\n')
    file:write('  <meta name="viewport" content="width=device-width, initial-scale=1.0">\n')
    file:write('  <title>' .. filename .. '</title>\n')
    file:write('  <style>\n')
    file:write('    body { max-width: 800px; margin: 40px auto; padding: 0 20px; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; line-height: 1.6; }\n')
    file:write('    code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }\n')
    file:write('    pre { background: #f4f4f4; padding: 16px; border-radius: 6px; overflow-x: auto; }\n')
    file:write('    pre code { background: none; padding: 0; }\n')
    file:write('    blockquote { border-left: 4px solid #ddd; margin: 0; padding-left: 16px; color: #666; }\n')
    file:write('  </style>\n')
    file:write('</head>\n')
    file:write('<body>\n')
    file:write(html)
    file:write('\n</body>\n')
    file:write('</html>\n')
    file:close()

    loki.status("Rendered to " .. html_filename)
    return true
end

-- Render markdown to HTML and insert at cursor
function M.insert_markdown_as_html()
    local content = M.get_buffer_text()

    if not content or content == "" then
        loki.status("Error: Empty buffer")
        return false
    end

    local html = markdown.to_html(content, markdown.OPT_SAFE)
    if not html then
        loki.status("Error: Failed to render HTML")
        return false
    end

    -- Insert at cursor
    loki.insert_text("\n\n<!-- HTML Rendering -->\n")
    loki.insert_text(html)
    loki.status("Inserted HTML rendering")
    return true
end

-- Show markdown document statistics
function M.markdown_stats()
    local content = M.get_buffer_text()

    if not content or content == "" then
        loki.status("Error: Empty buffer")
        return false
    end

    local doc = markdown.parse(content)
    if not doc then
        loki.status("Error: Failed to parse markdown")
        return false
    end

    local headings = doc:count_headings()
    local links = doc:count_links()
    local blocks = doc:count_code_blocks()

    loki.status(string.format("Markdown: %d headings, %d links, %d code blocks",
                             headings, links, blocks))
    return true
end

-- Register help for REPL
if loki.repl and loki.repl.register then
    loki.repl.register("editor.count_lines", "Show line count")
    loki.repl.register("editor.cursor", "Show cursor position")
    loki.repl.register("editor.timestamp", "Insert current date/time")
    loki.repl.register("editor.first_line", "Display first line")
    loki.repl.register("editor.render_markdown_to_html", "Render markdown to HTML file")
    loki.repl.register("editor.insert_markdown_as_html", "Insert HTML rendering at cursor")
    loki.repl.register("editor.markdown_stats", "Show markdown document statistics")
end

return M
