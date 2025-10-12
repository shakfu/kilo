# Implement a vim-like :command subsystem in loki

> How implementing a `:<command>` system like vim. Werer would that be
implemented? In its own own file?

⏺ Excellent question! A :command system fits perfectly with loki's existing
modal editing architecture. Let me analyze where and how this should be
implemented.

## Architecture Analysis

Current Modal System

Existing structure (src/loki_modal.c):

```c
/* Current modes in include/loki/core.h */
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_VISUAL,
    /* MODE_COMMAND would go here */
} EditorMode;

/* Main entry point in loki_modal.c */
void modal_process_keypress(editor_ctx_t *ctx, int fd) {
    switch (ctx->mode) {
        case MODE_NORMAL:  process_normal_mode(ctx, fd); break;
        case MODE_INSERT:  process_insert_mode(ctx, fd); break;
        case MODE_VISUAL:  process_visual_mode(ctx, fd); break;
    }
}
```

## Where Command Mode Should Live

Option A: Extend loki_modal.c (~150 lines added)

- Pros: All modal logic in one place, simpler mental model
- Cons: File grows to ~550 lines, mixing concerns (navigation + command
execution)
- Best for: Simple command mode with <10 built-in commands

Option B: New file loki_command.c (~300-400 lines)

- Pros: Separation of concerns, easier to test, extensible
- Cons: Extra file, more indirection
- Best for: Rich command system with 20+ commands, Lua extensibility

### My recommendation: Option B - Create dedicated src/loki_command.c

Here's why:

1. Command execution is distinct from modal navigation - commands involve
parsing, validation, execution (write files, change settings,
search/replace)

2. Growth potential - Command systems naturally expand (:s/find/replace/,
:!shell, :help, :set number, etc.)

3. Testing - Easier to unit test command parser separately from modal
logic

4. Lua integration - Custom commands registered from Lua fit better in
dedicated module

## Implementation Design

File Structure

```text
src/
├── loki_modal.c        # Modal editing (NORMAL/INSERT/VISUAL/COMMAND mode
 switching)
├── loki_command.c      # Command mode logic (NEW)
└── loki_command.h      # Command API (NEW)

include/loki/
└── core.h              # Add MODE_COMMAND to EditorMode enum
```

1. Core Data Structures

src/loki_command.h:

```c
#ifndef LOKI_COMMAND_H
#define LOKI_COMMAND_H

#include "loki/core.h"

/* Command input buffer state */
typedef struct {
    char buffer[256];      /* Command input: ":w file.txt" */
    int length;            /* Current length of input */
    int cursor_pos;        /* Cursor position within command */
    int active;            /* 1 if command mode active, 0 otherwise */
    int history_index;     /* Current position in command history */
} command_state_t;

/* Command handler function signature */
typedef int (*command_handler_t)(editor_ctx_t *ctx, const char *args);

/* Command registration entry */
typedef struct {
    const char *name;          /* Command name (without ':') */
    command_handler_t handler; /* Function to execute */
    const char *help;          /* Help text */
    int min_args;              /* Minimum arguments required */
    int max_args;              /* Maximum arguments (-1 = unlimited) */
} command_def_t;

/* Public API */
void command_mode_init(editor_ctx_t *ctx);
void command_mode_free(editor_ctx_t *ctx);
void command_mode_enter(editor_ctx_t *ctx);
void command_mode_handle_key(editor_ctx_t *ctx, int fd, int key);
int command_execute(editor_ctx_t *ctx, const char *cmdline);
int command_register(const char *name, command_handler_t handler,
                     const char *help, int min_args, int max_args);

/* Built-in command handlers */
int cmd_write(editor_ctx_t *ctx, const char *args);
int cmd_quit(editor_ctx_t *ctx, const char *args);
int cmd_write_quit(editor_ctx_t *ctx, const char *args);
int cmd_help(editor_ctx_t *ctx, const char *args);
int cmd_set(editor_ctx_t *ctx, const char *args);

#endif /* LOKI_COMMAND_H */
```

1. Implementation: `src/loki_command.c`

⏺ Read(`~/projects/loki/include/loki/core.h`)
⎿  Read 50 lines

⏺ Great! I can see MODE_COMMAND is already defined in `include/loki/core.h`
(line 32). Let me show you a complete implementation structure:

Complete Implementation: `src/loki_command.c`

