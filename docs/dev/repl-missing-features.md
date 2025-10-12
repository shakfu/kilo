# Missing Readline/Editline Features in loki-repl

**Date:** 2025-10-11
**Status:** Feature gap analysis

## Currently Implemented [x]

Our current editline/readline integration provides:

- [x] Basic line editing (arrows, home/end)
- [x] Command history (up/down arrows)
- [x] History persistence (`.loki/repl_history`)
- [x] Standard shortcuts (Ctrl-A/E/K/U/W/D)
- [x] History limit (1000 entries)

## Missing High-Value Features

### 1. Tab Completion ⭐⭐⭐⭐⭐ (HIGHEST PRIORITY)

**What it is:** Press Tab to auto-complete identifiers

**Use cases:**

```lua
loki> pr<TAB>          → print
loki> loki.get_<TAB>   → loki.get_lines, loki.get_line, loki.get_cursor
loki> string.su<TAB>   → string.sub, string.sub
loki> io.op<TAB>       → io.open
```

**Implementation complexity:** Medium

- Need to register completion callback
- Query Lua global table
- Match `loki.*` API functions
- Match loaded module functions
- Match local variables (requires parser)

**Value:** Extremely high

- Discoverability of API
- Faster typing
- Fewer errors
- Professional UX

**Example implementation:**

```c
#ifdef LOKI_HAVE_READLINE
char *completion_generator(const char *text, int state) {
    static int list_index, len;
    static char **matches;

    if (!state) {
        list_index = 0;
        len = strlen(text);
        // Get Lua globals and loki.* functions
        matches = get_lua_completions(E.L, text);
    }

    if (matches && matches[list_index]) {
        return strdup(matches[list_index++]);
    }
    return NULL;
}

char **loki_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, completion_generator);
}

// In init:
rl_attempted_completion_function = loki_completion;
```

**Completion sources:**

1. **Lua keywords:** `function`, `local`, `if`, `then`, `else`, `end`, etc.
2. **Lua globals:** `print`, `type`, `pairs`, `ipairs`, `table`, `string`, `math`, etc.
3. **loki API:** `loki.status`, `loki.get_lines`, `loki.insert_text`, etc.
4. **Loaded modules:** After `require('json')`, complete `json.*`
5. **Local variables:** Requires parsing current scope (complex)
6. **Table keys:** For `mytable.<TAB>` (very complex)

**Estimated effort:** 4-8 hours
**Dependencies:** None (readline/editline provide the mechanism)

---

### 2. Incremental History Search (Ctrl-R) ⭐⭐⭐⭐

**What it is:** Search through command history as you type

**Use case:**

```text
(reverse-i-search)`http': test_http()
```

Type Ctrl-R, then start typing - shows matching commands from history.

**Current workaround:** Manually press up arrow multiple times

**Implementation complexity:** Very low

- **Already works!** This is a default readline/editline feature
- Just needs to be documented

**Value:** High for users with long history

**Testing:**

```bash
$ ./loki-repl
loki> print("test 1")
loki> print("test 2")
loki> x = 42
loki> # Press Ctrl-R, type "print"
# Should show: (reverse-i-search)`print': print("test 2")
```

**Estimated effort:** 0 hours (already works, just document it)

---

### 3. Multi-line Input Support ⭐⭐⭐⭐

**What it is:** Automatically continue to next line for incomplete Lua

**Use case:**

```lua
loki> function factorial(n)
cont>   if n <= 1 then
cont>     return 1
cont>   else
cont>     return n * factorial(n-1)
cont>   end
cont> end
Function defined: factorial
```

**Current behavior:** Each line executed separately, causes syntax errors

**Implementation complexity:** Medium

- Detect incomplete Lua (unclosed brackets, `do`/`then` without `end`)
- Change prompt to "cont> " for continuation lines
- Buffer lines until complete
- Pass complete block to Lua

**Value:** Essential for defining functions/tables in REPL

**Example implementation:**

```c
bool is_lua_complete(const char *code) {
    lua_State *L = luaL_newstate();
    int status = luaL_loadstring(L, code);
    lua_close(L);

    // LUA_ERRSYNTAX with "unexpected <eof>" means incomplete
    if (status == LUA_ERRSYNTAX) {
        const char *msg = lua_tostring(L, -1);
        if (strstr(msg, "<eof>") || strstr(msg, "unfinished"))
            return false;
    }
    return true;
}

