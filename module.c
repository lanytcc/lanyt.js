
#include "module.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

#include <cutils.h>
#include <mimalloc.h>
#include <quickjs-libc.h>
#include <quickjs.h>

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
    bool i = false;
    if (cl_load == NULL)
        return -2;

    for (int i = 0; i < cl_load->len; ++i) {
        if (strcmp(name, cl_load->array[i].name) == 0) {
            *fn = cl_load->array[i].fn;
            i = true;
        }
    }

    if (i)
        return 0;

    return -1;
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

static void dll_list_free() {

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
        func = (init_cmodule_fn_t)dlsym(lib, fn_name);
        if (func == NULL) {
            error = mi_malloc(256);
            pos = snprintf(error, 256, "dlsym error: %s", fn_name);
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

JSModuleDef *lanyt_js_init_module(JSContext *ctx, const char *module_name) {
    char _module_name[MODULE_MAX_NAME] = {0};
    char *str1 = NULL;
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

    if (has_suffix(module_name, p_suffix)) {
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

void lanyt_js_module_init() {
    cmodule_list_add("lanyt:ffi", js_ffi_init_module);
}
void lanyt_js_module_free() {
    cmodule_list_free();
    dll_list_free();
}