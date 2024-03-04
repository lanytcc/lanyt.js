#ifndef MODULE_H
#define MODULE_H

#include <quickjs.h>

#if defined(_WIN32) || defined(_WIN64)
#define p_suffix ".dll"
#elif defined(__linux__)
#define p_suffix ".so"
#elif defined(__APPLE__)
#define p_suffix ".dylib"
#else
#error "Unknown_platform"
#endif

void lanyt_js_module_init();
void lanyt_js_module_free();

JSModuleDef *lanyt_js_init_module(JSContext *ctx, const char *module_name);
// cmodule
typedef JSModuleDef *(*init_cmodule_fn_t)(JSContext *ctx,
                                          const char *module_name);
int cmodule_list_add(const char *name, init_cmodule_fn_t fn);
int cmodule_list_find(const char *name, init_cmodule_fn_t *fn);

// cmd
// typedef int (*init_cmd_fn_t)(JSRuntime *rt, int argc, char **argv);
// int cmd_run(JSRuntime *rt, int argc, char **argv);
// int cmd_list_add(const char *name, init_cmd_fn_t fn);

// ffi
init_cmodule_fn_t load_dynamic(const char *filename, const char *fn_name,
                               char **error_msg);

// plugin
// typedef struct plugin plugin_t;
// plugin_t *load_plugin(JSContext *ctx, const char *plugin_name);

#endif // MODULE_H