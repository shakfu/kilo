-- Markdown and TODO Highlighting Module
-- Provides enhanced syntax highlighting for Markdown and TODO/FIXME tags

local M = {}

local HL_TYPE_MARKDOWN = 1
local TODO_TAGS = { "TODO", "FIXME" }

-- Helper: Add a span with start position and length
local function add_span(spans, start_col, length, style)
    if not (start_col and length and length > 0) then return end
    table.insert(spans, { start = start_col, length = length, style = style })
end

-- Helper: Add a span with start and stop positions
local function add_span_range(spans, start_col, stop_col_after, style)
    if not (start_col and stop_col_after and stop_col_after > start_col) then return end
    add_span(spans, start_col, stop_col_after - start_col, style)
end

-- Highlight Markdown constructs and TODO/FIXME tags regardless of built-in rules.
function M.highlight_row(idx, text, render, syntax_type, default_applied)
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

-- Install the highlight function globally
function M.install()
    loki.highlight_row = M.highlight_row
end

return M