```c
/* loki_command.c - Vim-like command mode implementation
 *
 * Handles command mode (:w, :q, :set, etc.) for vim-like editing.
 * Commands can be built-in (C functions) or registered from Lua.
 */

#include "loki_command.h"
#include "loki_internal.h"
#include "loki_terminal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Command history */
#define COMMAND_HISTORY_MAX 50
static char *command_history[COMMAND_HISTORY_MAX];
static int command_history_len = 0;

/* Dynamic command registry (for Lua-registered commands) */
#define MAX_DYNAMIC_COMMANDS 100
static command_def_t dynamic_commands[MAX_DYNAMIC_COMMANDS];
static int dynamic_command_count = 0;

/* Built-in command table */
static command_def_t builtin_commands[] = {
    {"w",      cmd_write,      "Write (save) file",              0, 1},
    {"write",  cmd_write,      "Write (save) file",              0, 1},
    {"q",      cmd_quit,       "Quit editor",                    0, 0},
    {"quit",   cmd_quit,       "Quit editor",                    0, 0},
    {"wq",     cmd_write_quit, "Write and quit",                 0, 1},
    {"x",      cmd_write_quit, "Write and quit (if modified)",   0, 1},
    {"q!",     cmd_force_quit, "Quit without saving",            0, 0},
    {"help",   cmd_help,       "Show help",                      0, 1},
    {"h",      cmd_help,       "Show help",                      0, 1},
    {"set",    cmd_set,        "Set option (number, wrap, etc)", 1, 2},
    {"e",      cmd_edit,       "Edit file",                      1, 1},
    {"edit",   cmd_edit,       "Edit file",                      1, 1},
    {NULL, NULL, NULL, 0, 0}  /* Sentinel */
};

/* ======================== Command State Management
======================== */

void command_mode_init(editor_ctx_t *ctx) {
    /* Command state is part of editor_ctx_t, just reset it */
    memset(ctx->cmd_buffer, 0, sizeof(ctx->cmd_buffer));
    ctx->cmd_length = 0;
    ctx->cmd_cursor_pos = 0;
}

void command_mode_free(editor_ctx_t *ctx) {
    /* Nothing to free currently - command buffer is inline */
    (void)ctx;
}

void command_mode_enter(editor_ctx_t *ctx) {
    ctx->mode = MODE_COMMAND;
    ctx->cmd_buffer[0] = ':';
    ctx->cmd_length = 1;
    ctx->cmd_cursor_pos = 1;
    editor_set_status_msg(ctx, ":");
}

void command_mode_exit(editor_ctx_t *ctx) {
    ctx->mode = MODE_NORMAL;
    ctx->cmd_length = 0;
    ctx->cmd_cursor_pos = 0;
    editor_set_status_msg(ctx, "");
}

/* ======================== Command History ======================== */

static void command_history_add(const char *cmd) {
    /* Don't add empty or duplicate commands */
    if (!cmd || !cmd[0]) return;
    if (command_history_len > 0 &&
        strcmp(command_history[command_history_len - 1], cmd) == 0) {
        return;
    }

    /* Add to history */
    if (command_history_len < COMMAND_HISTORY_MAX) {
        command_history[command_history_len++] = strdup(cmd);
    } else {
        /* Shift and reuse oldest slot */
        free(command_history[0]);
        memmove(command_history, command_history + 1,
                sizeof(char*) * (COMMAND_HISTORY_MAX - 1));
        command_history[COMMAND_HISTORY_MAX - 1] = strdup(cmd);
    }
}

static const char* command_history_get(int index) {
    if (index < 0 || index >= command_history_len) return NULL;
    return command_history[index];
}

/* ======================== Command Parsing ======================== */

/* Parse command line into command name and arguments */
static int parse_command(const char *cmdline, char **cmd_name, char
**args) {
    /* Skip leading ':' and whitespace */
    while (*cmdline && (*cmdline == ':' || isspace(*cmdline))) {
        cmdline++;
    }

    if (!*cmdline) return 0;  /* Empty command */

    /* Find command name (up to first space or end) */
    const char *cmd_end = cmdline;
    while (*cmd_end && !isspace(*cmd_end)) {
        cmd_end++;
    }

    size_t cmd_len = cmd_end - cmdline;
    *cmd_name = malloc(cmd_len + 1);
    if (!*cmd_name) return 0;

    memcpy(*cmd_name, cmdline, cmd_len);
    (*cmd_name)[cmd_len] = '\0';

    /* Skip whitespace after command */
    while (*cmd_end && isspace(*cmd_end)) {
        cmd_end++;
    }

    /* Rest is arguments */
    if (*cmd_end) {
        *args = strdup(cmd_end);
    } else {
        *args = NULL;
    }

    return 1;
}

/* Find command definition (builtin or dynamic) */
static command_def_t* find_command(const char *name) {
    /* Check built-in commands */
    for (int i = 0; builtin_commands[i].name != NULL; i++) {
        if (strcmp(builtin_commands[i].name, name) == 0) {
            return &builtin_commands[i];
        }
    }

    /* Check dynamic (Lua-registered) commands */
    for (int i = 0; i < dynamic_command_count; i++) {
        if (strcmp(dynamic_commands[i].name, name) == 0) {
            return &dynamic_commands[i];
        }
    }

    return NULL;
}

/* ======================== Command Execution ======================== */

int command_execute(editor_ctx_t *ctx, const char *cmdline) {
    char *cmd_name = NULL;
    char *args = NULL;

    if (!parse_command(cmdline, &cmd_name, &args)) {
        editor_set_status_msg(ctx, "Empty command");
        return 0;
    }

    /* Add to history */
    command_history_add(cmdline);

    /* Find command handler */
    command_def_t *cmd = find_command(cmd_name);
    if (!cmd) {
        editor_set_status_msg(ctx, "Unknown command: %s", cmd_name);
        free(cmd_name);
        free(args);
        return 0;
    }

    /* Validate argument count */
    int arg_count = args ? 1 : 0;  /* Simplified: just check if args exist
 */
    if (arg_count < cmd->min_args ||
        (cmd->max_args >= 0 && arg_count > cmd->max_args)) {
        editor_set_status_msg(ctx, "Invalid arguments for :%s", cmd_name);
        free(cmd_name);
        free(args);
        return 0;
    }

    /* Execute command */
    int result = cmd->handler(ctx, args);

    free(cmd_name);
    free(args);
    return result;
}

/* ======================== Command Mode Input Handling
======================== */

void command_mode_handle_key(editor_ctx_t *ctx, int fd, int key) {
    switch (key) {
        case ESC:
            command_mode_exit(ctx);
            break;

        case ENTER:
            /* Execute command */
            if (ctx->cmd_length > 1) {  /* More than just ':' */
                command_execute(ctx, ctx->cmd_buffer);
            }
            command_mode_exit(ctx);
            break;

        case BACKSPACE:
        case CTRL_H:
        case DEL_KEY:
            if (ctx->cmd_cursor_pos > 1) {  /* Can't delete ':' */
                /* Remove character before cursor */
                memmove(ctx->cmd_buffer + ctx->cmd_cursor_pos - 1,
                        ctx->cmd_buffer + ctx->cmd_cursor_pos,
                        ctx->cmd_length - ctx->cmd_cursor_pos + 1);
                ctx->cmd_cursor_pos--;
                ctx->cmd_length--;
                editor_set_status_msg(ctx, "%s", ctx->cmd_buffer);
            } else {
                /* Backspace on empty command exits command mode */
                command_mode_exit(ctx);
            }
            break;

        case ARROW_LEFT:
            if (ctx->cmd_cursor_pos > 1) {
                ctx->cmd_cursor_pos--;
            }
            break;

        case ARROW_RIGHT:
            if (ctx->cmd_cursor_pos < ctx->cmd_length) {
                ctx->cmd_cursor_pos++;
            }
            break;

        case ARROW_UP:
            /* Previous command in history */
            if (ctx->cmd_history_index > 0) {
                ctx->cmd_history_index--;
                const char *hist =
command_history_get(ctx->cmd_history_index);
                if (hist) {
                    strncpy(ctx->cmd_buffer, hist, sizeof(ctx->cmd_buffer)
 - 1);
                    ctx->cmd_length = strlen(ctx->cmd_buffer);
                    ctx->cmd_cursor_pos = ctx->cmd_length;
                    editor_set_status_msg(ctx, "%s", ctx->cmd_buffer);
                }
            }
            break;

        case ARROW_DOWN:
            /* Next command in history */
            if (ctx->cmd_history_index < command_history_len - 1) {
                ctx->cmd_history_index++;
                const char *hist =
command_history_get(ctx->cmd_history_index);
                if (hist) {
                    strncpy(ctx->cmd_buffer, hist, sizeof(ctx->cmd_buffer)
 - 1);
                    ctx->cmd_length = strlen(ctx->cmd_buffer);
                    ctx->cmd_cursor_pos = ctx->cmd_length;
                    editor_set_status_msg(ctx, "%s", ctx->cmd_buffer);
                }
            } else {
                /* At end of history, clear command */
                ctx->cmd_buffer[0] = ':';
                ctx->cmd_buffer[1] = '\0';
                ctx->cmd_length = 1;
                ctx->cmd_cursor_pos = 1;
                ctx->cmd_history_index = command_history_len;
                editor_set_status_msg(ctx, ":");
            }
            break;

        case CTRL_U:
            /* Clear command line */
            ctx->cmd_buffer[0] = ':';
            ctx->cmd_buffer[1] = '\0';
            ctx->cmd_length = 1;
            ctx->cmd_cursor_pos = 1;
            editor_set_status_msg(ctx, ":");
            break;

        default:
            /* Regular character input */
            if (isprint(key) && ctx->cmd_length <
(int)sizeof(ctx->cmd_buffer) - 1) {
                /* Insert character at cursor */
                memmove(ctx->cmd_buffer + ctx->cmd_cursor_pos + 1,
                        ctx->cmd_buffer + ctx->cmd_cursor_pos,
                        ctx->cmd_length - ctx->cmd_cursor_pos + 1);
                ctx->cmd_buffer[ctx->cmd_cursor_pos] = key;
                ctx->cmd_cursor_pos++;
                ctx->cmd_length++;
                editor_set_status_msg(ctx, "%s", ctx->cmd_buffer);
            }
            break;
    }
}

/* ======================== Built-in Command Implementations
======================== */

int cmd_write(editor_ctx_t *ctx, const char *args) {
    const char *filename = args ? args : ctx->filename;

    if (!filename) {
        editor_set_status_msg(ctx, "No filename");
        return 0;
    }

    /* Save file using existing editor_save() */
    int len = editor_save(ctx);
    if (len >= 0) {
        editor_set_status_msg(ctx, "\"%s\" %dL written", filename,
ctx->numrows);
        ctx->dirty = 0;
        return 1;
    } else {
        editor_set_status_msg(ctx, "Error writing file");
        return 0;
    }
}

int cmd_quit(editor_ctx_t *ctx, const char *args) {
    (void)args;

    if (ctx->dirty) {
        editor_set_status_msg(ctx, "Unsaved changes! Use :q! to force
quit");
        return 0;
    }

    /* Exit program */
    terminal_clear_screen();
    exit(0);
}

int cmd_force_quit(editor_ctx_t *ctx, const char *args) {
    (void)ctx;
    (void)args;

    /* Exit without checking dirty flag */
    terminal_clear_screen();
    exit(0);
}

int cmd_write_quit(editor_ctx_t *ctx, const char *args) {
    /* Save first */
    if (!cmd_write(ctx, args)) {
        return 0;
    }

    /* Then quit */
    terminal_clear_screen();
    exit(0);
}

int cmd_help(editor_ctx_t *ctx, const char *args) {
    if (!args) {
        /* Show general help */
        editor_set_status_msg(ctx,
            "Commands: :w :q :wq :set :help <cmd> | Ctrl-F=find
Ctrl-S=save");
        return 1;
    }

    /* Show help for specific command */
    command_def_t *cmd = find_command(args);
    if (cmd) {
        editor_set_status_msg(ctx, ":%s - %s", cmd->name, cmd->help);
        return 1;
    } else {
        editor_set_status_msg(ctx, "Unknown command: %s", args);
        return 0;
    }
}

int cmd_set(editor_ctx_t *ctx, const char *args) {
    if (!args) {
        /* Show current settings */
        editor_set_status_msg(ctx, "Options: number, wrap");
        return 1;
    }

    /* Parse "set option" or "set option=value" */
    char option[64] = {0};
    char value[64] = {0};

    if (sscanf(args, "%63s = %63s", option, value) == 2 ||
        sscanf(args, "%63s=%63s", option, value) == 2) {
        /* Set option to value */
        editor_set_status_msg(ctx, "Set %s=%s (not implemented yet)",
option, value);
        return 1;
    } else if (sscanf(args, "%63s", option) == 1) {
        /* Toggle boolean option or show value */
        if (strcmp(option, "number") == 0) {
            /* Toggle line numbers (when implemented) */
            editor_set_status_msg(ctx, "Line numbers (not implemented
yet)");
            return 1;
        } else if (strcmp(option, "wrap") == 0) {
            ctx->word_wrap = !ctx->word_wrap;
            editor_set_status_msg(ctx, "Word wrap: %s",
                                 ctx->word_wrap ? "on" : "off");
            return 1;
        } else {
            editor_set_status_msg(ctx, "Unknown option: %s", option);
            return 0;
        }
    }

    return 0;
}

int cmd_edit(editor_ctx_t *ctx, const char *args) {
    if (!args) {
        editor_set_status_msg(ctx, "Filename required");
        return 0;
    }

    if (ctx->dirty) {
        editor_set_status_msg(ctx, "Unsaved changes! Save first or use
:q!");
        return 0;
    }

    /* Load new file */
    editor_open(ctx, args);
    editor_set_status_msg(ctx, "\"%s\" loaded", args);
    return 1;
}

/* ======================== Dynamic Command Registration (for Lua)
======================== */

int command_register(const char *name, command_handler_t handler,
                     const char *help, int min_args, int max_args) {
    if (dynamic_command_count >= MAX_DYNAMIC_COMMANDS) {
        return 0;  /* Registry full */
    }

    /* Check if command already exists */
    if (find_command(name)) {
        return 0;  /* Can't override built-in or existing command */
    }

    /* Register new command */
    dynamic_commands[dynamic_command_count].name = strdup(name);
    dynamic_commands[dynamic_command_count].handler = handler;
    dynamic_commands[dynamic_command_count].help = strdup(help);
    dynamic_commands[dynamic_command_count].min_args = min_args;
    dynamic_commands[dynamic_command_count].max_args = max_args;
    dynamic_command_count++;

    return 1;
}

void command_unregister_all_dynamic(void) {
    for (int i = 0; i < dynamic_command_count; i++) {
        free((char*)dynamic_commands[i].name);
        free((char*)dynamic_commands[i].help);
    }
    dynamic_command_count = 0;
}
```

