
#include "module.h"
#include "jsc.h"
#include "quickjs-libc.h"
#include "quickjs.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#include <mimalloc.h>
#include <vcruntime.h>

#ifndef MODULE_MAX_NAME
#define MODULE_MAX_NAME 256
#endif
#define cl_load atomic_load_explicit(&cmodule_list, memory_order_acquire)

static char *concat(const char *s1, const char *s2) {
    char *result = mi_malloc(strlen(s1) + strlen(s2) + 1);
    if (result == NULL) {
        fprintf(stderr, "memory can't apply\n");
        return NULL;
    }
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

static bool has_prefix(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

typedef struct {
    char *name;
    init_cmodule_fn_t fn;
} cmodule_t;

typedef struct {
    cmodule_t *array;
    int cap;
    int len;
} cmodule_list_t;

typedef struct {
    char *name;
    init_cmd_fn_t fn;
} cmd_t;

typedef struct {
    cmd_t *array;
    int cap;
    int len;
} cmd_list_t;

_Atomic(cmodule_list_t *) cmodule_list = NULL;
#define cl_load atomic_load_explicit(&cmodule_list, memory_order_acquire)

void cmodule_list_free() {

    if (cl_load == NULL)
        return;
    while (cl_load->len > 0) {
        cmodule_t *t = &cl_load->array[--cl_load->len];
        mi_free(t->name);
    }
    mi_free(cl_load->array);
    cl_load->array = NULL;
    cl_load->cap = 0;

    mi_free(cl_load);
    atomic_store_explicit(&cmodule_list, NULL, memory_order_release);
}

int cmodule_list_add(const char *name, init_cmodule_fn_t fn) {

    if (!cl_load) {
        atomic_store_explicit(&cmodule_list, mi_malloc(sizeof(cmodule_list_t)),
                              memory_order_release);
        cl_load->array = NULL;
        cl_load->cap = 0;
        cl_load->len = 0;
    }

    if (cl_load->len >= cl_load->cap) {
        size_t newcap = cl_load->cap + (cl_load->cap >> 1) + 4;
        cmodule_t *a =
            mi_realloc(cl_load->array, sizeof(cl_load->array[0]) * newcap);

        if (!a)
            return -1;

        cl_load->array = a;
        cl_load->cap = newcap;
    }

    cl_load->array[cl_load->len].name = mi_strdup(name);
    cl_load->array[cl_load->len].fn = fn;
    ++cl_load->len;

    return 0;
}

int cmodule_list_find(const char *name, init_cmodule_fn_t *fn) {

    if (cl_load == NULL)
        return -2;

    for (int i = 0; i < cl_load->len; ++i) {
        if (strcmp(name, cl_load->array[i].name) == 0) {
            *fn = cl_load->array[i].fn;
            return 0;
        }
    }

    return -1;
}

_Atomic(cmd_list_t *) cmd_list = NULL;
#define cmd_load atomic_load_explicit(&cmd_list, memory_order_acquire)

int cmd_run(JSRuntime *rt, int argc, char **argv) {

    if (cmd_load == NULL)
        return 0;

    for (int i = 1; i < argc; ++i) {
        for (int j = 0; j < cmd_load->len; ++j) {
            if (strcmp(argv[i], cmd_load->array[j].name) == 0) {
                return cmd_load->array[j].fn(rt, argc, argv);
            }
        }
    }

    return 0;
}

int cmd_list_add(const char *name, init_cmd_fn_t fn) {

    if (!cmd_load) {
        atomic_store_explicit(&cmd_list, mi_malloc(sizeof(cmd_list_t)),
                              memory_order_release);
        cmd_load->array = NULL;
        cmd_load->cap = 0;
        cmd_load->len = 0;
    }

    for (int i = 0; i < cmd_load->len; ++i) {
        if (strcmp(name, cmd_load->array[i].name) == 0)
            return -2;
    }

    if (cmd_load->len >= cmd_load->cap) {
        size_t newcap = cmd_load->cap + (cmd_load->cap >> 1) + 4;
        cmd_t *a =
            mi_realloc(cmd_load->array, sizeof(cmd_load->array[0]) * newcap);

        if (!a)
            return -1;

        cmd_load->array = a;
        cmd_load->cap = newcap;
    }

    cmd_load->array[cmd_load->len].name = mi_strdup(name);
    cmd_load->array[cmd_load->len].fn = fn;
    ++cmd_load->len;

    return 0;
}

void cmd_list_free() {

    if (cmd_load == NULL)
        return;
    while (cmd_load->len > 0) {
        cmd_t *t = &cmd_load->array[--cmd_load->len];
        mi_free(t->name);
    }
    mi_free(cmd_load->array);
    cmd_load->array = NULL;
    cmd_load->cap = 0;

    mi_free(cmd_load);
    atomic_store_explicit(&cmd_list, NULL, memory_order_release);
}

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define LIB_T HMODULE
#else
#include <dlfcn.h>
#define LIB_T void *
#endif

typedef struct {
    char *filename;
    LIB_T lib;
} lib_t;

typedef struct {
    lib_t *array;
    int cap;
    int len;
} lib_list_t;

static _Atomic(lib_list_t *) list = NULL;
#define l_load atomic_load_explicit(&list, memory_order_acquire)

void dll_list_free() {

    if (l_load == NULL)
        return;
    while (l_load->len > 0) {
        lib_t *t = &l_load->array[--l_load->len];
        mi_free(t->filename);
#if defined(_WIN32) || defined(_WIN64)
        if (t->lib)
            FreeLibrary(t->lib);
#else
        if (t->lib)
            dlclose(t->lib);
#endif
    }
    mi_free(l_load->array);
    l_load->array = NULL;
    l_load->cap = 0;

    mi_free(l_load);
    atomic_store_explicit(&list, NULL, memory_order_release);
}

static int dll_list_add(const char *filename, LIB_T lib) {
    if (!l_load) {
        atomic_store_explicit(&list, mi_malloc(sizeof(lib_list_t)),
                              memory_order_release);
        l_load->array = NULL;
        l_load->cap = 0;
        l_load->len = 0;
    }

    if (l_load->len >= l_load->cap) {
        size_t newcap = l_load->cap + (l_load->cap >> 1) + 4;
        lib_t *a = (lib_t *)mi_realloc(l_load->array,
                                       sizeof(l_load->array[0]) * newcap);
        if (!a)
            return -1;
        l_load->array = a;
        l_load->cap = newcap;
    }

    l_load->array[l_load->len].filename = mi_strdup(filename);
    if (!l_load->array[l_load->len].filename)
        return -1;
    l_load->array[l_load->len].lib = lib;
    ++l_load->len;

    return 0;
}

static LIB_T search_dll_list(const char *filename) {
    if (!l_load)
        return NULL;
    for (int i = 0; i < l_load->len; ++i) {
        if (l_load->array[i].filename == filename) {
            return l_load->array[i].lib;
        }
    }
    return NULL;
}

init_cmodule_fn_t load_dynamic(const char *filename, const char *fn_name,
                               char **error_msg) {
    size_t pos = 0;
    char *error = NULL;
    init_cmodule_fn_t func = NULL;
    LIB_T lib = search_dll_list(filename);

#if defined(_WIN32) || defined(_WIN64)
    if (!lib)
        lib = LoadLibrary(filename);
    if (lib) {
        func = (init_cmodule_fn_t)GetProcAddress(lib, fn_name);
        if (func == NULL) {
            error = mi_malloc(256);
            pos = snprintf(error, 256, "GetProcAddress error: %s", fn_name);
            snprintf(error + pos, 256 - pos, "\n  GetLastError() code: %ld",
                     GetLastError());
            goto fail;
        }
    } else {
        error = mi_malloc(256);
        snprintf(error, 256, "LoadLibrary error: %s", filename);
        goto fail;
    }
#else
    if (!lib)
        lib = dlopen(filename, RTLD_LAZY);
    if (lib) {
        func = (init_cmodule_fn_t)dlsym(lib, funcname);
        if (func == NULL) {
            error = mi_malloc(256);
            pos = snprintf(error, 256, "dlsym error: %s", funcname);
            snprintf(error + pos, 256 - pos, "\n  dlerror: %s", dlerror());
            goto fail;
        }
    } else {
        error = mi_malloc(256);
        snprintf(error, 256, "dlopen error: %s", filename);
        goto fail;
    }

#endif

    if (dll_list_add(filename, lib) != 0) {
        error = mi_malloc(256);
        snprintf(error, 256, "dll_list: memory can't apply");
        goto fail;
    }
    return func;
fail:
    if (error_msg) {
        *error_msg = error;
    } else {
        mi_free(error);
    }
    return NULL;
}

static const JSCFunctionListEntry js_ffi_funcs[] = {
    JS_PROP_STRING_DEF("p_suffix", p_suffix, JS_PROP_ENUMERABLE),
};

static int js_ffi_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, js_ffi_funcs, countof(js_ffi_funcs));
}

