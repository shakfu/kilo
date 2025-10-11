#include "loki/lua.h"
#include "loki/version.h"

#include <lauxlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Line editing library support (editline or readline) */
#if defined(LOKI_HAVE_EDITLINE)
    /* Editline (libedit) with readline compatibility */
    #include <editline/readline.h>
    #define LOKI_HAVE_LINEEDIT 1
#elif defined(LOKI_HAVE_READLINE)
    /* GNU Readline */
    #include <readline/history.h>
    #include <readline/readline.h>
    #define LOKI_HAVE_LINEEDIT 1
#endif

static void loki_setenv_override(const char *key, const char *value) {
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

struct repl_history_config {
    const char *path;
    bool dirty;
};

typedef struct repl_history_config repl_history_config;

static void print_usage(void);
static int run_script(lua_State *L, const char *path);
static int run_repl(lua_State *L, repl_history_config *history);
static int execute_lua_line(lua_State *L, const char *line);
static bool is_lua_complete(lua_State *L, const char *code);
static void repl_print_help(void);
static void repl_init_history(repl_history_config *config);
static void repl_shutdown_history(repl_history_config *config);
static void repl_add_history_entry(const char *line, repl_history_config *config);
static char *repl_read_line(const char *prompt);
static void repl_free_line(char *line);
static char *repl_edit_external(const char *initial_content);
#ifdef LOKI_HAVE_LINEEDIT
static void repl_init_completion(lua_State *L);
#endif
#ifndef LOKI_HAVE_LINEEDIT
static void repl_show_highlight(const char *prompt, const char *line);
static char *repl_highlight_lua(const char *prompt, const char *line);
static void repl_append_colored(char **buf, size_t *len, size_t *cap, const char *text);
static void repl_append_char(char **buf, size_t *len, size_t *cap, char ch);
static bool repl_is_lua_keyword(const char *word, size_t len);
#endif

int main(int argc, char **argv) {
    int trace_http = 0;
    const char *script_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            printf("loki-repl %s (%s)\n", LOKI_VERSION, loki_lua_runtime());
            return 0;
        }
        if (strcmp(arg, "--trace-http") == 0) {
            trace_http = 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage();
            return 64;
        }
        script_path = arg;
        if (i + 1 < argc) {
            fprintf(stderr, "Ignoring extra arguments after %s\n", script_path);
        }
        break;
    }

    if (trace_http) {
        loki_setenv_override("KILO_DEBUG", "1");
    }

    struct loki_lua_opts opts = {
        .bind_editor = 0,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = NULL,
        .reporter_userdata = NULL,
    };

    lua_State *L = loki_lua_bootstrap(NULL, &opts);
    if (!L) {
        fprintf(stderr, "Failed to initialize Lua runtime (%s)\n", loki_lua_runtime());
        return 1;
    }

    repl_history_config history = {
        .path = ".loki/repl_history",
        .dirty = false,
    };

    repl_init_history(&history);

    loki_lua_install_namespaces(L);

    int status = 0;
    if (script_path) {
        status = run_script(L, script_path);
    } else {
        status = run_repl(L, &history);
    }

    repl_shutdown_history(&history);
    lua_close(L);
    return status;
}

static void print_usage(void) {
    printf("Usage: loki-repl [options] [script.lua]\n");
    printf("Options:\n");
    printf("  --help           Show this message\n");
    printf("  --version        Print version information\n");
    printf("  --trace-http     Enable verbose async HTTP logging\n");
}

static int run_script(lua_State *L, const char *path) {
    int base = lua_gettop(L);
    if (luaL_loadfile(L, path) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "Error loading %s: %s\n", path, err ? err : "unknown");
        lua_settop(L, base);
        return 1;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "Error running %s: %s\n", path, err ? err : "unknown");
        lua_settop(L, base);
        return 1;
    }

    /* Poll for async HTTP requests in a loop (5 seconds max) */
    for (int i = 0; i < 50; i++) {
        loki_poll_async_http(L);
        usleep(100000);  /* 100ms */
    }

    lua_settop(L, base);
    return 0;
}