1. Integration with Modal System

Update `src/loki_modal.c:`

```c
#include "loki_command.h"

/* Add MODE_COMMAND to the main dispatch */
void modal_process_keypress(editor_ctx_t *ctx, int fd) {
    switch (ctx->mode) {
        case MODE_NORMAL:
            process_normal_mode(ctx, fd);
            break;
        case MODE_INSERT:
            process_insert_mode(ctx, fd);
            break;
        case MODE_VISUAL:
            process_visual_mode(ctx, fd);
            break;
        case MODE_COMMAND:  /* NEW */
            {
                int c = terminal_read_key(fd);
                command_mode_handle_key(ctx, fd, c);
            }
            break;
    }
}

/* In process_normal_mode(), add ':' key handler */
static void process_normal_mode(editor_ctx_t *ctx, int fd) {
    int c = terminal_read_key(fd);

    switch (c) {
        /* ... existing cases ... */

        case ':':  /* NEW - Enter command mode */
            command_mode_enter(ctx);
            break;

        /* ... rest of cases ... */
    }
}
```

1. Update Editor Context

Add to `loki_internal.h`:

```c
struct editor_ctx {
    /* ... existing fields ... */

    /* Command mode state */
    char cmd_buffer[256];      /* Command input buffer */
    int cmd_length;            /* Length of command */
    int cmd_cursor_pos;        /* Cursor position in command */
    int cmd_history_index;     /* Current history position */
};

5. Lua Integration

Allow Lua scripts to register custom commands (src/loki_lua.c):

/* Wrapper for Lua command handlers */
static int lua_command_wrapper(editor_ctx_t *ctx, const char *args) {
    /* Get Lua state from context */
    lua_State *L = ctx->L;

    /* Retrieve handler function from registry */
    /* (Stored when command was registered) */
    lua_getglobal(L, "loki_command_handlers");
    lua_getfield(L, -1, /* command name */);

    /* Call Lua handler with args */
    lua_pushstring(L, args ? args : "");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        editor_set_status_msg(ctx, "Error: %s", lua_tostring(L, -1));
        return 0;
    }

    /* Get return value (1 = success, 0 = failure) */
    int result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
}

/* loki.register_command(name, handler, help) */
static int lua_loki_register_command(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);  /* Handler function */
    const char *help = luaL_optstring(L, 3, "Custom command");

    /* Store handler in registry */
    lua_getglobal(L, "loki_command_handlers");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "loki_command_handlers");
        lua_getglobal(L, "loki_command_handlers");
    }
    lua_pushvalue(L, 2);  /* Push handler function */
    lua_setfield(L, -2, name);

    /* Register C wrapper */
    command_register(name, lua_command_wrapper, help, 0, -1);

    return 0;
}
```

