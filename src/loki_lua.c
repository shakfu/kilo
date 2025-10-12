/* loki_lua.c - Lua API bindings for Loki editor
 *
 * This file contains all Lua C bindings that expose editor functionality
 * to Lua scripts. These bindings allow users to extend and customize the
 * editor through Lua configuration files and REPL commands.
 *
 * Dependencies:
 * - Standard C headers for I/O, strings, memory management
 * - Lua 5.4 headers for the C API
 * - Loki headers for core editor functionality and types
 *
 * Note: This code references global editor state (struct E) and various
 * functions from loki_core.c. These dependencies need to be properly
 * exposed through loki/core.h or kept in loki_core.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* for strcasecmp */
#include <stdarg.h>
#include <unistd.h>   /* for access */
#include <ctype.h>    /* for isspace, isprint, tolower */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "loki/core.h"
#include "loki/lua.h"
#include "loki_internal.h"  /* Internal structures and functions */
#include "loki_terminal.h"  /* Terminal functions */
#include "loki_languages.h"  /* Language definitions and dynamic registration */

/* ======================= Lua API bindings ================================ */

/* Registry key for storing editor context pointer.
 * This is used to retrieve the context in Lua C API functions.
 * The key itself is just the address of this variable - we don't care about
 * its value, just that it's a unique pointer for the registry. */
static const char editor_ctx_registry_key = 0;

/* Helper function to retrieve editor context from Lua registry.
 * This is called by all Lua API functions to get the current editor context.
 * Returns NULL if context is not found (shouldn't happen in normal operation). */
static editor_ctx_t* lua_get_editor_context(lua_State *L) {
    lua_pushlightuserdata(L, (void *)&editor_ctx_registry_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    editor_ctx_t *ctx = (editor_ctx_t *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

/* Lua API: loki.status(message) - Set status message */
static int lua_loki_status(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    const char *msg = luaL_checkstring(L, 1);
    editor_set_status_msg(ctx, "%s", msg);
    return 0;
}

/* Lua API: loki.get_line(row) - Get line content at row (0-indexed) */
static int lua_loki_get_line(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    int row = luaL_checkinteger(L, 1);
    if (row < 0 || row >= ctx->numrows) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, ctx->row[row].chars);
    return 1;
}

/* Lua API: loki.get_lines() - Get total number of lines */
static int lua_loki_get_lines(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    lua_pushinteger(L, ctx->numrows);
    return 1;
}

/* Lua API: loki.get_cursor() - Get cursor position (returns row, col) */
static int lua_loki_get_cursor(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    lua_pushinteger(L, ctx->cy);
    lua_pushinteger(L, ctx->cx);
    return 2;
}

/* Lua API: loki.insert_text(text) - Insert text at cursor */
static int lua_loki_insert_text(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    const char *text = luaL_checkstring(L, 1);
    for (const char *p = text; *p; p++) {
        editor_insert_char(ctx, *p);
    }
    return 0;
}

/* Lua API: loki.stream_text(text) - Append text and scroll to bottom */
static int lua_loki_stream_text(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    const char *text = luaL_checkstring(L, 1);

    /* Move to end of file */
    if (ctx->numrows > 0) {
        ctx->cy = ctx->numrows - 1;
        ctx->cx = ctx->row[ctx->cy].size;
    }

    /* Insert the text */
    for (const char *p = text; *p; p++) {
        editor_insert_char(ctx, *p);
    }

    /* Scroll to bottom */
    if (ctx->numrows > ctx->screenrows) {
        ctx->rowoff = ctx->numrows - ctx->screenrows;
    }
    ctx->cy = ctx->numrows - 1;

    /* Refresh screen immediately */
    editor_refresh_screen(ctx);

    return 0;
}

/* Lua API: loki.get_filename() - Get current filename */
static int lua_loki_get_filename(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    if (ctx->filename) {
        lua_pushstring(L, ctx->filename);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* Helper: Map color name to HL_* constant */
static int color_name_to_hl(const char *name) {
    if (strcasecmp(name, "normal") == 0) return HL_NORMAL;
    if (strcasecmp(name, "nonprint") == 0) return HL_NONPRINT;
    if (strcasecmp(name, "comment") == 0) return HL_COMMENT;
    if (strcasecmp(name, "mlcomment") == 0) return HL_MLCOMMENT;
    if (strcasecmp(name, "keyword1") == 0) return HL_KEYWORD1;
    if (strcasecmp(name, "keyword2") == 0) return HL_KEYWORD2;
    if (strcasecmp(name, "string") == 0) return HL_STRING;
    if (strcasecmp(name, "number") == 0) return HL_NUMBER;
    if (strcasecmp(name, "match") == 0) return HL_MATCH;
    return -1;
}

/* Lua API: loki.set_color(name, {r=R, g=G, b=B}) - Set syntax highlight color */
static int lua_loki_set_color(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    int hl = color_name_to_hl(name);
    if (hl < 0) {
        return luaL_error(L, "Unknown color name: %s", name);
    }

    /* Get RGB values from table */
    lua_getfield(L, 2, "r");
    lua_getfield(L, 2, "g");
    lua_getfield(L, 2, "b");

    if (!lua_isnumber(L, -3) || !lua_isnumber(L, -2) || !lua_isnumber(L, -1)) {
        return luaL_error(L, "Color table must have r, g, b numeric fields");
    }

    int r = lua_tointeger(L, -3);
    int g = lua_tointeger(L, -2);
    int b = lua_tointeger(L, -1);

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        return luaL_error(L, "RGB values must be 0-255");
    }

    ctx->colors[hl].r = r;
    ctx->colors[hl].g = g;
    ctx->colors[hl].b = b;

    lua_pop(L, 3);
    return 0;
}

/* Lua API: loki.set_theme(table) - Set multiple colors at once */
static int lua_loki_set_theme(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    luaL_checktype(L, 1, LUA_TTABLE);

    /* Iterate over theme table */
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        /* Key at -2, value at -1 */
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TTABLE) {
            const char *name = lua_tostring(L, -2);
            int hl = color_name_to_hl(name);

            if (hl >= 0) {
                /* Get RGB from value table */
                lua_getfield(L, -1, "r");  /* Stack: ... table, r */
                lua_getfield(L, -2, "g");  /* Stack: ... table, r, g */
                lua_getfield(L, -3, "b");  /* Stack: ... table, r, g, b */

                if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                    int r = lua_tointeger(L, -3);
                    int g = lua_tointeger(L, -2);
                    int b = lua_tointeger(L, -1);

                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        ctx->colors[hl].r = r;
                        ctx->colors[hl].g = g;
                        ctx->colors[hl].b = b;
                    }
                }
                lua_pop(L, 3);
            }
        }
        lua_pop(L, 1); /* Remove value, keep key for next iteration */
    }

    return 0;
}

