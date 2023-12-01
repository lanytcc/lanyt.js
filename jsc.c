
#include "jsc.h"
#include "module.h"
#include "quickjs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <mimalloc.h>

#if defined(__APPLE__)
#define MALLOC_OVERHEAD 0
#else
#define MALLOC_OVERHEAD 8
#endif

static void *pjs_def_malloc(JSMallocState *s, size_t size) {
    void *ptr;

    if (size == 0 || unlikely(s->malloc_size + size > s->malloc_limit))
        return NULL;

    ptr = mi_malloc(size);
    if (!ptr)
        return NULL;

    s->malloc_count++;
    s->malloc_size += mi_usable_size(ptr) + MALLOC_OVERHEAD;
    return ptr;
}

static void pjs_def_free(JSMallocState *s, void *ptr) {
    if (!ptr)
        return;
    s->malloc_count--;
    s->malloc_size -= mi_usable_size(ptr) + MALLOC_OVERHEAD;
    mi_free(ptr);
}

static void *pjs_def_realloc(JSMallocState *s, void *ptr, size_t size) {
    size_t old_size;

    if (!ptr) {
        if (size == 0)
            return NULL;
        return pjs_def_malloc(s, size);
    }

    old_size = mi_usable_size(ptr);
    if (size == 0) {
        s->malloc_count--;
        s->malloc_size -= old_size + MALLOC_OVERHEAD;
        mi_free(ptr);
        return NULL;
    }
    if (s->malloc_size + size - old_size > s->malloc_limit)
        return NULL;

    ptr = mi_realloc(ptr, size);
    if (!ptr)
        return NULL;

    s->malloc_size += mi_usable_size(ptr) - old_size;
    return ptr;
}

static size_t pjs_def_malloc_usable_size(const void *ptr) {
    return mi_usable_size(ptr);
}

static const JSMallocFunctions def_malloc_funcs = {
    pjs_def_malloc,
    pjs_def_free,
    pjs_def_realloc,
    pjs_def_malloc_usable_size,
};

static JSContext *JS_NewCustomContext(JSRuntime *rt) {
    JSContext *ctx = JS_NewContextRaw(rt);
    if (!ctx) {
        return NULL;
    }
    JS_AddIntrinsicBaseObjects(ctx);
    JS_AddIntrinsicDate(ctx);
    JS_AddIntrinsicEval(ctx);
    JS_AddIntrinsicStringNormalize(ctx);
    JS_AddIntrinsicRegExp(ctx);
    JS_AddIntrinsicJSON(ctx);
    JS_AddIntrinsicProxy(ctx);
    JS_AddIntrinsicMapSet(ctx);
    JS_AddIntrinsicTypedArrays(ctx);
    JS_AddIntrinsicPromise(ctx);
    return ctx;
}

JSRuntime *panda_jsc_new_rt() {
    JSRuntime *p = JS_NewRuntime2(&def_malloc_funcs, NULL);
    if (!p)
        return NULL;
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(p);
    return p;
}

void panda_jsc_free_rt(JSRuntime *p) {
    js_std_free_handlers(p);
    JS_FreeRuntime(p);
}

struct panda_js {
    JSContext *ctx;
    int byte_swap;
    size_t bytecode_len;
    uint8_t *bytecode;
    struct panda_js *next;
};

static panda_js *panda_new_js_noctx(JSRuntime *rt);

static int to_bytecode(JSContext *ctx, JSValueConst obj, panda_js *pjs) {
    uint8_t *bytecode_buf;
    size_t bytecode_buf_len;
    int flags;
    flags = JS_WRITE_OBJ_BYTECODE;
    if (pjs->byte_swap)
        flags |= JS_WRITE_OBJ_BSWAP;
    bytecode_buf = JS_WriteObject(ctx, &bytecode_buf_len, obj, flags);

    if (!bytecode_buf) {
        return -1;
    }

    pjs->bytecode_len = bytecode_buf_len;
    pjs->bytecode = bytecode_buf;

    return 0;
}