Example Lua usage:

```lua
-- .loki/init.lua

-- Register custom :timestamp command
loki.register_command("timestamp", function(args)
    local ts = os.date("%Y-%m-%d %H:%M:%S")
    loki.insert_text(ts)
    loki.status("Inserted timestamp")
    return true  -- Success
end, "Insert current timestamp")

-- Register :count command
loki.register_command("count", function(args)
    local pattern = args or "."
    local count = 0
    for i = 0, loki.get_lines() - 1 do
        local line = loki.get_line(i)
        if line:match(pattern) then
            count = count + 1
        end
    end
    loki.status(string.format("Found %d lines matching '%s'", count,
pattern))
    return true
end, "Count lines matching pattern")

-- Now in editor:
-- :timestamp    --> Inserts "2025-01-12 14:30:45"
-- :count TODO   --> "Found 7 lines matching 'TODO'"
```

1. Testing Strategy

New test file: tests/test_command.c

```c
#include "test_framework.h"
#include "loki_internal.h"
#include "loki_command.h"

TEST(command_parse_simple) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    command_mode_enter(&ctx);
    ASSERT_EQ(ctx.mode, MODE_COMMAND);
    ASSERT_EQ(ctx.cmd_length, 1);
    ASSERT_STR_EQ(ctx.cmd_buffer, ":");

    editor_ctx_free(&ctx);
}

TEST(command_execute_quit) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);
    ctx.dirty = 0;

    /* This would exit, so we can't actually test it */
    /* Instead test that unsaved changes block quit */
    ctx.dirty = 1;
    int result = command_execute(&ctx, ":q");
    ASSERT_EQ(result, 0);  /* Should fail */

    editor_ctx_free(&ctx);
}

TEST(command_execute_set_wrap) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);
    ctx.word_wrap = 0;

    command_execute(&ctx, ":set wrap");
    ASSERT_EQ(ctx.word_wrap, 1);

    command_execute(&ctx, ":set wrap");
    ASSERT_EQ(ctx.word_wrap, 0);

    editor_ctx_free(&ctx);
}

TEST(command_history) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    command_execute(&ctx, ":w test.txt");
    command_execute(&ctx, ":set wrap");
    command_execute(&ctx, ":help");

    /* History should contain 3 entries */
    /* (Testing internal history API) */

    editor_ctx_free(&ctx);
}

TEST(command_unknown) {
    editor_ctx_t ctx;
    editor_ctx_init(&ctx);

    int result = command_execute(&ctx, ":foobar");
    ASSERT_EQ(result, 0);  /* Should fail */

    editor_ctx_free(&ctx);
}
```

