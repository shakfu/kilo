#ifndef LOKI_LUA_H
#define LOKI_LUA_H

#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*loki_lua_report_fn)(const char *message, void *userdata);

struct loki_lua_opts {
    int bind_editor;           /* Non-zero to load editor bindings */
    int bind_http;             /* Non-zero to expose async HTTP helpers */
    int load_config;           /* Non-zero to load .loki/init.lua and ~/.loki/init.lua */
    const char *config_override; /* Optional absolute path to init.lua */
    const char *project_root;  /* Optional project root for .loki/ discovery */
    const char *extra_lua_path;/* Optional extra package.path entries */
    loki_lua_report_fn reporter; /* Optional reporter for init errors */
    void *reporter_userdata;   /* Context passed to reporter */
};

lua_State *loki_lua_bootstrap(const struct loki_lua_opts *opts);
const char *loki_lua_runtime(void);
void loki_lua_bind_editor(lua_State *L);
void loki_lua_bind_http(lua_State *L);
int loki_lua_load_config(lua_State *L, const struct loki_lua_opts *opts);
void loki_lua_install_namespaces(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* LOKI_LUA_H */