/* =========================== Modal System Lua API =========================== */

/* Lua API: loki.get_mode() - Get current editor mode */
static int lua_loki_get_mode(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    const char *mode_str = "";
    switch(ctx->mode) {
        case MODE_NORMAL: mode_str = "normal"; break;
        case MODE_INSERT: mode_str = "insert"; break;
        case MODE_VISUAL: mode_str = "visual"; break;
        case MODE_COMMAND: mode_str = "command"; break;
    }
    lua_pushstring(L, mode_str);
    return 1;
}

/* Lua API: loki.set_mode(mode) - Set editor mode */
static int lua_loki_set_mode(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    if (!ctx) return 0;

    const char *mode_str = luaL_checkstring(L, 1);

    EditorMode new_mode = ctx->mode;
    if (strcasecmp(mode_str, "normal") == 0) {
        new_mode = MODE_NORMAL;
    } else if (strcasecmp(mode_str, "insert") == 0) {
        new_mode = MODE_INSERT;
    } else if (strcasecmp(mode_str, "visual") == 0) {
        new_mode = MODE_VISUAL;
        /* Activate selection */
        ctx->sel_active = 1;
        ctx->sel_start_x = ctx->cx;
        ctx->sel_start_y = ctx->cy;
        ctx->sel_end_x = ctx->cx;
        ctx->sel_end_y = ctx->cy;
    } else if (strcasecmp(mode_str, "command") == 0) {
        new_mode = MODE_COMMAND;
    } else {
        return luaL_error(L, "Invalid mode: %s", mode_str);
    }

    ctx->mode = new_mode;
    return 0;
}

/* Lua API: loki.register_command(key, callback) - Register normal mode command */
static int lua_loki_register_command(lua_State *L) {
    /* This just stores commands in a Lua table for now */
    /* The actual dispatching is done by loki_process_normal_key in Lua */

    /* Get or create global command registry table */
    lua_getglobal(L, "_loki_commands");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1); /* Duplicate table */
        lua_setglobal(L, "_loki_commands");
    }

    /* Register command: _loki_commands[key] = callback */
    const char *key = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushstring(L, key);
    lua_pushvalue(L, 2); /* Push the callback function */
    lua_settable(L, -3);

    lua_pop(L, 1); /* Pop the registry table */
    return 0;
}

/* Lua API: loki.async_http(url, method, body, headers, callback) - Async HTTP request */
static int lua_loki_async_http(lua_State *L) {
    editor_ctx_t *ctx = lua_get_editor_context(L);
    const char *url = luaL_checkstring(L, 1);
    const char *method = luaL_optstring(L, 2, "GET");
    const char *body = luaL_optstring(L, 3, NULL);

    /* Parse headers table (optional) */
    const char **headers = NULL;
    int num_headers = 0;

    if (lua_istable(L, 4)) {
        /* Count headers */
        lua_pushnil(L);
        while (lua_next(L, 4) != 0) {
            num_headers++;
            lua_pop(L, 1);
        }

        /* Allocate and populate headers array */
        if (num_headers > 0) {
            headers = malloc(sizeof(char*) * num_headers);
            if (!headers) {
                return luaL_error(L, "Out of memory allocating HTTP headers array");
            }
            int i = 0;
            lua_pushnil(L);
            while (lua_next(L, 4) != 0) {
                const char *header_str = strdup(lua_tostring(L, -1));
                if (!header_str) {
                    /* Free previously allocated headers */
                    for (int j = 0; j < i; j++) {
                        free((void*)headers[j]);
                    }
                    free(headers);
                    lua_pop(L, 1);  /* Pop the value */
                    return luaL_error(L, "Out of memory allocating HTTP header string");
                }
                headers[i++] = header_str;
                lua_pop(L, 1);
            }
        }
    }

    const char *callback = luaL_checkstring(L, 5);

    /* Start async request */
    int req_id = start_async_http_request(ctx, url, method, body, headers, num_headers, callback);

    /* Free headers */
    if (headers) {
        for (int i = 0; i < num_headers; i++) {
            free((void*)headers[i]);
        }
        free(headers);
    }

    if (req_id >= 0) {
        editor_set_status_msg(ctx, "HTTP request sent (async)...");
        lua_pushinteger(L, req_id);
        return 1;
    } else {
        editor_set_status_msg(ctx, "Too many pending HTTP requests");
        lua_pushnil(L);
        return 1;
    }
}

static int lua_loki_repl_register(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *description = luaL_checkstring(L, 2);
    const char *example = luaL_optstring(L, 3, NULL);

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }

    lua_getfield(L, -1, "__repl_help");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }

    lua_newtable(L);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, description);
    lua_setfield(L, -2, "description");
    if (example) {
        lua_pushstring(L, example);
        lua_setfield(L, -2, "example");
    }

    int idx = (int)lua_rawlen(L, -2) + 1;
    lua_rawseti(L, -2, idx);

    lua_pop(L, 2);
    return 0;
}

/* Lua API: loki.register_language(config) - Register a new language for syntax highlighting
 * config table must contain:
 *   - name (string): Language name
 *   - extensions (table): File extensions (e.g., {".py", ".pyw"})
 * Optional fields:
 *   - keywords (table): Language keywords
 *   - types (table): Type keywords
 *   - line_comment (string): Single-line comment delimiter
 *   - block_comment_start (string): Multi-line comment start
 *   - block_comment_end (string): Multi-line comment end
 *   - separators (string): Separator characters
 *   - highlight_strings (boolean): Enable string highlighting (default: true)
 *   - highlight_numbers (boolean): Enable number highlighting (default: true)
 */