1. Build System Updates

CMakeLists.txt:

```cmake
add_library(libloki ${LOKI_LIBRARY_TYPE}
    src/loki_core.c
    src/loki_lua.c
    src/loki_editor.c
    src/loki_languages.c
    src/loki_selection.c
    src/loki_search.c
    src/loki_modal.c
    src/loki_terminal.c
    src/loki_command.c      # NEW
)

# Add test for command mode
add_executable(test_command tests/test_command.c)
target_include_directories(test_command PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_command PRIVATE libloki test_framework)
add_test(NAME test_command COMMAND test_command)
```

Summary: Implementation Decision

Yes, create a separate loki_command.c file (~300-400 lines) because:

1. Clean separation: Command parsing/execution is distinct from modal
navigation
2. Extensibility: Easy to add new commands without modifying modal logic
3. Lua integration: Natural place for custom command registration
4. Testing: Can unit test command parser independently
5. Growth: Command systems tend to expand (:substitute, :global, :buffers,

 etc.)

Architecture:
loki_modal.c       --> Handles mode switching (detects ':' key, switches
to MODE_COMMAND)
loki_command.c     --> Handles command input, parsing, execution
loki_lua.c         --> Bridges Lua custom commands to C command system

Estimated size: ~350 lines for basic implementation with 10 built-in
commands

Would you like me to implement a working prototype of this command system?
 I can create the full loki_command.c file with tests and integration.

> Let's do loki_command.c (Option B)
