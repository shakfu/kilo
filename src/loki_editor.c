/* loki_editor.c - Integration layer between editor core and Lua
 *
 * This file contains:
 * - Lua state management
 * - REPL state and functions
 * - Main editor loop with Lua integration
 * - Functions that bridge between pure C core and Lua bindings
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* Lua headers */
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/* Loki headers */
#include "loki/version.h"
#include "loki/editor.h"
#include "loki/core.h"
#include "loki/lua.h"
#include "loki_internal.h"

/* libcurl for async HTTP */
#include <curl/curl.h>

/* ======================= Async HTTP Structures =========================== */

/* Response buffer for async HTTP */
struct curl_response {
    char *data;
    size_t size;
};

/* Async HTTP request state */
typedef struct {
    CURL *easy_handle;
    CURLM *multi_handle;
    struct curl_slist *header_list;
    struct curl_response response;
    char *lua_callback;
    int failed;
    char error_buffer[CURL_ERROR_SIZE];
} async_http_request;

#define MAX_ASYNC_REQUESTS 10
#define MAX_HTTP_RESPONSE_SIZE (10 * 1024 * 1024)  /* 10MB limit */

static async_http_request *pending_requests[MAX_ASYNC_REQUESTS] = {0};
static int num_pending = 0;
static int curl_initialized = 0;

/* ======================== Helper Functions =============================== */

/* Lua status reporter - reports Lua errors to editor status bar */
static void loki_lua_status_reporter(const char *message, void *userdata) {
    (void)userdata;
    if (message && message[0] != '\0') {
        editor_set_status_msg("%s", message);
    }
}

/* ======================= Async HTTP Implementation ======================= */

/* Callback for curl to write received data */
static size_t kilo_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct curl_response *resp = (struct curl_response *)userp;

    /* Check size limit to prevent memory exhaustion */
    if (resp->size + realsize > MAX_HTTP_RESPONSE_SIZE) {
        /* Abort transfer - response too large */
        return 0;
    }

    char *ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) {
        /* Out of memory */
        return 0;
    }

    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->data[resp->size] = 0;

    return realsize;
}

/* Initialize curl globally */
static void init_curl(void) {
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = 1;
    }
}

/* Detect CA bundle path for SSL/TLS verification */
static const char *detect_ca_bundle_path(void) {
    /* Common CA bundle locations across different platforms */
    static const char *ca_paths[] = {
        "/etc/ssl/cert.pem",                     /* macOS */
        "/etc/ssl/certs/ca-certificates.crt",   /* Debian/Ubuntu/Gentoo */
        "/etc/pki/tls/certs/ca-bundle.crt",     /* Fedora/RHEL */
        "/etc/ssl/ca-bundle.pem",                /* OpenSUSE */
        "/etc/ssl/certs/ca-bundle.crt",         /* Old Red Hat */
        "/usr/local/share/certs/ca-root-nss.crt", /* FreeBSD */
        NULL
    };

    for (int i = 0; ca_paths[i] != NULL; i++) {
        if (access(ca_paths[i], R_OK) == 0) {
            return ca_paths[i];
        }
    }

    /* If no bundle found, let curl use its default */
    return NULL;
}

/* Cleanup curl globally */
static void cleanup_curl(void) {
    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = 0;
    }
}

