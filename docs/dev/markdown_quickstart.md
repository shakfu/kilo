# Markdown Rendering - Quick Start

## TL;DR

**To render markdown to HTML in loki-editor:**

```
1. Open your .md file:  ./build/loki-editor myfile.md
2. Press Ctrl-L
3. Type: editor.render_markdown_to_html()
4. Press Enter
5. Done! Check myfile.html
```

## Three Ways to Render Markdown

### 1. Quick File Rendering (Recommended)

```lua
-- In loki-editor (Ctrl-L to open REPL):
editor.render_markdown_to_html()
```

**What it does:**
- [x] Reads current buffer content
- [x] Converts markdown → HTML
- [x] Creates standalone HTML file with CSS
- [x] Saves as `filename.html`

### 2. Get Document Statistics

```lua
-- In loki-editor (Ctrl-L):
editor.markdown_stats()
```

**Output:** `"Markdown: 5 headings, 3 links, 2 code blocks"`

### 3. Direct API (For Scripting)

```lua
-- Simple one-liner:
local html = markdown.to_html("# Hello\n\nWorld")

-- Full parsing:
local doc = markdown.parse("# Title\n\n[Link](url)")
local headings = doc:extract_headings()
local links = doc:extract_links()
print("Found " .. doc:count_headings() .. " headings")
```

## Options

```lua
-- Safe mode (recommended - strips HTML/scripts)
markdown.OPT_SAFE

-- Smart quotes and punctuation
markdown.OPT_SMART

-- Combine options with |
local html = markdown.to_html(text, markdown.OPT_SAFE | markdown.OPT_SMART)
```

## Output Formats

```lua
local doc = markdown.parse(text)

doc:render_html()          -- HTML
doc:render_xml()           -- XML (AST)
doc:render_latex(0, 80)    -- LaTeX
doc:render_man(0, 80)      -- Man page
doc:render_commonmark(0, 80) -- Normalized markdown
```

## Example Workflow

**Edit → Render → View**

```bash
# 1. Edit your markdown
./build/loki-editor README.md

# 2. Save changes (in editor: Ctrl-S)

# 3. Render to HTML (in editor: Ctrl-L)
editor.render_markdown_to_html()

# 4. View result
open README.html
```

## Keyboard Shortcuts (Optional)

Add to `.loki/init.lua`:

```lua
-- Register :md command
loki.register_ex_command("md", function()
    return editor.render_markdown_to_html()
end, "Render markdown to HTML")

-- Now just type: :md
```

## Batch Processing

Convert multiple files with standalone REPL:

```bash
cat > convert.lua << 'EOF'
for _, file in ipairs(arg) do
    local f = io.open(file, "r")
    local html = markdown.to_html(f:read("*all"), markdown.OPT_SAFE)
    f:close()

    local out = io.open(file:gsub("%.md$", ".html"), "w")
    out:write(html)
    out:close()
end
EOF

./build/loki-repl convert.lua *.md
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "No filename" | Save file first: `Ctrl-S` or `:w filename.md` |
| "Failed to parse" | Check syntax with `markdown.validate(text)` |
| Empty output | Verify buffer has content: `loki.get_lines()` |

## Complete Documentation

See [docs/MARKDOWN_RENDERING.md](docs/MARKDOWN_RENDERING.md) for:
- Advanced usage
- Custom styling
- AST traversal
- Performance tips
- All API functions

## Version

Using cmark 0.31.1 (CommonMark spec 0.31)

```lua
-- Check version
print(markdown.version())  -- "0.31.1"
```
