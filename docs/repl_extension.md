# Extending the Loki REPL

The embedded REPL gives you a fast way to exercise Loki's Lua API without leaving the editor. This guide shows how to shape the experience for your project by registering discoverable commands, composing helper wrappers, and wiring them into the existing async tooling.

## Workflow Basics

- Toggle the panel with `Ctrl-L`; it stays collapsed when idle so the text view keeps its full height.
- The prompt is `>> `. Type Lua expressions, press `Enter`, and read the results immediately above the prompt.
- Built-in helpers: `help`, `history`, `clear`, `clear-history`, `exit`.
- Use `Up`/`Down` to replay previous input, `Ctrl-U` to clear the current line.

## Surfacing Project Commands

You can publish project-specific helpers in the REPL's `help` output by calling `loki.repl.register` inside `.loki/init.lua` (or `~/.loki/init.lua`).

```lua
-- .loki/init.lua
local function insert_header()
    loki.insert_text("/* generated */\n")
end

loki.repl.register(
    "insert_header",
    "Insert a C-style generated header comment",
    "insert_header()"
)

function insert_header()
    insert_header()
end
```

Reload Loki (or run `:reload` from your config) and the REPL `help` command now shows the new entry under "Project commands". Registered examples are optional but make it easier for teammates to discover the right call shape.

## Organizing Helper Modules

- Use plain Lua modules under `.loki/` to group related commands (`require("my_project.lint")`).
- Expose just a thin wrapper that calls into the module, then register the wrapper with `loki.repl.register`.
- Pair registrations with unit-friendly helpers—e.g. a formatter that streams HTTP output back into the buffer through `loki.async_http`.

## Asynchronous Patterns

Combine the REPL with the async HTTP bridge to iterate quickly on AI or tooling integrations:

```lua
local function explain_selection()
    local cursor = { loki.get_cursor() }
    loki.async_http(
        "https://internal.tools/api/explain",
        "POST",
        json.encode({ row = cursor[1], col = cursor[2] }),
        { "Content-Type: application/json" },
        "handle_explain"
    )
end

function handle_explain(response)
    loki.insert_text("\n" .. response)
end

loki.repl.register("explain_selection", "Send selection to internal explain API")
```

Invoking `explain_selection()` from the REPL will queue the HTTP request without blocking the editor.

## Tips

- Use `clear` to reset the output log when testing long-running sequences.
- `clear-history` is handy when you refine a snippet and don't want outdated attempts replayed.
- Log structured data from Lua with `print(vim.inspect(tbl))` or a JSON encoder before inserting into the buffer.
- Keep helper names unique—`loki.repl.register` appends entries in the order you call it, so duplicates clutter the `help` output.

Need more examples? Copy one of the recipes into `.loki/init.lua`, tweak it for your service, and it will be instantly available every time you toggle the REPL.