static JSModuleDef *js_ffi_init_module(JSContext *ctx,
                                       const char *module_name) {
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_ffi_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_ffi_funcs, countof(js_ffi_funcs));
    return m;
}

static _Atomic(JSClassID) plugin_class_id = 0;
static _Atomic(plugin_t *) _plugin = NULL;

typedef enum {
    PLUGIN_TYPE_JS,
    PLUGIN_TYPE_C,
    PLUGIN_TYPE_NULL,
} plugin_type;

struct plugin {
    JSContext *ctx;
    char *filename;
    plugin_type type;
};

plugin_t *load_plugin(JSContext *ctx, const char *plugin_name) {
    // TODO: remote plugin
    char *plugin_dir = concat(plugin_name, "/load.js");
    panda_js *pjs = panda_new_js(JS_GetRuntime(ctx), 0, NULL);
    if (pjs == NULL) {
        return NULL;
    }
    panda_js_eval(pjs, plugin_dir);
    mi_free(plugin_dir);

    panda_js_run(pjs);
    plugin_t *p = atomic_load_explicit(&_plugin, memory_order_acquire);
    atomic_store_explicit(&_plugin, NULL, memory_order_release);

    panda_free_js(pjs);

    if (!p)
        JS_ThrowInternalError(ctx, "Check whether function load is called");

    return p;
}