/* Start an async HTTP request */
int start_async_http_request(const char *url, const char *method,
                                     const char *body, const char **headers,
                                     int num_headers, const char *lua_callback) {
    if (num_pending >= MAX_ASYNC_REQUESTS) {
        editor_set_status_msg("Too many pending requests");
        return -1;
    }

    init_curl();

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        if (!pending_requests[i]) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1;

    /* Allocate request structure */
    async_http_request *req = malloc(sizeof(async_http_request));
    if (!req) return -1;

    memset(req, 0, sizeof(async_http_request));
    req->response.data = malloc(1);
    if (!req->response.data) {
        free(req);
        return -1;
    }
    req->response.data[0] = '\0';
    req->response.size = 0;
    req->lua_callback = strdup(lua_callback);
    if (!req->lua_callback) {
        free(req->response.data);
        free(req);
        return -1;
    }

    /* Initialize curl */
    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        free(req->response.data);
        free(req->lua_callback);
        free(req);
        return -1;
    }

    req->multi_handle = curl_multi_init();
    if (!req->multi_handle) {
        curl_easy_cleanup(req->easy_handle);
        free(req->response.data);
        free(req->lua_callback);
        free(req);
        return -1;
    }

    /* Set curl options */
    curl_easy_setopt(req->easy_handle, CURLOPT_URL, url);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, kilo_curl_write_callback);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, &req->response);
    curl_easy_setopt(req->easy_handle, CURLOPT_ERRORBUFFER, req->error_buffer);
    curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT, 60L);  /* Increase to 60 seconds */
    curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT, 10L);  /* Connection timeout */
    curl_easy_setopt(req->easy_handle, CURLOPT_FOLLOWLOCATION, 1L);

    /* SSL/TLS settings - use system CA bundle */
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(req->easy_handle, CURLOPT_SSL_VERIFYHOST, 2L);

    /* Detect and set CA bundle path if available */
    const char *ca_bundle = detect_ca_bundle_path();
    if (ca_bundle) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CAINFO, ca_bundle);
    }
    /* Otherwise, let curl use its built-in defaults */

    /* Enable verbose output only if KILO_DEBUG is set */
    if (getenv("KILO_DEBUG")) {
        curl_easy_setopt(req->easy_handle, CURLOPT_VERBOSE, 1L);
    }

    /* Set method */
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, body);
        }
    }

    /* Set headers if provided */
    req->header_list = NULL;
    if (headers && num_headers > 0) {
        for (int i = 0; i < num_headers; i++) {
            req->header_list = curl_slist_append(req->header_list, headers[i]);
        }
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->header_list);
    }

    /* Add to multi handle */
    curl_multi_add_handle(req->multi_handle, req->easy_handle);

    /* Store in pending requests */
    pending_requests[slot] = req;
    num_pending++;

    return slot;
}