static int execute_lua_line(lua_State *L, const char *line) {
    int base = lua_gettop(L);

    if (luaL_loadstring(L, line) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "%s\n", err ? err : "syntax error");
        lua_settop(L, base);
        return 1;
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "%s\n", err ? err : "runtime error");
        lua_settop(L, base);
        return 1;
    }

    int results = lua_gettop(L) - base;
    if (results > 0) {
        for (int i = 1; i <= results; i++) {
            luaL_tolstring(L, base + i, NULL);
            const char *out = lua_tostring(L, -1);
            printf("%s%s", out ? out : "nil", (i == results) ? "\n" : "\t");
            lua_pop(L, 1);
        }
    }

    lua_settop(L, base);
    return 0;
}

/* Check if Lua code is syntactically complete */
static bool is_lua_complete(lua_State *L, const char *code) {
    int base = lua_gettop(L);
    int status = luaL_loadstring(L, code);

    if (status == LUA_OK) {
        lua_pop(L, 1);  /* Pop the compiled chunk */
        return true;
    }

    if (status == LUA_ERRSYNTAX) {
        const char *msg = lua_tostring(L, -1);
        /* Check for "unexpected <eof>" or "unfinished" which indicate incomplete input */
        bool incomplete = (msg && (strstr(msg, "<eof>") != NULL || strstr(msg, "unfinished") != NULL));
        lua_pop(L, 1);  /* Pop the error message */
        return !incomplete;
    }

    lua_settop(L, base);  /* Pop any other errors */
    return true;  /* Other errors mean it's "complete" but has real errors */
}

static int run_repl(lua_State *L, repl_history_config *history) {
    printf("loki-repl %s (%s). Type :help for commands.\n", LOKI_VERSION, loki_lua_runtime());

#if defined(LOKI_HAVE_EDITLINE)
    printf("Line editing: editline (history + tab completion + multi-line enabled)\n");
#elif defined(LOKI_HAVE_READLINE)
    printf("Line editing: readline (history + tab completion + multi-line enabled)\n");
#else
    printf("Line editing: basic (multi-line enabled)\n");
#endif

#ifdef LOKI_HAVE_LINEEDIT
    /* Initialize tab completion */
    repl_init_completion(L);
#endif

    int status = 0;
    const char *main_prompt = "loki> ";
    const char *cont_prompt = "cont> ";

    /* Multi-line buffer */
    char *buffer = NULL;
    size_t buffer_len = 0;
    size_t buffer_cap = 0;

    while (1) {
        /* Poll async HTTP requests */
        loki_poll_async_http(L);

        const char *prompt = (buffer_len > 0) ? cont_prompt : main_prompt;
        char *line = repl_read_line(prompt);

        if (!line) {
            putchar('\n');
            break;
        }

        /* Check for quit commands (only at main prompt) */
        if (buffer_len == 0 && (strcmp(line, "quit") == 0 || strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0)) {
            repl_free_line(line);
            break;
        }

        /* Check for help commands (only at main prompt) */
        if (buffer_len == 0 && (strcmp(line, ":help") == 0 || strcmp(line, "help") == 0)) {
#ifndef LOKI_HAVE_LINEEDIT
            /* Only show highlighting with basic getline (not readline/editline) */
            repl_show_highlight(prompt, line);
#endif
            repl_print_help();
            repl_free_line(line);
            continue;
        }

        /* Check for edit command */
        if (strcmp(line, ":edit") == 0 || strcmp(line, ":e") == 0) {
            repl_free_line(line);

            /* Open editor with current buffer content (if any) */
            char *content = repl_edit_external(buffer_len > 0 ? buffer : NULL);
            if (content && content[0]) {
                /* Execute the content from editor */
                repl_add_history_entry(content, history);
                if (execute_lua_line(L, content) != 0) {
                    status = 1;
                }
                free(content);
            }

            /* Reset buffer */
            buffer_len = 0;
            if (buffer) buffer[0] = '\0';
            continue;
        }

        /* Append line to buffer */
        size_t line_len = strlen(line);
        size_t needed = buffer_len + line_len + 2;  /* +1 for newline, +1 for null */

        if (needed > buffer_cap) {
            buffer_cap = needed * 2;
            buffer = realloc(buffer, buffer_cap);
            if (!buffer) {
                fprintf(stderr, "Out of memory\n");
                repl_free_line(line);
                return 1;
            }
        }

        if (buffer_len > 0) {
            buffer[buffer_len++] = '\n';
        }
        memcpy(buffer + buffer_len, line, line_len);
        buffer_len += line_len;
        buffer[buffer_len] = '\0';

        repl_free_line(line);

        /* Check if code is complete */
        if (buffer[0] != '\0' && is_lua_complete(L, buffer)) {
#ifndef LOKI_HAVE_LINEEDIT
            /* Only show highlighting with basic getline (not readline/editline) */
            repl_show_highlight(main_prompt, buffer);
#endif
            repl_add_history_entry(buffer, history);

            if (execute_lua_line(L, buffer) != 0) {
                status = 1;
            }

            /* Reset buffer */
            buffer_len = 0;
            buffer[0] = '\0';
        }
        /* If incomplete, loop will continue with cont_prompt */
    }

    free(buffer);
    return status;
}