static void plugin_finalizer(JSRuntime *rt, JSValue val) {
    plugin_t *p = JS_GetOpaque(
        val, atomic_load_explicit(&plugin_class_id, memory_order_relaxed));
    if (p) {
        js_free(p->ctx, p->filename);
        js_free(p->ctx, p);
    }
}

static JSClassDef plugin_class = {
    .class_name = "plugin",
    .finalizer = plugin_finalizer,
};

static JSValue plugin_ctor(JSContext *ctx, JSValueConst new_target, int argc,
                           JSValueConst *argv) {
    JSValue obj = JS_UNDEFINED;
    plugin_t *p = NULL;
    JSValue proto;
    JSClassID class_id;
    JSValueConst proto_val;

    class_id = atomic_load_explicit(&plugin_class_id, memory_order_relaxed);
    proto_val = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto_val))
        goto fail;
    proto = JS_DupValue(ctx, proto_val);
    JS_FreeValue(ctx, proto_val);
    obj = JS_NewObjectProtoClass(ctx, proto, class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;

    p = js_mallocz(ctx, sizeof(*p));
    if (!p) {
        JS_ThrowOutOfMemory(ctx);
        goto fail;
    }
    p->ctx = ctx;
    p->type = PLUGIN_TYPE_NULL;
    p->filename = NULL;
    JS_SetOpaque(obj, p);
    return obj;
fail:
    js_free(ctx, p);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue plugin_c(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
    plugin_t *p = JS_GetOpaque(
        this_val, atomic_load_explicit(&plugin_class_id, memory_order_relaxed));
    if (!p)
        return JS_EXCEPTION;
    if (argc > 0) {
        const char *filename = JS_ToCString(ctx, argv[0]);
        if (!filename)
            return JS_EXCEPTION;
        p->type = PLUGIN_TYPE_C;
        if (p->filename)
            js_free(ctx, p->filename);
        p->filename = js_strdup(ctx, filename);
        if (!p->filename) {
            JS_ThrowOutOfMemory(ctx);
            return JS_EXCEPTION;
        }
        JS_FreeCString(ctx, filename);
    }
    return JS_UNDEFINED;
}

static JSValue plugin_js(JSContext *ctx, JSValueConst this_val, int argc,
                         JSValueConst *argv) {
    plugin_t *p = JS_GetOpaque(
        this_val, atomic_load_explicit(&plugin_class_id, memory_order_relaxed));
    if (!p)
        return JS_EXCEPTION;
    if (argc > 0) {
        const char *filename = JS_ToCString(ctx, argv[0]);
        if (!filename)
            return JS_EXCEPTION;
        p->type = PLUGIN_TYPE_JS;
        if (p->filename)
            js_free(ctx, p->filename);
        p->filename = js_strdup(ctx, filename);
        if (!p->filename) {
            JS_ThrowOutOfMemory(ctx);
            return JS_EXCEPTION;
        }
        JS_FreeCString(ctx, filename);
    }
    return JS_UNDEFINED;
}

static JSValue load(JSContext *ctx, JSValueConst this_val, int argc,
                    JSValueConst *argv) {
    plugin_t *p = JS_GetOpaque(
        this_val, atomic_load_explicit(&plugin_class_id, memory_order_relaxed));
    if (!p)
        return JS_EXCEPTION;

    plugin_t *new = js_malloc(ctx, sizeof(plugin_t));

    new->ctx = NULL;
    new->type = p->type;
    new->filename = js_strdup(ctx, p->filename);

    atomic_store_explicit(&_plugin, new, memory_order_release);

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry plugin_fun[] = {
    JS_CFUNC_DEF("c", 1, plugin_c),
    JS_CFUNC_DEF("js", 1, plugin_js),
    JS_CFUNC_DEF("load", 0, load),
};

static int js_plugin_init(JSContext *ctx, JSModuleDef *m) {
    JSValue proto;
    JSValue obj;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, plugin_fun, countof(plugin_fun));
    JS_SetClassProto(
        ctx, atomic_load_explicit(&plugin_class_id, memory_order_acquire),
        proto);

    obj = JS_NewCFunction2(ctx, plugin_ctor, "plugin", 0, JS_CFUNC_constructor,
                           0);
    JS_SetConstructor(ctx, obj, proto);
    JS_SetModuleExport(ctx, m, "plugin", obj);
    return 0;
}

static JSModuleDef *js_plugin_init_module(JSContext *ctx,
                                          const char *module_name) {
    JSModuleDef *m;
    JSClassID id;

    m = JS_NewCModule(ctx, module_name, js_plugin_init);
    if (!m)
        return NULL;

    if (atomic_load_explicit(&plugin_class_id, memory_order_acquire) == 0) {
        int ret;
        ret = JS_NewClass(JS_GetRuntime(ctx), &id, &plugin_class);
        if (ret < 0)
            return NULL;
        atomic_store_explicit(&plugin_class_id, id, memory_order_release);
    }

    JS_AddModuleExport(ctx, m, "plugin");

    return m;
}

void panda_js_init_plugin() {
    cmodule_list_add("plugin", js_plugin_init_module);
}

JSModuleDef *panda_js_init_module(JSContext *ctx, const char *module_name,
                                  char **_filename) {
    char _module_name[MODULE_MAX_NAME] = {0};
    char *str1 = NULL;
    char *str2 = NULL;
    plugin_t *p = NULL;
    init_cmodule_fn_t fn = NULL;
    if (!module_name) {
        return NULL;
    }

    if (!strcmp(module_name, "std")) {
        return js_init_module_std(ctx, module_name);
    }
    if (!strcmp(module_name, "os")) {
        return js_init_module_os(ctx, module_name);
    }

    if (has_prefix(module_name, "plugin:")) {
        int _module_len = strlen(module_name) - strlen("plugin:");

        if (_module_len > MODULE_MAX_NAME) {
            JS_ThrowInternalError(ctx, "module name too long");
            js_std_dump_error(ctx);
            return NULL;
        }

        snprintf(_module_name, MODULE_MAX_NAME - 1, "%s",
                 module_name + strlen("plugin:"));

        p = load_plugin(ctx, _module_name);

        if (!p) {
            js_std_dump_error(ctx);
            return NULL;
        }

        str1 = concat(_module_name, "/");
        str2 = concat(str1, p->filename);
        mi_free(str1);

        if (p->type == PLUGIN_TYPE_C) {
            JSModuleDef *m = panda_js_init_module(ctx, str2, _filename);
            mi_free(str2);
            return m;
        } else {
            *_filename = js_strdup(ctx, str2);
            mi_free(str2);
        }

        js_free(ctx, p->filename);
        js_free(ctx, p);

        return NULL;
    } else if (has_suffix(module_name, p_suffix)) {
        int _module_len = strlen(module_name) - strlen(p_suffix);

        if (_module_len > MODULE_MAX_NAME) {
            JS_ThrowInternalError(ctx, "module name too long");
            js_std_dump_error(ctx);
            return NULL;
        }
        int pos = 0;
        for (size_t i = 0; i < _module_len; ++i) {
            if (module_name[i] == '\\' || module_name[i] == '/')
                pos = i;
        }
        snprintf(_module_name, MODULE_MAX_NAME - 1, "%.*s",
                 _module_len - pos - 1, module_name + pos + 1);

        fn = load_dynamic(module_name, "js_init_module", &str1);

        if (fn == NULL) {
            JS_ThrowInternalError(ctx, "load dynamic module error :%s", str1);
            mi_free(str1);
            js_std_dump_error(ctx);
            return NULL;
        }

        return fn(ctx, _module_name);
    }

    if (cmodule_list_find(module_name, &fn) == 0) {
        return fn(ctx, module_name);
    }

    return NULL;
}

void panda_js_module_init() {
    cmodule_list_add("panda:plugin", js_plugin_init_module);
    cmodule_list_add("panda:ffi", js_ffi_init_module);
}
void panda_js_module_free() {
    cmodule_list_free();
    cmd_list_free();
    dll_list_free();
}