// In REPL loop:
char *buffer = strdup(line);
while (!is_lua_complete(buffer)) {
    char *cont = readline("cont> ");
    if (!cont) break;

    size_t newlen = strlen(buffer) + strlen(cont) + 2;
    buffer = realloc(buffer, newlen);
    strcat(buffer, "\n");
    strcat(buffer, cont);
    free(cont);
}
// Now execute complete buffer
```

**Estimated effort:** 4-6 hours

---

### 4. Syntax Hints/Suggestions ⭐⭐⭐

**What it is:** Show completion suggestion inline (fish shell style)

**Use case:**

```lua
loki> print(1+1)
# User types "pri"
loki> pri|nt(1+1)
      ^^^^^^^ (shown in gray, press → to accept)
```

**Implementation complexity:** High

- Not standard readline (need custom extension)
- Requires terminal manipulation
- linenoise and isocline support this natively

**Value:** High for discoverability, but not essential

**Estimated effort:** 8-12 hours (or switch to library with native support)

---

### 5. Brace/Parenthesis Matching ⭐⭐⭐

**What it is:** Highlight matching braces as you type

**Use case:**

```lua
loki> print(table.concat({1,2,3}, ","))
              ^                ^  # Highlights matching parens
```

**Implementation complexity:** Very high with readline

- Requires custom display handler
- Terminal cursor manipulation
- Might interfere with editline/readline

**Value:** Medium (nice to have)

**Alternative:** Use external editor (`:edit` command to open in $EDITOR)

**Estimated effort:** 12-20 hours

---

### 6. Context-aware Completion ⭐⭐⭐⭐

**What it is:** Different completions based on context

**Use cases:**

```lua
loki> require('<TAB>        # Complete module names
loki> dofile('<TAB>         # Complete file paths
loki> mytable.<TAB>         # Complete table keys
loki> string:<TAB>          # Complete string methods
```

**Implementation complexity:** High

- Parse current line to understand context
- Different completion sources per context
- File system access for paths
- Lua table introspection

**Value:** Very high for advanced users

**Example:**

```c
char **context_aware_completion(const char *text, int start, int end) {
    // Get the line before cursor
    char *line = rl_line_buffer;

    // Check for require('
    if (strstr(line, "require(") && (line[start-1] == '\'' || line[start-1] == '"')) {
        return complete_lua_modules(text);
    }

    // Check for dofile('
    if (strstr(line, "dofile(") && (line[start-1] == '\'' || line[start-1] == '"')) {
        return complete_file_paths(text);
    }

    // Check for table access: xxx.
    if (start > 0 && line[start-1] == '.') {
        return complete_table_keys(line, start, text);
    }

    // Default: complete identifiers
    return complete_identifiers(text);
}
```

**Estimated effort:** 10-16 hours

---

### 7. Line Editing Undo/Redo ⭐⭐

**What it is:** Undo changes to current line (Ctrl-_ or Ctrl-X Ctrl-U)

**Current status:** Already works with readline/editline!

- Ctrl-_ : Undo
- Ctrl-X Ctrl-U : Undo all changes

**Implementation complexity:** Zero (built-in)

**Value:** Low (rarely needed for REPL)

**Action:** Document in help

---

### 8. Vi/Emacs Mode Toggle ⭐⭐

**What it is:** Switch between vi and emacs key bindings

**Current:** Emacs mode (default)

**Vi mode commands:**

- `Esc` to enter command mode
- `i` to enter insert mode
- `hjkl` for movement
- `dd` to delete line
- `0` and `$` for home/end

**Implementation:**

```c
// Already supported by readline/editline!
rl_bind_key('\t', rl_complete);  // Emacs mode (default)
rl_variable_bind("editing-mode", "vi");  // Vi mode
```

**Value:** Medium (for vi users)

**Implementation complexity:** Zero (built-in, just expose option)

**Estimated effort:** 1 hour (add config option)

---

### 9. Custom Key Bindings ⭐⭐

**What it is:** Let users define custom keyboard shortcuts

**Use case:**

```lua
-- In .loki/init.lua
loki.repl.bind_key("Ctrl-T", function()
    loki.insert_text(os.date("%Y-%m-%d %H:%M:%S"))
end)
```

**Implementation complexity:** Medium

- Expose readline's key binding API to Lua
- Register Lua callbacks for keys
- Handle re-entrancy (Lua calling from C from Lua)

**Value:** Medium (power users)

**Estimated effort:** 6-8 hours

---

### 10. History Timestamps ⭐

**What it is:** Record when each command was executed

**Format:**

```text
#1625097600
print(1+1)
#1625097615
test_http()
```

**Current:** Plain history without timestamps

**Implementation:**

```c
// Readline supports this with:
history_write_timestamps = 1;
```

**Value:** Low (rarely useful)

**Estimated effort:** 1 hour

---

### 11. Persistent Variables Across Sessions ⭐⭐⭐

**What it is:** Remember variables between REPL sessions

**Use case:**

```lua
# Session 1:
loki> x = 42
loki> quit

# Session 2:
loki> print(x)  -- Still 42!
```

**Implementation:**

- Save Lua state to `.loki/repl_state.lua`
- Serialize global table
- Load on startup

**Value:** High for interactive development

**Estimated effort:** 6-10 hours

---

### 12. Output Paging ⭐⭐

**What it is:** Page long output with `less`-style pager

**Use case:**

```lua
loki> for i=1,100 do print(i) end
# Instead of scrolling off screen, shows page-by-page
# Space = next page, q = quit
```

**Implementation complexity:** Medium

- Detect TTY
- Count output lines
- Integrate with `less` or custom pager

**Value:** Medium (helpful for exploring)

**Estimated effort:** 4-6 hours

---

### 13. Code Execution from External Editor ⭐⭐⭐

**What it is:** Edit multi-line code in $EDITOR, then execute

**Use case:**

```lua
loki> :edit
# Opens $EDITOR with current buffer/last function
# Save and quit to execute
```

**Implementation:**

```c
void repl_edit_command(void) {
    char tmpfile[] = "/tmp/loki_edit_XXXXXX";
    int fd = mkstemp(tmpfile);

    // Write current buffer to file
    write(fd, buffer, strlen(buffer));
    close(fd);

    // Open in editor
    char *editor = getenv("EDITOR");
    if (!editor) editor = "vi";

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s", editor, tmpfile);
    system(cmd);

    // Read back and execute
    FILE *f = fopen(tmpfile, "r");
    // ... read and execute
    unlink(tmpfile);
}
```

**Value:** High for complex code

**Estimated effort:** 3-4 hours

---

### 14. Command Aliases ⭐⭐

**What it is:** Define short aliases for common commands

**Use case:**

```lua
-- In .loki/init.lua
loki.repl.alias("p", "print")
loki.repl.alias("t", "type")

-- Then in REPL:
loki> p(42)  -- Same as print(42)
```

**Value:** Medium (convenience)

**Estimated effort:** 2-3 hours

---

## Priority Matrix

| Feature | Value | Effort | Priority | Status |
|---------|-------|--------|----------|--------|
| **Tab Completion** | ⭐⭐⭐⭐⭐ | Medium |  Critical | Not started |
| **Multi-line Input** | ⭐⭐⭐⭐ | Medium |  Critical | Not started |
| **History Search (Ctrl-R)** | ⭐⭐⭐⭐ | Zero | [x] Done | Works, needs docs |
| **Context-aware Completion** | ⭐⭐⭐⭐ | High | 🟡 Important | Not started |
| **External Editor** | ⭐⭐⭐ | Low | 🟡 Important | Not started |
| **Syntax Hints** | ⭐⭐⭐ | High | 🟢 Nice-to-have | Not started |
| **Persistent Variables** | ⭐⭐⭐ | Medium | 🟢 Nice-to-have | Not started |
| **Brace Matching** | ⭐⭐⭐ | Very High | 🟢 Nice-to-have | Not started |
| **Vi Mode Toggle** | ⭐⭐ | Zero | 🟢 Nice-to-have | Built-in, needs config |
| **Custom Key Bindings** | ⭐⭐ | Medium | 🟢 Nice-to-have | Not started |
| **Undo/Redo** | ⭐⭐ | Zero | [x] Done | Works (Ctrl-_) |
| **Output Paging** | ⭐⭐ | Medium |  Low | Not started |
| **Command Aliases** | ⭐⭐ | Low |  Low | Not started |
| **History Timestamps** | ⭐ | Low |  Low | Not started |

## Implementation Roadmap

### Phase 1: Critical Features (Next Release)

**1. Tab Completion** (8 hours)

- [x] Research readline completion API
- [ ] Implement Lua global completion
- [ ] Implement `loki.*` API completion
- [ ] Add keyword completion
- [ ] Test and document

**2. Multi-line Input** (6 hours)

- [ ] Detect incomplete Lua code
- [ ] Buffer continuation lines
- [ ] Change prompt for continuations
- [ ] Execute complete blocks
- [ ] Test with functions/tables

**3. Document Existing Features** (2 hours)

- [ ] Ctrl-R history search
- [ ] Ctrl-_ undo
- [ ] All standard readline shortcuts
- [ ] Update REPL_LINE_EDITING.md

**Total: ~16 hours**

### Phase 2: Important Features (Later)

**4. Context-aware Completion** (16 hours)

- File path completion for `dofile()`
- Module completion for `require()`
- Table key completion
- String method completion

**5. External Editor Integration** (4 hours)

- `:edit` command
- Respect $EDITOR
- Execute edited code

**6. Vi Mode Support** (2 hours)

- Add config option
- Document vi commands
- Test vi mode

**Total: ~22 hours**

### Phase 3: Nice-to-Have Features (Future)

**7. Syntax Hints** (12 hours)

- Requires custom display handler
- Or switch to linenoise/isocline

**8. Persistent State** (10 hours)

- Serialize global table
- Handle non-serializable values
- Load on startup

**9. Output Paging** (6 hours)

- Integrate less-style pager
- Handle TTY detection

**Total: ~28 hours**

## Quick Wins (< 2 hours each)

These features are **already built-in** to readline/editline, just need documentation or minimal config:

1. [x] **Ctrl-R history search** - Already works!
2. [x] **Ctrl-_ undo** - Already works!
3. [x] **Ctrl-X Ctrl-U undo all** - Already works!
4. **Vi mode** - Just needs config exposure (1 hour)
5. **History timestamps** - One-line config (1 hour)

## Example: Implementing Tab Completion

Here's a detailed example for the highest-priority feature:

```c
// In src/main_repl.c

#ifdef LOKI_HAVE_LINEEDIT

/* Lua completion helper */
static char **get_lua_completions(lua_State *L, const char *text) {
    char **matches = NULL;
    int count = 0;
    size_t len = strlen(text);

    // Check loki.* API first
    if (strncmp(text, "loki.", 5) == 0) {
        const char *loki_funcs[] = {
            "loki.status", "loki.get_lines", "loki.get_line",
            "loki.get_cursor", "loki.insert_text", "loki.get_filename",
            "loki.async_http", NULL
        };

        for (int i = 0; loki_funcs[i]; i++) {
            if (strncmp(loki_funcs[i], text, len) == 0) {
                matches = realloc(matches, sizeof(char*) * (count + 2));
                matches[count++] = strdup(loki_funcs[i]);
            }
        }
    }

    // Lua keywords
    const char *keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false",
        "for", "function", "goto", "if", "in", "local", "nil",
        "not", "or", "repeat", "return", "then", "true", "until", "while",
        NULL
    };

    for (int i = 0; keywords[i]; i++) {
        if (strncmp(keywords[i], text, len) == 0) {
            matches = realloc(matches, sizeof(char*) * (count + 2));
            matches[count++] = strdup(keywords[i]);
        }
    }

    // Lua globals (iterate _G table)
    lua_getglobal(L, "_G");
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char *key = lua_tostring(L, -2);
            if (strncmp(key, text, len) == 0) {
                matches = realloc(matches, sizeof(char*) * (count + 2));
                matches[count++] = strdup(key);
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (matches) {
        matches[count] = NULL;  // Null-terminate
    }

    return matches;
}

/* Completion generator for readline */
static char *completion_generator(const char *text, int state) {
    static char **matches;
    static int index;

    if (!state) {
        // First call - generate matches
        index = 0;
        matches = get_lua_completions(E.L, text);
    }

    if (matches && matches[index]) {
        return matches[index++];
    }

    // Cleanup
    if (matches) {
        for (int i = 0; matches[i]; i++) {
            free(matches[i]);
        }
        free(matches);
        matches = NULL;
    }

    return NULL;
}

/* Completion entry point */
static char **loki_completion(const char *text, int start, int end) {
    (void)start;
    (void)end;

    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, completion_generator);
}

/* In repl initialization */
static void repl_init_completion(void) {
#ifdef HAVE_READLINE_READLINE_H
    rl_attempted_completion_function = loki_completion;
#endif
}

#endif /* LOKI_HAVE_LINEEDIT */
```

## Conclusion

**Most valuable missing features:**

1. **Tab completion** - Essential for discoverability
2. **Multi-line input** - Essential for functions/tables
3. 🟡 **Context-aware completion** - Great UX improvement
4. 🟡 **External editor** - Important for complex code

**Quick wins:**

- Document Ctrl-R history search (already works!)
- Document Ctrl-_ undo (already works!)
- Expose vi mode config (1 line)

**Recommended next steps:**

1. Implement basic tab completion (Lua globals + keywords)
2. Add multi-line input support
3. Document all existing readline features
4. Add `:edit` command for external editor

**Total effort for Phase 1:** ~16-20 hours for critical features

---

**Status:** Feature gap analysis complete
**Next:** Prioritize and implement tab completion