static void repl_print_help(void) {
    printf("Commands:\n");
    printf("  help / :help    Show this help message\n");
    printf("  quit / :quit    Exit the repl\n");
    printf("  :q              Shortcut for :quit\n");
    printf("  edit / :edit    Open $EDITOR to write/edit multi-line code\n");
    printf("  :e              Shortcut for :edit\n");
    printf("\n");
    printf("Features:\n");
    printf("  Multi-line input: Incomplete Lua code (functions, tables, etc.) will\n");
    printf("                    automatically show a continuation prompt (cont>)\n");
    printf("  External editor:  Type :edit to open your preferred editor ($EDITOR or vi)\n");
    printf("                    for complex code. Content will be executed on save & exit.\n");
#ifdef LOKI_HAVE_LINEEDIT
    printf("  Tab completion:   Press TAB to complete Lua keywords, globals, and loki.* API\n");
    printf("  History:          Use Up/Down arrows to navigate previous commands\n");
    printf("                    Ctrl-R: Reverse search through history\n");
    printf("                    Ctrl-_: Undo last edit\n");
#endif
    printf("\n");
    printf("Any other input is executed as Lua code using the shared loki runtime.\n");
    printf("Use --trace-http on startup (or set KILO_DEBUG=1) for verbose async logs.\n");
}

/* ------------------------------------------------------------------------- */
/* History and line input helpers                                           */
/* ------------------------------------------------------------------------- */

#ifndef LOKI_HAVE_LINEEDIT
static bool repl_is_tty(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}
#endif

static void repl_init_history(repl_history_config *config) {
    if (!config || !config->path) return;

#ifdef LOKI_HAVE_LINEEDIT
    using_history();
    if (read_history(config->path) == 0) {
        stifle_history(1000);
    }
#else
    (void)config;
#endif
}

static void repl_shutdown_history(repl_history_config *config) {
    if (!config || !config->path || !config->dirty) return;
#ifdef LOKI_HAVE_LINEEDIT
    write_history(config->path);
#endif
}

static void repl_add_history_entry(const char *line, repl_history_config *config) {
    if (!line || !*line) return;
#ifdef LOKI_HAVE_LINEEDIT
    add_history(line);
    if (config) config->dirty = true;
#else
    (void)config;
#endif
}

static char *repl_read_line(const char *prompt) {
#ifdef LOKI_HAVE_LINEEDIT
    return readline(prompt);
#else
    (void)prompt;
    size_t cap = 0;
    char *line = NULL;
    ssize_t nread = getline(&line, &cap, stdin);
    if (nread < 0) {
        free(line);
        return NULL;
    }
    if (nread > 0 && line[nread - 1] == '\n') {
        line[nread - 1] = '\0';
    }
    return line;
#endif
}

