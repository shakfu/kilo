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
static void repl_print_help(void);
static void repl_init_history(repl_history_config *config);
static void repl_shutdown_history(repl_history_config *config);
static void repl_add_history_entry(const char *line, repl_history_config *config);
static char *repl_read_line(const char *prompt);
static void repl_free_line(char *line);
static void repl_show_highlight(const char *prompt, const char *line);
static char *repl_highlight_lua(const char *prompt, const char *line);
static void repl_append_colored(char **buf, size_t *len, size_t *cap, const char *text);
static void repl_append_char(char **buf, size_t *len, size_t *cap, char ch);
static bool repl_is_lua_keyword(const char *word, size_t len);

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

    lua_State *L = loki_lua_bootstrap(&opts);
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

static int run_repl(lua_State *L, repl_history_config *history) {
    printf("loki-repl %s (%s). Type :help for commands.\n", LOKI_VERSION, loki_lua_runtime());

#if defined(LOKI_HAVE_EDITLINE)
    printf("Line editing: editline (history enabled)\n");
#elif defined(LOKI_HAVE_READLINE)
    printf("Line editing: readline (history enabled)\n");
#else
    printf("Line editing: basic (no history)\n");
#endif

    int status = 0;
    const char *prompt = "loki> ";

    while (1) {
        char *line = repl_read_line(prompt);
        if (!line) {
            putchar('\n');
            break;
        }

        if (strcmp(line, "quit") == 0 || strcmp(line, ":quit") == 0) {
            repl_free_line(line);
            break;
        }

        if (strcmp(line, ":help") == 0 || strcmp(line, "help") == 0) {
            repl_show_highlight(prompt, line);
            repl_print_help();
            repl_free_line(line);
            continue;
        }

        if (line[0] != '\0') {
            repl_show_highlight(prompt, line);
            repl_add_history_entry(line, history);
            if (execute_lua_line(L, line) != 0) {
                status = 1;
            }
        }

        repl_free_line(line);
    }

    return status;
}

static void repl_print_help(void) {
    printf("Commands:\n");
    printf("  help / :help    Show this help message\n");
    printf("  quit / :quit    Exit the repl\n");
    printf("\n");
    printf("Any other input is executed as Lua code using the shared loki runtime.\n");
    printf("Use --trace-http on startup (or set KILO_DEBUG=1) for verbose async logs.\n");
}

/* ------------------------------------------------------------------------- */
/* History and line input helpers                                           */
/* ------------------------------------------------------------------------- */

static bool repl_is_tty(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

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
/* Syntax highlighting                                                      */
/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/* Lua namespace helpers                                                    */
/* ------------------------------------------------------------------------- */