/* Check and process completed async HTTP requests */
void check_async_requests(editor_ctx_t *ctx, lua_State *L) {
    for (int i = 0; i < MAX_ASYNC_REQUESTS; i++) {
        async_http_request *req = pending_requests[i];
        if (!req) continue;

        /* Perform non-blocking work */
        int still_running = 0;
        curl_multi_perform(req->multi_handle, &still_running);

        if (still_running == 0) {
            /* Request completed - check for messages */
            int msgs_left = 0;
            CURLMsg *msg = NULL;

            while ((msg = curl_multi_info_read(req->multi_handle, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    /* Check if the transfer succeeded */
                    if (msg->data.result != CURLE_OK) {
                        req->failed = 1;
                        /* error_buffer should contain the error message */
                        if (req->error_buffer[0] == '\0') {
                            /* If error_buffer is empty, use curl_easy_strerror */
                            snprintf(req->error_buffer, CURL_ERROR_SIZE, "%s",
                                    curl_easy_strerror(msg->data.result));
                        }
                    }
                }
            }

            /* Get response code */
            long response_code = 0;
            curl_easy_getinfo(req->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

            /* Debug output for non-interactive mode */
            if (!ctx || !ctx->rawmode) {
                fprintf(stderr, "HTTP request completed: status=%ld, response_size=%zu\n",
                        response_code, req->response.size);

                /* Check for curl errors */
                if (req->failed) {
                    fprintf(stderr, "CURL error: %s\n", req->error_buffer);
                }

                /* Show response preview if available */
                if (req->response.data && req->response.size > 0) {
                    size_t preview_len = req->response.size > 200 ? 200 : req->response.size;
                    fprintf(stderr, "Response preview: %.*s%s\n",
                            (int)preview_len, req->response.data,
                            req->response.size > 200 ? "..." : "");
                } else {
                    fprintf(stderr, "No response data received\n");
                }
            }

            /* Check for HTTP errors */
            if (response_code >= 400) {
                char errmsg[256];
                snprintf(errmsg, sizeof(errmsg), "HTTP error %ld", response_code);
                editor_set_status_msg("%s", errmsg);
                if (!ctx || !ctx->rawmode) {
                    fprintf(stderr, "%s\n", errmsg);
                }
            }

            /* Call Lua callback with response */
            if (L && req->lua_callback) {
                lua_getglobal(L, req->lua_callback);
                if (lua_isfunction(L, -1)) {
                    /* Create response table */
                    lua_newtable(L);  /* Stack: function, table */

                    /* Set status field */
                    lua_pushinteger(L, (lua_Integer)response_code);
                    lua_setfield(L, -2, "status");

                    /* Set body field */
                    if (req->response.data && req->response.size > 0) {
                        lua_pushstring(L, req->response.data);
                    } else {
                        lua_pushnil(L);
                    }
                    lua_setfield(L, -2, "body");

                    /* Set error field */
                    if (req->failed && req->error_buffer[0] != '\0') {
                        lua_pushstring(L, req->error_buffer);
                    } else if (response_code >= 400) {
                        char errbuf[128];
                        snprintf(errbuf, sizeof(errbuf), "HTTP error %ld", response_code);
                        lua_pushstring(L, errbuf);
                    } else {
                        lua_pushnil(L);
                    }
                    lua_setfield(L, -2, "error");

                    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                        const char *err = lua_tostring(L, -1);
                        editor_set_status_msg("Lua callback error: %s", err);
                        /* Also print to stderr for non-interactive mode */
                        if (!ctx || !ctx->rawmode) {
                            fprintf(stderr, "Lua callback error: %s\n", err);
                        }
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
            }

            /* Cleanup */
            curl_multi_remove_handle(req->multi_handle, req->easy_handle);
            curl_easy_cleanup(req->easy_handle);
            curl_multi_cleanup(req->multi_handle);
            if (req->header_list) {
                curl_slist_free_all(req->header_list);
            }
            free(req->response.data);
            free(req->lua_callback);
            free(req);

            pending_requests[i] = NULL;
            num_pending--;
        }
    }
}

/* Update REPL layout when active/inactive state changes */
void editor_update_repl_layout(editor_ctx_t *ctx) {
    if (!ctx) return;
    int reserved = ctx->repl.active ? LUA_REPL_TOTAL_ROWS : 0;
    int available = ctx->screenrows_total;
    if (available > reserved) {
        ctx->screenrows = available - reserved;
    } else {
        ctx->screenrows = 1;
    }
    if (ctx->screenrows < 1) ctx->screenrows = 1;

    if (ctx->cy >= ctx->screenrows) {
        ctx->cy = ctx->screenrows - 1;
        if (ctx->cy < 0) ctx->cy = 0;
    }

    if (ctx->numrows > ctx->screenrows && ctx->rowoff > ctx->numrows - ctx->screenrows) {
        ctx->rowoff = ctx->numrows - ctx->screenrows;
    }
    if (ctx->numrows <= ctx->screenrows) {
        ctx->rowoff = 0;
    }
}

/* Toggle the Lua REPL focus */
static void exec_lua_command(editor_ctx_t *ctx, int fd) {
    (void)fd;
    if (!ctx || !ctx->L) {
        editor_set_status_msg("Lua not available");
        return;
    }
    int was_active = ctx->repl.active;
    ctx->repl.active = !ctx->repl.active;
    editor_update_repl_layout(ctx);
    if (ctx->repl.active) {
        ctx->repl.history_index = -1;
        editor_set_status_msg(
            "Lua REPL: Enter runs, ESC exits, Up/Down history, type 'help'");
        if (ctx->repl.log_len == 0) {
            lua_repl_append_log(ctx, "Type 'help' for built-in commands");
        }
    } else {
        if (was_active) {
            editor_set_status_msg("Lua REPL closed");
        }
    }
}

/* Run AI command in non-interactive mode */
static int run_ai_command(char *filename, const char *command) {
    init_editor(&E);

    /* Initialize Lua for AI commands */
    struct loki_lua_opts opts = {
        .bind_editor = 1,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = loki_lua_status_reporter,
        .reporter_userdata = NULL
    };

    lua_State *L = loki_lua_bootstrap(&E, &opts);
    if (!L) {
        fprintf(stderr, "Failed to initialize Lua\n");
        return 1;
    }

    /* Call Lua function for AI command execution */
    lua_getglobal(L, "run_ai_command");
    if (!lua_isfunction(L, -1)) {
        fprintf(stderr, "Error: run_ai_command function not found in Lua config\n");
        lua_close(L);
        return 1;
    }

    lua_pushstring(L, filename);
    lua_pushstring(L, command);

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "Error executing AI command: %s\n", err ? err : "unknown");
        lua_close(L);
        return 1;
    }

    int result = lua_tointeger(L, -1);
    lua_close(L);
    return result;
}

/* Apply Lua-based highlighting spans to a row */
static int lua_apply_span_table(editor_ctx_t *ctx, t_erow *row, int table_index) {
    if (!ctx || !ctx->L) return 0;
    lua_State *L = ctx->L;
    if (!lua_istable(L, table_index)) return 0;

    int applied = 0;
    size_t entries = lua_rawlen(L, table_index);

    for (size_t i = 1; i <= entries; i++) {
        lua_rawgeti(L, table_index, (lua_Integer)i);
        if (lua_type(L, -1) == LUA_TTABLE) {
            int start = 0;
            int stop = 0;
            int length = 0;
            int style = -1;

            lua_getfield(L, -1, "start");
            if (lua_isnumber(L, -1)) start = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "stop");
            if (lua_isnumber(L, -1)) stop = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "end");
            if (lua_isnumber(L, -1)) stop = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "length");
            if (lua_isnumber(L, -1)) length = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "style");
            if (lua_isstring(L, -1)) {
                style = hl_name_to_code(lua_tostring(L, -1));
            } else if (lua_isnumber(L, -1)) {
                style = (int)lua_tointeger(L, -1);
            }
            lua_pop(L, 1);

            if (style < 0) {
                lua_getfield(L, -1, "type");
                if (lua_isstring(L, -1)) {
                    style = hl_name_to_code(lua_tostring(L, -1));
                } else if (lua_isnumber(L, -1)) {
                    style = (int)lua_tointeger(L, -1);
                }
                lua_pop(L, 1);
            }

            if (start <= 0) start = 1;
            if (length > 0 && stop <= 0) stop = start + length - 1;
            if (stop <= 0) stop = start;

            if (style >= 0 && row->rsize > 0) {
                if (start > stop) {
                    int tmp = start;
                    start = stop;
                    stop = tmp;
                }
                if (start < 1) start = 1;
                if (stop > row->rsize) stop = row->rsize;
                for (int pos = start - 1; pos < stop && pos < row->rsize; pos++) {
                    row->hl[pos] = style;
                }
                applied = 1;
            } else if (style >= 0 && row->rsize == 0) {
                applied = 1;
            }
        }
        lua_pop(L, 1);
    }

    return applied;
}