static int lua_loki_register_language(lua_State *L) {
    /* Validate argument is a table */
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "argument must be a table");
        return 2;
    }

    /* Allocate new language structure */
    struct t_editor_syntax *lang = calloc(1, sizeof(struct t_editor_syntax));
    if (!lang) {
        lua_pushnil(L);
        lua_pushstring(L, "memory allocation failed");
        return 2;
    }

    /* Extract name (required) */
    lua_getfield(L, 1, "name");
    if (!lua_isstring(L, -1)) {
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'name' field is required and must be a string");
        return 2;
    }
    /* Note: name is not stored in the struct, just used for error messages */
    lua_pop(L, 1);

    /* Extract extensions (required) */
    lua_getfield(L, 1, "extensions");
    if (!lua_istable(L, -1)) {
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'extensions' field is required and must be a table");
        return 2;
    }

    /* Count extensions */
    int ext_count = (int)lua_rawlen(L, -1);
    if (ext_count == 0) {
        lua_pop(L, 1);
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "'extensions' table cannot be empty");
        return 2;
    }

    /* Allocate extension array (NULL-terminated) */
    lang->filematch = calloc(ext_count + 1, sizeof(char*));
    if (!lang->filematch) {
        lua_pop(L, 1);
        free(lang);
        lua_pushnil(L);
        lua_pushstring(L, "memory allocation failed for extensions");
        return 2;
    }

    /* Copy extensions */
    for (int i = 0; i < ext_count; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "extension must be a string");
            return 2;
        }
        const char *ext = lua_tostring(L, -1);
        if (ext[0] != '.') {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "extension must start with '.'");
            return 2;
        }
        lang->filematch[i] = strdup(ext);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  /* pop extensions table */

    /* Extract keywords (optional) */
    lua_getfield(L, 1, "keywords");
    lua_getfield(L, 1, "types");
    int kw_count = lua_istable(L, -2) ? (int)lua_rawlen(L, -2) : 0;
    int type_count = lua_istable(L, -1) ? (int)lua_rawlen(L, -1) : 0;
    int total_kw = kw_count + type_count;

    if (total_kw > 0) {
        lang->keywords = calloc(total_kw + 1, sizeof(char*));
        if (!lang->keywords) {
            lua_pop(L, 2);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "memory allocation failed for keywords");
            return 2;
        }

        /* Copy regular keywords */
        int idx = 0;
        if (kw_count > 0) {
            for (int i = 0; i < kw_count; i++) {
                lua_rawgeti(L, -2, i + 1);
                if (lua_isstring(L, -1)) {
                    const char *kw = lua_tostring(L, -1);
                    lang->keywords[idx++] = strdup(kw);
                }
                lua_pop(L, 1);
            }
        }

        /* Copy type keywords (append "|" to distinguish them) */
        if (type_count > 0) {
            for (int i = 0; i < type_count; i++) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_isstring(L, -1)) {
                    const char *type = lua_tostring(L, -1);
                    size_t len = strlen(type);
                    char *type_with_pipe = malloc(len + 2);
                    if (type_with_pipe) {
                        strcpy(type_with_pipe, type);
                        type_with_pipe[len] = '|';
                        type_with_pipe[len + 1] = '\0';
                        lang->keywords[idx++] = type_with_pipe;
                    }
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_pop(L, 2);  /* pop types and keywords tables */

    /* Extract comment delimiters (optional) */
    lua_getfield(L, 1, "line_comment");
    if (lua_isstring(L, -1)) {
        const char *lc = lua_tostring(L, -1);
        size_t len = strlen(lc);
        if (len >= sizeof(lang->singleline_comment_start)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "line_comment too long (max 3 chars)");
            return 2;
        }
        strncpy(lang->singleline_comment_start, lc, sizeof(lang->singleline_comment_start) - 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "block_comment_start");
    if (lua_isstring(L, -1)) {
        const char *bcs = lua_tostring(L, -1);
        size_t len = strlen(bcs);
        if (len >= sizeof(lang->multiline_comment_start)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "block_comment_start too long (max 5 chars)");
            return 2;
        }
        strncpy(lang->multiline_comment_start, bcs, sizeof(lang->multiline_comment_start) - 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "block_comment_end");
    if (lua_isstring(L, -1)) {
        const char *bce = lua_tostring(L, -1);
        size_t len = strlen(bce);
        if (len >= sizeof(lang->multiline_comment_end)) {
            lua_pop(L, 1);
            free_dynamic_language(lang);
            lua_pushnil(L);
            lua_pushstring(L, "block_comment_end too long (max 5 chars)");
            return 2;
        }
        strncpy(lang->multiline_comment_end, bce, sizeof(lang->multiline_comment_end) - 1);
    }
    lua_pop(L, 1);

    /* Extract separators (optional) */
    lua_getfield(L, 1, "separators");
    if (lua_isstring(L, -1)) {
        const char *sep = lua_tostring(L, -1);
        lang->separators = strdup(sep);
    } else {
        lang->separators = strdup(",.()+-/*=~%<>[];");  /* Default separators */
    }
    lua_pop(L, 1);

    /* Extract flags (optional) */
    lua_getfield(L, 1, "highlight_strings");
    int hl_strings = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : 1;  /* Default: true */
    lua_pop(L, 1);

    lua_getfield(L, 1, "highlight_numbers");
    int hl_numbers = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : 1;  /* Default: true */
    lua_pop(L, 1);

    lang->flags = 0;
    if (hl_strings) lang->flags |= HL_HIGHLIGHT_STRINGS;
    if (hl_numbers) lang->flags |= HL_HIGHLIGHT_NUMBERS;

    lang->type = HL_TYPE_C;  /* Use C-style highlighting */

    /* Add to dynamic registry */
    if (add_dynamic_language(lang) != 0) {
        free_dynamic_language(lang);
        lua_pushnil(L);
        lua_pushstring(L, "failed to register language");
        return 2;
    }

    lua_pushboolean(L, 1);  /* Success */
    return 1;
}

static int lua_loki_status_stdout(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    if (msg && *msg) {
        fprintf(stdout, "[loki] %s\n", msg);
        fflush(stdout);
    }
    return 0;
}