static JSModuleDef *jsc_module_loader(JSContext *ctx, const char *module_name,
                                      void *opaque) {

    JSModuleDef *m;
    size_t buf_len;
    uint8_t *buf;
    JSValue func_val;
    panda_js *pjs = opaque;
    char *filename = NULL;

    /* check if it is a declared C or system module */
    m = panda_js_init_module(ctx, module_name, &filename);

    if (m) {
        return m;
    }

    if (filename) {
        buf = js_load_file(ctx, &buf_len, filename);
        js_free(ctx, filename);
    } else {
        buf = js_load_file(ctx, &buf_len, module_name);
    }
    if (!buf) {
        size_t len = strlen(module_name);
        char *module_name_buf = (char *)js_malloc(ctx, len + 4);
        snprintf(module_name_buf, len + 4, "%s.js", module_name);
        buf = js_load_file(ctx, &buf_len, module_name_buf);
        js_free(ctx, module_name_buf);
    }
    if (!buf) {
        JS_ThrowInternalError(ctx, "could not load module filename '%s'",
                              module_name);
        js_std_dump_error(ctx);
        return NULL;
    }

    /* compile the module */
    func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                       JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    js_free(ctx, buf);

    if (JS_IsException(func_val)) {
        js_std_dump_error(ctx);
        return NULL;
    }

    while (pjs->next != NULL)
        pjs = pjs->next;

    pjs->next = panda_new_js_noctx(JS_GetRuntime(ctx));
    if (to_bytecode(ctx, func_val, pjs->next)) {
        JS_FreeValue(ctx, func_val);
        JS_ThrowInternalError(ctx, "could not write module bytecode '%s'",
                              module_name);
        return NULL;
    }

    /* the module is already referenced, so we must free it */
    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);

    return m;
}

static int compile_file(JSContext *ctx, panda_js *pjs, const char *filename) {
    uint8_t *buf;
    int eval_flags;
    JSValue obj;
    size_t buf_len;

    char *pc = "<panda>";
    char pc_buf[8] = {0};
    snprintf(pc_buf, 8, "%s", filename);

    if (strcmp(pc_buf, pc))
        buf = js_load_file(ctx, &buf_len, filename);
    else {
        buf_len = strlen(filename) - 7;
        buf = (uint8_t *)filename + 7;
    }
    if (!buf) {
        JS_ThrowTypeError(ctx, "Could not load '%s'", filename);
        js_std_dump_error(ctx);
        return -1;
    }
    eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
    int module = JS_DetectModule((const char *)buf, buf_len);

    if (module)
        eval_flags |= JS_EVAL_TYPE_MODULE;
    else
        eval_flags |= JS_EVAL_TYPE_GLOBAL;

    obj = JS_Eval(ctx, (const char *)buf, buf_len, filename, eval_flags);
    if (strcmp(pc_buf, pc))
        js_free(ctx, buf);

    if (JS_IsException(obj)) {
        js_std_dump_error(ctx);
        return -2;
    }

    to_bytecode(ctx, obj, pjs);
    JS_FreeValue(ctx, obj);

    return 0;
}

static panda_js *panda_new_js_noctx(JSRuntime *rt) {
    panda_js *r = mi_malloc(sizeof(panda_js));

    if (!r)
        return NULL;

    r->ctx = NULL;
    r->byte_swap = 0;
    r->bytecode_len = 0;
    r->bytecode = NULL;
    r->next = NULL;

    return r;
}

panda_js *panda_new_js(JSRuntime *rt, int argc, char **argv) {
    panda_js *r = mi_malloc(sizeof(panda_js));

    if (!r)
        return NULL;

    r->ctx = JS_NewCustomContext(rt);
    if (!r->ctx) {
        mi_free(r);
        return NULL;
    }
    r->byte_swap = 0;
    r->bytecode_len = 0;
    r->bytecode = NULL;
    r->next = NULL;
    JS_SetModuleLoaderFunc(rt, NULL, jsc_module_loader, r);
    js_std_add_helpers(r->ctx, argc, argv);

    return r;
}

static void free_help(JSContext *ctx, panda_js *pjs) {
    if (pjs == NULL)
        return;
    js_free(ctx, pjs->bytecode);
    free_help(ctx, pjs->next);
    mi_free(pjs);
}