static void repl_free_line(char *line) {
    free(line);
}

/* ------------------------------------------------------------------------- */
/* External editor integration                                              */
/* ------------------------------------------------------------------------- */

/* Edit code in external editor ($EDITOR or vi) */
static char *repl_edit_external(const char *initial_content) {
    /* Create temporary file */
    char tmpfile[] = "/tmp/loki_repl_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to create temporary file\n");
        return NULL;
    }

    /* Write initial content if provided */
    if (initial_content && initial_content[0]) {
        ssize_t written = write(fd, initial_content, strlen(initial_content));
        if (written < 0) {
            fprintf(stderr, "Error: Failed to write to temporary file\n");
            close(fd);
            unlink(tmpfile);
            return NULL;
        }
    }
    close(fd);

    /* Get editor from environment, default to vi */
    const char *editor = getenv("EDITOR");
    if (!editor || !editor[0]) {
        editor = getenv("VISUAL");
    }
    if (!editor || !editor[0]) {
        editor = "vi";
    }

    /* Build command and launch editor */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s %s", editor, tmpfile);

    printf("Opening editor: %s\n", editor);
    fflush(stdout);

    int status = system(cmd);
    if (status != 0) {
        fprintf(stderr, "Error: Editor exited with status %d\n", status);
        unlink(tmpfile);
        return NULL;
    }

    /* Read back the file content */
    FILE *f = fopen(tmpfile, "r");
    if (!f) {
        fprintf(stderr, "Error: Failed to reopen temporary file\n");
        unlink(tmpfile);
        return NULL;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "Error: Failed to get file size\n");
        fclose(f);
        unlink(tmpfile);
        return NULL;
    }

    if (size == 0) {
        /* Empty file - user cleared content or didn't write anything */
        fclose(f);
        unlink(tmpfile);
        return NULL;
    }

    /* Allocate buffer and read content */
    char *content = malloc(size + 1);
    if (!content) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        unlink(tmpfile);
        return NULL;
    }

    size_t nread = fread(content, 1, size, f);
    content[nread] = '\0';

    fclose(f);
    unlink(tmpfile);

    return content;
}

/* ------------------------------------------------------------------------- */
/* Tab completion (readline/editline only)                                 */
/* ------------------------------------------------------------------------- */

#ifdef LOKI_HAVE_LINEEDIT

/* Global Lua state for completion (set in run_repl) */
static lua_State *g_completion_L = NULL;