void loki_lua_bind_minimal(lua_State *L) {
    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_pushcfunction(L, lua_loki_status_stdout);
    lua_setfield(L, -2, "status");

    lua_pushcfunction(L, lua_loki_register_language);
    lua_setfield(L, -2, "register_language");

    lua_newtable(L); /* storage for registered help */
    lua_setfield(L, -2, "__repl_help");

    lua_newtable(L); /* repl helpers */
    lua_pushcfunction(L, lua_loki_repl_register);
    lua_setfield(L, -2, "register");
    lua_setfield(L, -2, "repl");

    lua_setglobal(L, "loki");
}

/* Initialize Lua API */
void loki_lua_bind_editor(lua_State *L) {
    /* Create loki table */
    lua_newtable(L);

    /* Register functions */
    lua_pushcfunction(L, lua_loki_status);
    lua_setfield(L, -2, "status");

    lua_pushcfunction(L, lua_loki_get_line);
    lua_setfield(L, -2, "get_line");

    lua_pushcfunction(L, lua_loki_get_lines);
    lua_setfield(L, -2, "get_lines");

    lua_pushcfunction(L, lua_loki_get_cursor);
    lua_setfield(L, -2, "get_cursor");

    lua_pushcfunction(L, lua_loki_insert_text);
    lua_setfield(L, -2, "insert_text");

    lua_pushcfunction(L, lua_loki_stream_text);
    lua_setfield(L, -2, "stream_text");

    lua_pushcfunction(L, lua_loki_get_filename);
    lua_setfield(L, -2, "get_filename");

    lua_pushcfunction(L, lua_loki_set_color);
    lua_setfield(L, -2, "set_color");

    lua_pushcfunction(L, lua_loki_set_theme);
    lua_setfield(L, -2, "set_theme");

    /* Modal system functions */
    lua_pushcfunction(L, lua_loki_get_mode);
    lua_setfield(L, -2, "get_mode");

    lua_pushcfunction(L, lua_loki_set_mode);
    lua_setfield(L, -2, "set_mode");

    lua_pushcfunction(L, lua_loki_register_command);
    lua_setfield(L, -2, "register_command");

    lua_pushcfunction(L, lua_loki_async_http);
    lua_setfield(L, -2, "async_http");

    lua_pushcfunction(L, lua_loki_register_language);
    lua_setfield(L, -2, "register_language");

    lua_newtable(L); /* storage for registered help */
    lua_setfield(L, -2, "__repl_help");

    lua_newtable(L); /* repl helpers */
    lua_pushcfunction(L, lua_loki_repl_register);
    lua_setfield(L, -2, "register");
    lua_setfield(L, -2, "repl");

    /* Highlight constants */
    lua_newtable(L);
    lua_pushinteger(L, HL_NORMAL);
    lua_setfield(L, -2, "normal");
    lua_pushinteger(L, HL_NONPRINT);
    lua_setfield(L, -2, "nonprint");
    lua_pushinteger(L, HL_COMMENT);
    lua_setfield(L, -2, "comment");
    lua_pushinteger(L, HL_MLCOMMENT);
    lua_setfield(L, -2, "mlcomment");
    lua_pushinteger(L, HL_KEYWORD1);
    lua_setfield(L, -2, "keyword1");
    lua_pushinteger(L, HL_KEYWORD2);
    lua_setfield(L, -2, "keyword2");
    lua_pushinteger(L, HL_STRING);
    lua_setfield(L, -2, "string");
    lua_pushinteger(L, HL_NUMBER);
    lua_setfield(L, -2, "number");
    lua_pushinteger(L, HL_MATCH);
    lua_setfield(L, -2, "match");
    lua_setfield(L, -2, "hl");

    /* Set as global 'loki' */
    lua_setglobal(L, "loki");
}

