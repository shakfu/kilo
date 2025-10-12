-- Markdown language definition
-- Used as default fallback for text files

return loki.register_language({
    name = "Markdown",
    extensions = {".md", ".markdown", ".txt"},
    keywords = {
        -- Markdown has no traditional keywords, but we can highlight common syntax
    },
    types = {
        -- No types in markdown
    },
    line_comment = "",  -- Markdown doesn't have comments
    block_comment_start = "",
    block_comment_end = "",
    separators = ",.()+-/*=~%[]{}:;",
    highlight_strings = false,  -- Don't highlight strings in markdown
    highlight_numbers = false,  -- Don't highlight numbers in markdown
})