/* Get completions from Lua environment */
static char **get_lua_completions(lua_State *L, const char *text, int *count) {
    if (!L) return NULL;

    char **matches = NULL;
    *count = 0;
    size_t len = strlen(text);

    /* Lua keywords */
    const char *keywords[] = {
        "and", "break", "do", "else", "elseif", "end", "false",
        "for", "function", "goto", "if", "in", "local", "nil",
        "not", "or", "repeat", "return", "then", "true", "until", "while",
        NULL
    };

    for (int i = 0; keywords[i]; i++) {
        if (strncmp(keywords[i], text, len) == 0) {
            matches = realloc(matches, sizeof(char*) * (*count + 1));
            matches[(*count)++] = strdup(keywords[i]);
        }
    }

    /* loki.* API functions */
    const char *loki_api[] = {
        "loki.status", "loki.get_lines", "loki.get_line",
        "loki.get_cursor", "loki.insert_text", "loki.get_filename",
        "loki.async_http", "loki.repl.register",
        NULL
    };

    for (int i = 0; loki_api[i]; i++) {
        if (strncmp(loki_api[i], text, len) == 0) {
            matches = realloc(matches, sizeof(char*) * (*count + 1));
            matches[(*count)++] = strdup(loki_api[i]);
        }
    }

    /* Namespaced functions from init.lua */
    const char *namespaced[] = {
        "editor.count_lines", "editor.cursor", "editor.timestamp", "editor.first_line",
        "ai.complete", "ai.explain",
        "test.http",
        NULL
    };

    for (int i = 0; namespaced[i]; i++) {
        if (strncmp(namespaced[i], text, len) == 0) {
            matches = realloc(matches, sizeof(char*) * (*count + 1));
            matches[(*count)++] = strdup(namespaced[i]);
        }
    }

    /* Lua globals from _G table */
    lua_getglobal(L, "_G");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char *key = lua_tostring(L, -2);
                if (key && strncmp(key, text, len) == 0) {
                    /* Avoid duplicates with keywords */
                    int is_dup = 0;
                    for (int i = 0; i < *count; i++) {
                        if (strcmp(matches[i], key) == 0) {
                            is_dup = 1;
                            break;
                        }
                    }
                    if (!is_dup) {
                        matches = realloc(matches, sizeof(char*) * (*count + 1));
                        matches[(*count)++] = strdup(key);
                    }
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    return matches;
}

/* Completion generator for readline */
static char *completion_generator(const char *text, int state) {
    static char **matches = NULL;
    static int match_count = 0;
    static int match_index = 0;

    if (!state) {
        /* First call - generate matches */
        match_index = 0;
        if (matches) {
            for (int i = 0; i < match_count; i++) {
                free(matches[i]);
            }
            free(matches);
            matches = NULL;
        }
        matches = get_lua_completions(g_completion_L, text, &match_count);
    }

    /* Return next match */
    if (matches && match_index < match_count) {
        return strdup(matches[match_index++]);
    }

    /* Cleanup after last match */
    if (matches) {
        for (int i = 0; i < match_count; i++) {
            free(matches[i]);
        }
        free(matches);
        matches = NULL;
        match_count = 0;
    }

    return NULL;
}

/* Completion entry point for readline/editline */
static char **loki_completion(const char *text, int start, int end) {
    (void)end;

    /* Disable default filename completion */
    rl_attempted_completion_over = 1;

    /* Only complete at start of word */
    if (start > 0) {
        char prev = rl_line_buffer[start - 1];
        if (prev != ' ' && prev != '(' && prev != ',' && prev != '{' && prev != '[') {
            /* In middle of compound expression like "loki.get_lines" */
            /* Let it fall through to default word completion */
        }
    }

    return rl_completion_matches(text, completion_generator);
}

/* Initialize tab completion */
static void repl_init_completion(lua_State *L) {
    g_completion_L = L;

    /* Set readline completion function */
    rl_attempted_completion_function = loki_completion;

    /* Don't append space after completion */
    rl_completion_append_character = '\0';
}

#endif /* LOKI_HAVE_LINEEDIT */

/* ------------------------------------------------------------------------- */
/* Syntax highlighting (only used when not using editline/readline)        */
/* ------------------------------------------------------------------------- */

#ifndef LOKI_HAVE_LINEEDIT
static void repl_show_highlight(const char *prompt, const char *line) {
    if (!repl_is_tty()) return;
    if (!line || !*line) return;

    char *colored = repl_highlight_lua(prompt, line);
    if (!colored) return;

    /* Move cursor one line up, clear, print highlighted version */
    fputs("\033[1A\r\033[2K", stdout);
    fputs(colored, stdout);
    fputs("\033[0m\n", stdout);
    fflush(stdout);
    free(colored);
}

static void repl_append_ansi(char **buf, size_t *len, size_t *cap, const char *code) {
    repl_append_colored(buf, len, cap, code);
}

static void repl_append_char(char **buf, size_t *len, size_t *cap, char ch) {
    if (*len + 1 >= *cap) {
        *cap = *cap ? (*cap * 2) : 64;
        *buf = realloc(*buf, *cap);
        if (!*buf) return;
    }
    (*buf)[(*len)++] = ch;
}

static void repl_append_colored(char **buf, size_t *len, size_t *cap, const char *text) {
    size_t tlen = strlen(text);
    if (*len + tlen >= *cap) {
        while (*len + tlen >= *cap) {
            *cap = *cap ? (*cap * 2) : 64;
        }
        *buf = realloc(*buf, *cap);
        if (!*buf) return;
    }
    memcpy(*buf + *len, text, tlen);
    *len += tlen;
}

static bool repl_is_identifier_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '.';
}