static void loki_lua_report(const struct loki_lua_opts *opts, const char *fmt, ...) {
    if (!fmt) return;

    char message[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    if (opts && opts->reporter) {
        opts->reporter(message, opts->reporter_userdata);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

/* Load init.lua: try override, .loki/init.lua (local), then ~/.loki/init.lua */
int loki_lua_load_config(lua_State *L, const struct loki_lua_opts *opts) {
    if (!L) return -1;

    const char *override = (opts && opts->config_override && opts->config_override[0] != '\0')
                               ? opts->config_override
                               : NULL;
    const char *project_root = NULL;
    char init_path[1024];
    int loaded = 0;

    if (opts && opts->project_root && opts->project_root[0] != '\0') {
        project_root = opts->project_root;
    }

    if (override && override[0] != '\0') {
        if (luaL_dofile(L, override) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            loki_lua_report(opts, "Lua init error (%s): %s", override, err ? err : "unknown");
            lua_pop(L, 1);
            return -1;
        }
        return 0;
    }

    /* Try local .loki/init.lua first (project-specific) */
    if (project_root) {
        snprintf(init_path, sizeof(init_path), "%s/.loki/init.lua", project_root);
    } else {
        snprintf(init_path, sizeof(init_path), ".loki/init.lua");
    }

    if (access(init_path, R_OK) == 0) {
        if (luaL_dofile(L, init_path) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            loki_lua_report(opts, "Lua init error (%s): %s", init_path, err ? err : "unknown");
            lua_pop(L, 1);
            return -1;
        }
        loaded = 1;
    }

    if (!loaded) {
        const char *home = getenv("HOME");
        if (home && home[0] != '\0') {
            snprintf(init_path, sizeof(init_path), "%s/.loki/init.lua", home);
            if (access(init_path, R_OK) == 0) {
                if (luaL_dofile(L, init_path) != LUA_OK) {
                    const char *err = lua_tostring(L, -1);
                    loki_lua_report(opts, "Lua init error (%s): %s", init_path, err ? err : "unknown");
                    lua_pop(L, 1);
                    return -1;
                }
                loaded = 1;
            }
        }
    }

    return loaded ? 0 : 1;
}

static void loki_lua_extend_path(lua_State *L, const struct loki_lua_opts *opts) {
    if (!L) return;

    char addition[4096];
    addition[0] = '\0';
    size_t used = 0;
    size_t remaining = sizeof(addition);

    const char *project_root = (opts && opts->project_root && opts->project_root[0] != '\0')
                                   ? opts->project_root
                                   : ".";
    int wrote = snprintf(addition + used, remaining, "%s/.loki/?.lua;%s/.loki/?/init.lua;",
                         project_root, project_root);
    if (wrote > 0 && (size_t)wrote < remaining) {
        used += (size_t)wrote;
        remaining -= (size_t)wrote;
    } else {
        remaining = remaining > 0 ? 1 : 0;
        used = sizeof(addition) - remaining;
    }

    const char *home = getenv("HOME");
    if (home && home[0] != '\0' && remaining > 1) {
        wrote = snprintf(addition + used, remaining, "%s/.loki/?.lua;%s/.loki/?/init.lua;",
                         home, home);
        if (wrote > 0 && (size_t)wrote < remaining) {
            used += (size_t)wrote;
            remaining -= (size_t)wrote;
        } else {
            remaining = remaining > 0 ? 1 : 0;
            used = sizeof(addition) - remaining;
        }
    }

    const char *extra = NULL;
    if (opts && opts->extra_lua_path && opts->extra_lua_path[0] != '\0') {
        extra = opts->extra_lua_path;
    } else {
        const char *env_extra = getenv("LOKI_LUA_PATH");
        if (env_extra && env_extra[0] != '\0') {
            extra = env_extra;
        }
    }
    if (extra && remaining > 1) {
        wrote = snprintf(addition + used, remaining, "%s;", extra);
        if (wrote > 0 && (size_t)wrote < remaining) {
            used += (size_t)wrote;
            remaining -= (size_t)wrote;
        }
    }

    if (used == 0) {
        return;
    }

    if (addition[used - 1] == ';') {
        addition[used - 1] = '\0';
    }

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "path");
    const char *current = lua_tostring(L, -1);
    if (!current) current = "";

    lua_pushfstring(L, "%s;%s", current, addition);
    lua_setfield(L, -3, "path");

    lua_pop(L, 2); /* pop path and package */
}

void loki_lua_bind_http(lua_State *L) {
    if (!L) return;

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }

    lua_pushcfunction(L, lua_loki_async_http);
    lua_setfield(L, -2, "async_http");

    lua_setglobal(L, "loki");
}

void loki_lua_install_namespaces(lua_State *L) {
    if (!L) return;

    lua_getglobal(L, "loki");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (lua_getfield(L, -1, "async_http") != LUA_TFUNCTION) {
        lua_pop(L, 2);
        return;
    }
    lua_pop(L, 1); /* remove async_http */

    static const char *const shim =
        "local loki = ...\n"
        "local function ensure(tbl, key)\n"
        "  local value = rawget(tbl, key)\n"
        "  if type(value) ~= 'table' then\n"
        "    value = {}\n"
        "    rawset(tbl, key, value)\n"
        "  end\n"
        "  return value\n"
        "end\n"
        "local editor = ensure(loki, 'editor')\n"
        "editor.buffer = ensure(editor, 'buffer')\n"
        "editor.status = ensure(editor, 'status')\n"
        "function editor.status.set(message)\n"
        "  return loki.status(message)\n"
        "end\n"
        "function editor.buffer.get_line(idx)\n"
        "  return loki.get_line(idx)\n"
        "end\n"
        "function editor.buffer.line_count()\n"
        "  return loki.get_lines()\n"
        "end\n"
        "function editor.buffer.insert(text)\n"
        "  return loki.insert_text(text)\n"
        "end\n"
        "local ai = rawget(_G, 'ai')\n"
        "if type(ai) ~= 'table' then\n"
        "  ai = {}\n"
        "  rawset(_G, 'ai', ai)\n"
        "end\n"
        "local function default_headers()\n"
        "  local api_key = os.getenv('OPENAI_API_KEY')\n"
        "  if api_key and api_key ~= '' then\n"
        "    return {\n"
        "      'Content-Type: application/json',\n"
        "      'Authorization: Bearer ' .. api_key,\n"
        "    }\n"
        "  end\n"
        "  return { 'Content-Type: application/json' }\n"
        "end\n"
        "local function default_body(prompt, opts)\n"
        "  local model = (opts and opts.model) or os.getenv('LOKI_AI_MODEL') or 'gpt-5-nano'\n"
        "  local temperature = (opts and opts.temperature) or 0.2\n"
        "  return string.format('{\"model\":%q,\"temperature\":%.3f,\"messages\":[{\"role\":\"user\",\"content\":%q}]}',\n"
        "    model, temperature, prompt)\n"
        "end\n"
        "function ai.prompt(prompt, opts)\n"
        "  assert(type(prompt) == 'string', 'ai.prompt expects a prompt string')\n"
        "  opts = opts or {}\n"
        "  local url = opts.url or os.getenv('LOKI_AI_URL') or 'https://api.openai.com/v1/chat/completions'\n"
        "  local method = opts.method or 'POST'\n"
        "  local callback = opts.callback or opts.on_response or 'ai_response_handler'\n"
        "  assert(type(callback) == 'string' and callback ~= '', 'ai.prompt requires opts.callback (Lua function name)')\n"
        "  local headers = opts.headers\n"
        "  if headers == nil then headers = default_headers() end\n"
        "  local body = opts.body\n"
        "  if body == nil then body = default_body(prompt, opts) end\n"
        "  return loki.async_http(url, method, body, headers, callback)\n"
        "end\n";

    if (luaL_loadstring(L, shim) == LUA_OK) {
        lua_pushvalue(L, -2); /* push loki table */
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            fprintf(stderr, "Failed to install ai namespace: %s\n", err ? err : "unknown error");
            lua_pop(L, 1);
        }
    } else {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "Failed to compile ai namespace shim: %s\n", err ? err : "unknown error");
        lua_pop(L, 1);
    }

    lua_pop(L, 1); /* loki */
}

/* Public API: Poll async HTTP requests (for REPL and external users) */
void loki_poll_async_http(editor_ctx_t *ctx, lua_State *L) {
    check_async_requests(ctx, L);
}

const char *loki_lua_runtime(void) {
#if defined(LUAJIT_VERSION)
    return "LuaJIT " LUAJIT_VERSION;
#elif defined(LUA_VERSION_MAJOR) && defined(LUA_VERSION_MINOR)
    return "Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR;
#elif defined(LUA_VERSION)
    return LUA_VERSION;
#else
    return "Lua";
#endif
}