/* Apply Lua custom highlighting to a row */
static void lua_apply_highlight_row(editor_ctx_t *ctx, t_erow *row, int default_ran) {
    if (!ctx || !ctx->L || row == NULL || row->render == NULL) return;
    lua_State *L = ctx->L;
    int top = lua_gettop(L);

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_settop(L, top);
        return;
    }

    lua_getfield(L, -1, "highlight_row");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return;
    }

    lua_pushinteger(L, row->idx);
    lua_pushlstring(L, row->chars ? row->chars : "", (size_t)row->size);
    lua_pushlstring(L, row->render ? row->render : "", (size_t)row->rsize);
    if (ctx->syntax) {
        lua_pushinteger(L, ctx->syntax->type);
    } else {
        lua_pushnil(L);
    }
    lua_pushboolean(L, default_ran);

    if (lua_pcall(L, 5, 1, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        editor_set_status_msg("Lua highlight error: %s", err ? err : "unknown");
        lua_settop(L, top);
        return;
    }

    if (!lua_istable(L, -1)) {
        lua_settop(L, top);
        return;
    }

    int table_index = lua_gettop(L);
    int replace = 0;

    lua_getfield(L, table_index, "replace");
    if (lua_isboolean(L, -1)) replace = lua_toboolean(L, -1);
    lua_pop(L, 1);

    int spans_index = table_index;
    int has_spans_field = 0;

    lua_getfield(L, table_index, "spans");
    if (lua_istable(L, -1)) {
        spans_index = lua_gettop(L);
        has_spans_field = 1;
    } else {
        lua_pop(L, 1);
    }

    if (replace) {
        memset(row->hl, HL_NORMAL, row->rsize);
    }

    lua_apply_span_table(ctx, row, spans_index);

    if (has_spans_field) {
        lua_pop(L, 1);
    }

    lua_settop(L, top);
}