static bool repl_is_lua_keyword(const char *word, size_t len) {
    static const char *keywords[] = {
        "and","break","do","else","elseif","end","false","for","function",
        "goto","if","in","local","nil","not","or","repeat","return",
        "then","true","until","while",
    };
    for (size_t i = 0; i < sizeof(keywords)/sizeof(keywords[0]); i++) {
        if (strlen(keywords[i]) == len && strncmp(keywords[i], word, len) == 0) {
            return true;
        }
    }
    return false;
}

static void repl_append_reset(char **buf, size_t *len, size_t *cap) {
    repl_append_ansi(buf, len, cap, "\033[0m");
}

static char *repl_highlight_lua(const char *prompt, const char *line) {
    size_t cap = 0, len = 0;
    char *out = NULL;

    if (prompt && *prompt) {
        repl_append_ansi(&out, &len, &cap, "\033[36m");
        repl_append_colored(&out, &len, &cap, prompt);
        repl_append_reset(&out, &len, &cap);
    }

    if (!line) line = "";

    size_t i = 0;
    size_t l = strlen(line);
    while (i < l) {
        if (line[i] == '-' && i + 1 < l && line[i + 1] == '-') {
            repl_append_ansi(&out, &len, &cap, "\033[90m");
            repl_append_colored(&out, &len, &cap, line + i);
            repl_append_reset(&out, &len, &cap);
            i = l;
            break;
        }

        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t start = i++;
            bool closed = false;
            while (i < l) {
                if (line[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (line[i] == quote) {
                    closed = true;
                    i++;
                    break;
                }
                i++;
            }
            repl_append_ansi(&out, &len, &cap, "\033[93m");
            repl_append_colored(&out, &len, &cap, line + start);
            if (!closed) {
                repl_append_reset(&out, &len, &cap);
                break;
            }
            repl_append_reset(&out, &len, &cap);
            continue;
        }

        if (isdigit((unsigned char)line[i])) {
            size_t start = i;
            while (i < l && (isdigit((unsigned char)line[i]) || line[i] == '.' || line[i] == 'x' || line[i] == 'X' || (line[i] >= 'a' && line[i] <= 'f') || (line[i] >= 'A' && line[i] <= 'F'))) {
                i++;
            }
            repl_append_ansi(&out, &len, &cap, "\033[35m");
            repl_append_colored(&out, &len, &cap, line + start);
            repl_append_reset(&out, &len, &cap);
            continue;
        }

        if (isalpha((unsigned char)line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < l && repl_is_identifier_char(line[i])) i++;
            size_t word_len = i - start;
            if (repl_is_lua_keyword(line + start, word_len)) {
                repl_append_ansi(&out, &len, &cap, "\033[95m");
                repl_append_colored(&out, &len, &cap, line + start);
                repl_append_reset(&out, &len, &cap);
                continue;
            }
            if (strncmp(line + start, "ai", word_len >= 2 ? 2 : word_len) == 0) {
                repl_append_ansi(&out, &len, &cap, "\033[96m");
                repl_append_colored(&out, &len, &cap, line + start);
                repl_append_reset(&out, &len, &cap);
                continue;
            }
            repl_append_colored(&out, &len, &cap, line + start);
            continue;
        }

        repl_append_char(&out, &len, &cap, line[i]);
        i++;
    }

    if (out) {
        repl_append_char(&out, &len, &cap, '\0');
    }

    return out;
}
#endif /* LOKI_HAVE_LINEEDIT */

/* ------------------------------------------------------------------------- */
/* Lua namespace helpers                                                    */
/* ------------------------------------------------------------------------- */