void panda_free_js(panda_js *pjs) {
    JSContext *ctx;
    if (pjs == NULL)
        return;
    ctx = pjs->ctx;
    free_help(ctx, pjs);
    JS_FreeContext(ctx);
}

int panda_js_eval(panda_js *pjs, const char *filename) {
    if (!pjs) {
        printf("pjs is null\n");
        return -1;
    }
    return compile_file(pjs->ctx, pjs, filename);
}

int panda_js_run(panda_js *pjs) {
    if (!pjs) {
        printf("pjs is null\n");
        return -1;
    }
    panda_js *n = pjs->next;
    while (n != NULL) {
        if (n->bytecode == NULL)
            return -2;
        js_std_eval_binary(pjs->ctx, n->bytecode, n->bytecode_len, 1);
        n = n->next;
    }
    if (pjs->bytecode == NULL)
        return -2;
    js_std_eval_binary(pjs->ctx, pjs->bytecode, pjs->bytecode_len, 0);

    js_std_loop(pjs->ctx);

    return 0;
}

int panda_js_save(panda_js *pjs, const char *filename, int is_debug) {
    if (!pjs) {
        printf("pjs is null\n");
        return -1;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        JS_ThrowInternalError(pjs->ctx, "could not open '%s'", filename);
        js_std_dump_error(pjs->ctx);
        return -1;
    }

    while (pjs != NULL) {
        if (pjs->bytecode == NULL)
            goto fail;
        if (fwrite(&pjs->bytecode_len, sizeof(pjs->bytecode_len), 1, fp) != 1)
            goto fail;
        if (fwrite(pjs->bytecode, 1, pjs->bytecode_len, fp) !=
            pjs->bytecode_len)
            goto fail;
        pjs = pjs->next;
    }

    size_t _tmp = 0;
    if (fwrite(&_tmp, sizeof(_tmp), 1, fp) != 1)
        goto fail;

    fclose(fp);
    return 0;
fail:
    fclose(fp);
    JS_ThrowInternalError(pjs->ctx, "could not write '%s'", filename);
    js_std_dump_error(pjs->ctx);
    return -2;
}

int panda_js_read(panda_js *pjs, const char *filename, int is_debug) {
    uint8_t *buf, *buf1;
    size_t buf_len;

    if (!pjs) {
        printf("pjs is null\n");
        return -1;
    }

    if (!filename) {
        JS_ThrowInternalError(pjs->ctx, "filename is null");
        js_std_dump_error(pjs->ctx);
        return -1;
    }

    buf = js_load_file(pjs->ctx, &buf_len, filename);
    buf1 = buf;

    if (!buf) {
        JS_ThrowInternalError(pjs->ctx, "could not load '%s'", filename);
        js_std_dump_error(pjs->ctx);
        return -2;
    }

    int f = 0;
    while (buf_len > 0) {
        size_t len;
        uint8_t *p;

        len = *(size_t *)buf;
        if (len == 0)
            break;
        p = buf + sizeof(size_t);

        if (len > buf_len) {
            JS_ThrowInternalError(pjs->ctx, "invalid file format");
            js_std_dump_error(pjs->ctx);
            goto fail;
        }

        panda_js *n = panda_new_js_noctx(JS_GetRuntime(pjs->ctx));
        if (!n) {
        mem_fail:
            JS_ThrowInternalError(pjs->ctx, "could not apply for memory");
            js_std_dump_error(pjs->ctx);
            goto fail;
        }

        n->bytecode_len = len;
        n->bytecode = js_malloc(pjs->ctx, len);
        if (!n->bytecode) {
            js_free(pjs->ctx, n);
            goto mem_fail;
        }

        memcpy(n->bytecode, p, len);

        if (!f) {
            pjs->bytecode_len = len;
            pjs->bytecode = n->bytecode;
            f = 1;
            js_free(pjs->ctx, n);
        } else {
            panda_js *t = pjs->next;
            while (t->next != NULL) {
                t = t->next;
            }
            t->next = n;
        }

        buf += len + sizeof(size_t);
        buf_len -= len + sizeof(size_t);
    }

    js_free(pjs->ctx, buf1);
    return 0;
fail:
    js_free(pjs->ctx, buf1);
    return -3;
}