lua_State *loki_lua_bootstrap(editor_ctx_t *ctx, const struct loki_lua_opts *opts) {
    struct loki_lua_opts effective = {
        .bind_editor = 1,
        .bind_http = 1,
        .load_config = 1,
        .config_override = NULL,
        .project_root = NULL,
        .extra_lua_path = NULL,
        .reporter = NULL,
        .reporter_userdata = NULL,
    };

    if (opts) {
        effective.bind_editor = opts->bind_editor ? 1 : 0;
        effective.bind_http = opts->bind_http ? 1 : 0;
        effective.load_config = opts->load_config ? 1 : 0;
        effective.config_override = opts->config_override;
        effective.project_root = opts->project_root;
        effective.extra_lua_path = opts->extra_lua_path;
        effective.reporter = opts->reporter;
        effective.reporter_userdata = opts->reporter_userdata;
    }

    lua_State *L = luaL_newstate();
    if (!L) {
        loki_lua_report(&effective, "Failed to allocate Lua state");
        return NULL;
    }

    /* Store editor context in Lua registry for retrieval by API functions */
    if (ctx) {
        lua_pushlightuserdata(L, (void *)&editor_ctx_registry_key);
        lua_pushlightuserdata(L, ctx);
        lua_settable(L, LUA_REGISTRYINDEX);
    }

    luaL_openlibs(L);
    loki_lua_extend_path(L, &effective);

    if (effective.bind_editor) {
        loki_lua_bind_editor(L);
    } else {
        loki_lua_bind_minimal(L);
    }

    if (effective.bind_http) {
        loki_lua_bind_http(L);
    }

    if (effective.load_config) {
        if (loki_lua_load_config(L, &effective) < 0) {
            /* Leave state usable even if config fails; errors already reported */
        }
    }

    loki_lua_install_namespaces(L);

    return L;
}
/* =========================== Lua REPL Functions ========================== */
/* Extracted from loki_core.c - handles the embedded Lua REPL interface */

/* Forward declarations */
static int lua_repl_handle_builtin(editor_ctx_t *ctx, const char *cmd, size_t len);

void lua_repl_render(editor_ctx_t *ctx, struct abuf *ab) {
    if (!ctx || !ctx->repl.active) return;

    terminal_buffer_append(ab,"\r\n",2);

    int start = ctx->repl.log_len - LUA_REPL_OUTPUT_ROWS;
    if (start < 0) start = 0;
    int rendered = 0;

    for (int i = start; i < ctx->repl.log_len; i++) {
        const char *line = ctx->repl.log[i] ? ctx->repl.log[i] : "";
        int take = (int)strlen(line);
        if (take > ctx->screencols) take = ctx->screencols;
        terminal_buffer_append(ab,"\x1b[0K",4);
        if (take > 0) terminal_buffer_append(ab,line,take);
        terminal_buffer_append(ab,"\r\n",2);
        rendered++;
    }

    while (rendered < LUA_REPL_OUTPUT_ROWS) {
        terminal_buffer_append(ab,"\x1b[0K\r\n",6);
        rendered++;
    }

    terminal_buffer_append(ab,"\x1b[0K",4);
    terminal_buffer_append(ab,LUA_REPL_PROMPT,strlen(LUA_REPL_PROMPT));

    int prompt_len = (int)strlen(LUA_REPL_PROMPT);
    int available = ctx->screencols - prompt_len;
    if (available < 0) available = 0;
    if (available > 0 && ctx->repl.input_len > 0) {
        int shown = ctx->repl.input_len;
        if (shown > available) shown = available;
        terminal_buffer_append(ab,ctx->repl.input,shown);
    }
}

/* =========================== Lua REPL Helpers ============================ */

static void lua_repl_clear_input(t_lua_repl *repl) {
    repl->input_len = 0;
    repl->input[0] = '\0';
}

static void lua_repl_append_log_owned(editor_ctx_t *ctx, char *line) {
    if (!ctx || !line) return;
    if (ctx->repl.log_len == LUA_REPL_LOG_MAX) {
        free(ctx->repl.log[0]);
        memmove(ctx->repl.log, ctx->repl.log + 1,
                sizeof(char*) * (LUA_REPL_LOG_MAX - 1));
        ctx->repl.log_len--;
    }
    ctx->repl.log[ctx->repl.log_len++] = line;
}

void lua_repl_append_log(editor_ctx_t *ctx, const char *line) {
    if (!ctx || !line) return;
    char *copy = strdup(line);
    if (!copy) {
        editor_set_status_msg(ctx, "Lua REPL: out of memory");
        return;
    }
    lua_repl_append_log_owned(ctx, copy);
}

static void lua_repl_log_prefixed(editor_ctx_t *ctx, const char *prefix, const char *text) {
    if (!ctx || !prefix) prefix = "";
    if (!text) text = "";

    size_t prefix_len = strlen(prefix);
    const char *line = text;
    do {
        const char *newline = strchr(line, '\n');
        size_t segment_len = newline ? (size_t)(newline - line) : strlen(line);
        size_t total = prefix_len + segment_len;
        char *entry = malloc(total + 1);
        if (!entry) {
            editor_set_status_msg(ctx, "Lua REPL: out of memory");
            return;
        }
        memcpy(entry, prefix, prefix_len);
        if (segment_len) memcpy(entry + prefix_len, line, segment_len);
        entry[total] = '\0';
        lua_repl_append_log_owned(ctx, entry);
        if (!newline) break;
        line = newline + 1;
        if (*line == '\0') {
            /* Preserve empty trailing line */
            char *blank = strdup(prefix);
            if (!blank) {
                editor_set_status_msg(ctx, "Lua REPL: out of memory");
                return;
            }
            lua_repl_append_log_owned(ctx, blank);
            break;
        }
    } while (1);
}

static void lua_repl_reset_log(t_lua_repl *repl) {
    for (int i = 0; i < repl->log_len; i++) {
        free(repl->log[i]);
        repl->log[i] = NULL;
    }
    repl->log_len = 0;
}