/* ======================== Main Editor Function =========================== */

static void print_usage(void) {
    printf("Usage: loki [options] <filename>\n");
    printf("\nOptions:\n");
    printf("  --help              Show this help message\n");
    printf("  --version           Show version information\n");
    printf("  --complete <file>   Run AI completion on file and save result\n");
    printf("  --explain <file>    Run AI explanation on file and print to stdout\n");
    printf("\nInteractive mode (default):\n");
    printf("  loki <filename>     Open file in interactive editor\n");
    printf("\nKeybindings in interactive mode:\n");
    printf("  Ctrl-S    Save file\n");
    printf("  Ctrl-Q    Quit\n");
    printf("  Ctrl-F    Find\n");
    printf("  Ctrl-L    Toggle Lua REPL\n");
    printf("\nAI commands require OPENAI_API_KEY environment variable\n");
    printf("and .loki/init.lua or ~/.loki/init.lua configuration.\n");
}

int loki_editor_main(int argc, char **argv) {
    /* Register cleanup handler early to ensure terminal is always restored */
    atexit(editor_atexit);

    /* Parse command-line arguments */
    if (argc < 2) {
        print_usage();
        exit(1);
    }

    /* Check for --help flag */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        exit(0);
    }

    /* Check for --version flag */
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("loki %s\n", LOKI_VERSION);
        exit(0);
    }

    /* Check for --complete flag */
    if (strcmp(argv[1], "--complete") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: --complete requires a filename argument\n");
            print_usage();
            exit(1);
        }
        return run_ai_command(argv[2], "complete");
    }

    /* Check for --explain flag */
    if (strcmp(argv[1], "--explain") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: --explain requires a filename argument\n");
            print_usage();
            exit(1);
        }
        return run_ai_command(argv[2], "explain");
    }

    /* Check for unknown options */
    if (argv[1][0] == '-') {
        fprintf(stderr, "Error: Unknown option: %s\n", argv[1]);
        print_usage();
        exit(1);
    }

    /* Default: interactive mode */
    if (argc != 2) {
        fprintf(stderr, "Error: Too many arguments\n");
        print_usage();
        exit(1);
    }

    /* Initialize editor core */
    init_editor(&E);
    editor_select_syntax_highlight(&E, argv[1]);
    editor_open(&E, argv[1]);

    /* Initialize Lua */
    struct loki_lua_opts opts = {
        .bind_editor = 1,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = loki_lua_status_reporter,
        .reporter_userdata = NULL
    };

    E.L = loki_lua_bootstrap(&E, &opts);
    if (!E.L) {
        fprintf(stderr, "Warning: Failed to initialize Lua runtime (%s)\n", loki_lua_runtime());
    }

    /* Initialize REPL */
    lua_repl_init(&E.repl);

    /* Enable terminal raw mode and start main loop */
    enable_raw_mode(STDIN_FILENO);
    editor_set_status_msg(
        "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-W = wrap | Ctrl-L = repl | Ctrl-C = copy");

    while(1) {
        handle_windows_resize(&E);

        /* Process any pending async HTTP requests */
        if (E.L) {
            loki_poll_async_http(&E, E.L);
        }

        editor_refresh_screen(&E);
        editor_process_keypress(&E, STDIN_FILENO);
    }

    return 0;
}

/* Clean up editor resources (called from editor_atexit in loki_core.c) */
void editor_cleanup_resources(editor_ctx_t *ctx) {
    if (!ctx) return;

    /* Clean up Lua REPL */
    lua_repl_free(&ctx->repl);

    /* Clean up Lua state */
    if (ctx->L) {
        lua_close(ctx->L);
        ctx->L = NULL;
    }

    /* Clean up CURL */
    cleanup_curl();
}