static const char *lua_repl_top_to_string(lua_State *L, size_t *len) {
#if LUA_VERSION_NUM >= 502
    return luaL_tolstring(L, -1, len);
#else
    if (luaL_callmeta(L, -1, "__tostring")) {
        if (!lua_isstring(L, -1)) {
            lua_pop(L, 1);
            return NULL;
        }
        return lua_tolstring(L, -1, len);
    }
    return lua_tolstring(L, -1, len);
#endif
}

static void lua_repl_push_history(editor_ctx_t *ctx, const char *cmd) {
    if (!ctx || !cmd || !*cmd) return;
    size_t len = strlen(cmd);
    int all_space = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)cmd[i])) {
            all_space = 0;
            break;
        }
    }
    if (all_space) return;

    if (ctx->repl.history_len == LUA_REPL_HISTORY_MAX) {
        free(ctx->repl.history[0]);
        memmove(ctx->repl.history, ctx->repl.history + 1,
                sizeof(char*) * (LUA_REPL_HISTORY_MAX - 1));
        ctx->repl.history_len--;
    }

    if (ctx->repl.history_len > 0) {
        const char *last = ctx->repl.history[ctx->repl.history_len - 1];
        if (last && strcmp(last, cmd) == 0) {
            ctx->repl.history_index = -1;
            return;
        }
    }

    char *copy = strdup(cmd);
    if (!copy) {
        editor_set_status_msg(ctx, "Lua REPL: out of memory");
        return;
    }
    ctx->repl.history[ctx->repl.history_len++] = copy;
    ctx->repl.history_index = -1;
}

static void lua_repl_history_apply(editor_ctx_t *ctx, t_lua_repl *repl) {
    if (repl->history_index < 0 || repl->history_index >= repl->history_len)
        return;
    const char *src = repl->history[repl->history_index];
    if (!src) {
        lua_repl_clear_input(repl);
        return;
    }
    size_t copy_len = strlen(src);
    if (copy_len > KILO_QUERY_LEN) copy_len = KILO_QUERY_LEN;
    int prompt_len = (int)strlen(LUA_REPL_PROMPT);
    if (ctx && ctx->screencols > prompt_len) {
        int max_cols = ctx->screencols - prompt_len;
        if ((int)copy_len > max_cols) copy_len = max_cols;
    }
    memcpy(repl->input, src, copy_len);
    repl->input[copy_len] = '\0';
    repl->input_len = (int)copy_len;
}

static int lua_repl_input_has_content(const t_lua_repl *repl) {
    for (int i = 0; i < repl->input_len; i++) {
        if (!isspace((unsigned char)repl->input[i])) return 1;
    }
    return 0;
}

static void lua_repl_emit_registered_help(editor_ctx_t *ctx) {
    if (!ctx || !ctx->L) return;

    lua_getglobal(ctx->L, "loki");
    if (!lua_istable(ctx->L, -1)) {
        lua_pop(ctx->L, 1);
        return;
    }

    lua_getfield(ctx->L, -1, "__repl_help");
    if (!lua_istable(ctx->L, -1)) {
        lua_pop(ctx->L, 2);
        return;
    }

    int len = (int)lua_rawlen(ctx->L, -1);
    if (len == 0) {
        lua_pop(ctx->L, 2);
        return;
    }

    lua_repl_log_prefixed(ctx, "= ", "Project commands:");

    lua_pushnil(ctx->L);
    while (lua_next(ctx->L, -2) != 0) {
        const char *name = NULL;
        const char *desc = NULL;
        const char *example = NULL;

        lua_getfield(ctx->L, -1, "name");
        if (lua_isstring(ctx->L, -1)) name = lua_tostring(ctx->L, -1);
        lua_pop(ctx->L, 1);

        lua_getfield(ctx->L, -1, "description");
        if (lua_isstring(ctx->L, -1)) desc = lua_tostring(ctx->L, -1);
        lua_pop(ctx->L, 1);

        lua_getfield(ctx->L, -1, "example");
        if (lua_isstring(ctx->L, -1)) example = lua_tostring(ctx->L, -1);
        lua_pop(ctx->L, 1);

        if (name && desc) {
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s - %s", name, desc);
            lua_repl_append_log(ctx, buf);
        }
        if (example) {
            char buf[256];
            snprintf(buf, sizeof(buf), "    e.g. %s", example);
            lua_repl_append_log(ctx, buf);
        }

        lua_pop(ctx->L, 1); /* pop value, keep key for next iteration */
    }

    lua_pop(ctx->L, 2); /* __repl_help, loki */
}

static void lua_repl_execute_current(editor_ctx_t *ctx) {
    if (!ctx || !ctx->L) {
        if (ctx) lua_repl_append_log(ctx, "! Lua interpreter not available");
        return;
    }

    if (!lua_repl_input_has_content(&ctx->repl)) {
        lua_repl_clear_input(&ctx->repl);
        return;
    }

    lua_repl_log_prefixed(ctx, LUA_REPL_PROMPT, ctx->repl.input);
    lua_repl_push_history(ctx, ctx->repl.input);

    const char *trim = ctx->repl.input;
    while (*trim && isspace((unsigned char)*trim)) trim++;
    size_t tlen = strlen(trim);
    while (tlen > 0 && isspace((unsigned char)trim[tlen-1])) tlen--;

    if (lua_repl_handle_builtin(ctx, trim, tlen)) {
        lua_repl_clear_input(&ctx->repl);
        return;
    }

    int base = lua_gettop(ctx->L);
    if (luaL_loadbuffer(ctx->L, ctx->repl.input, (size_t)ctx->repl.input_len,
                        "repl") != LUA_OK) {
        const char *err = lua_tostring(ctx->L, -1);
        lua_repl_log_prefixed(ctx, "! ", err ? err : "(unknown error)");
        lua_pop(ctx->L, 1);
        lua_settop(ctx->L, base);
        lua_repl_clear_input(&ctx->repl);
        return;
    }

    int status = lua_pcall(ctx->L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        const char *err = lua_tostring(ctx->L, -1);
        lua_repl_log_prefixed(ctx, "! ", err ? err : "(unknown error)");
        lua_pop(ctx->L, 1);
        lua_settop(ctx->L, base);
        lua_repl_clear_input(&ctx->repl);
        return;
    }

    int results = lua_gettop(ctx->L) - base;
    if (results == 0) {
        lua_repl_log_prefixed(ctx, "= ", "ok");
    } else {
        for (int i = 0; i < results; i++) {
            lua_pushvalue(ctx->L, base + 1 + i);
            size_t len = 0;
            const char *res = lua_repl_top_to_string(ctx->L, &len);
            if (res) {
                lua_repl_log_prefixed(ctx, "= ", res);
            } else {
                lua_repl_log_prefixed(ctx, "= ", "(non-printable)");
            }
            lua_settop(ctx->L, base + results);
        }
    }
    lua_settop(ctx->L, base);
    lua_repl_clear_input(&ctx->repl);
}

static int lua_repl_iequals(const char *cmd, size_t len, const char *word) {
    size_t wlen = strlen(word);
    if (len != wlen) return 0;
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)cmd[i]) != tolower((unsigned char)word[i]))
            return 0;
    }
    return 1;
}

static int lua_repl_handle_builtin(editor_ctx_t *ctx, const char *cmd, size_t len) {
    if (!ctx || !cmd) return 0;
    while (len && isspace((unsigned char)*cmd)) {
        cmd++;
        len--;
    }
    while (len && isspace((unsigned char)cmd[len-1])) len--;
    if (len == 0) return 0;

    if (cmd[0] == ':') {
        cmd++;
        if (len) len--;
        while (len && isspace((unsigned char)*cmd)) {
            cmd++;
            len--;
        }
        while (len && isspace((unsigned char)cmd[len-1])) len--;
        if (len == 0) return 0;
    }

    if ((len == 1 && cmd[0] == '?') || lua_repl_iequals(cmd, len, "help")) {
        lua_repl_log_prefixed(ctx, "= ", "Built-in commands:");
        lua_repl_append_log(ctx, "  help       Show this help message");
        lua_repl_append_log(ctx, "  history    Print recent commands");
        lua_repl_append_log(ctx, "  clear      Clear the REPL output log");
        lua_repl_append_log(ctx, "  clear-history  Drop saved input history");
        lua_repl_append_log(ctx, "  exit       Close the REPL panel");
        lua_repl_emit_registered_help(ctx);
        lua_repl_append_log(ctx, "  Lua code   Any other input runs inside loki's Lua state");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "clear")) {
        lua_repl_reset_log(&ctx->repl);
        lua_repl_log_prefixed(ctx, "= ", "Log cleared");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "history")) {
        if (ctx->repl.history_len == 0) {
            lua_repl_log_prefixed(ctx, "= ", "History is empty");
            return 1;
        }
        lua_repl_log_prefixed(ctx, "= ", "History (newest first):");
        int start = ctx->repl.history_len - 1;
        int shown = 0;
        for (int i = start; i >= 0; i--) {
            const char *entry = ctx->repl.history[i];
            if (!entry) continue;
            char buf[256];
            snprintf(buf, sizeof(buf), "  %d: %s", ctx->repl.history_len - i, entry);
            lua_repl_append_log(ctx, buf);
            shown++;
            if (shown >= 20) {
                lua_repl_append_log(ctx, "  ...");
                break;
            }
        }
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "clear-history")) {
        for (int i = 0; i < ctx->repl.history_len; i++) {
            free(ctx->repl.history[i]);
            ctx->repl.history[i] = NULL;
        }
        ctx->repl.history_len = 0;
        ctx->repl.history_index = -1;
        lua_repl_log_prefixed(ctx, "= ", "History cleared");
        return 1;
    }

    if (lua_repl_iequals(cmd, len, "exit") || lua_repl_iequals(cmd, len, "quit")) {
        ctx->repl.active = 0;
        editor_update_repl_layout(ctx);
        editor_set_status_msg(ctx, "Lua REPL closed");
        return 1;
    }

    return 0;
}

void lua_repl_handle_keypress(editor_ctx_t *ctx, int key) {
    if (!ctx) return;
    t_lua_repl *repl = &ctx->repl;
    int prompt_len = (int)strlen(LUA_REPL_PROMPT);

    switch(key) {
    case CTRL_L:
    case ESC:
    case CTRL_C:
        repl->active = 0;
        editor_update_repl_layout(ctx);
        editor_set_status_msg(ctx, "Lua REPL closed");
        return;
    case CTRL_U:
        lua_repl_clear_input(repl);
        repl->history_index = -1;
        return;
    case BACKSPACE:
    case CTRL_H:
    case DEL_KEY:
        if (repl->input_len > 0) {
            repl->input[--repl->input_len] = '\0';
        }
        repl->history_index = -1;
        return;
    case ARROW_UP:
        if (repl->history_len > 0) {
            if (repl->history_index == -1)
                repl->history_index = repl->history_len - 1;
            else if (repl->history_index > 0)
                repl->history_index--;
            lua_repl_history_apply(ctx, repl);
        }
        return;
    case ARROW_DOWN:
        if (repl->history_len > 0) {
            if (repl->history_index == -1) {
                return;
            } else if (repl->history_index < repl->history_len - 1) {
                repl->history_index++;
                lua_repl_history_apply(ctx, repl);
            } else {
                repl->history_index = -1;
                lua_repl_clear_input(repl);
            }
        }
        return;
    case ENTER:
        lua_repl_execute_current(ctx);
        if (!repl->active) {
            editor_update_repl_layout(ctx);
        }
        return;
    default:
        if (isprint(key)) {
            if (repl->input_len < KILO_QUERY_LEN) {
                if (ctx->screencols <= prompt_len) return;
                if (prompt_len + repl->input_len >= ctx->screencols) return;
                repl->input[repl->input_len++] = key;
                repl->input[repl->input_len] = '\0';
                repl->history_index = -1;
            }
        }
        return;
    }
}

void lua_repl_free(t_lua_repl *repl) {
    for (int i = 0; i < repl->history_len; i++) {
        free(repl->history[i]);
        repl->history[i] = NULL;
    }
    repl->history_len = 0;
    repl->history_index = -1;

    lua_repl_reset_log(repl);
}

void lua_repl_init(t_lua_repl *repl) {
    lua_repl_free(repl);
    repl->active = 0;
    repl->history_index = -1;
    lua_repl_clear_input(repl);
}
