
#include "jsc.h"
#include "module.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <mimalloc.h>
#include <string.h>

#if defined(__APPLE__)
#define MALLOC_OVERHEAD 0
#else
#define MALLOC_OVERHEAD 8
#endif

static void *ljs_def_malloc(JSMallocState *s, size_t size) {
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

static void ljs_def_free(JSMallocState *s, void *ptr) {
    if (!ptr)
        return;
    s->malloc_count--;
    s->malloc_size -= mi_usable_size(ptr) + MALLOC_OVERHEAD;
    mi_free(ptr);
}

static void *ljs_def_realloc(JSMallocState *s, void *ptr, size_t size) {
    size_t old_size;

    if (!ptr) {
        if (size == 0)
            return NULL;
        return ljs_def_malloc(s, size);
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

static size_t ljs_def_malloc_usable_size(const void *ptr) {
    return mi_usable_size(ptr);
}

static const JSMallocFunctions def_malloc_funcs = {
    ljs_def_malloc,
    ljs_def_free,
    ljs_def_realloc,
    ljs_def_malloc_usable_size,
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

JSRuntime *lanyt_jsc_new_rt() {
    JSRuntime *p = JS_NewRuntime2(&def_malloc_funcs, NULL);
    if (!p)
        return NULL;
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(p);
    return p;
}

void lanyt_jsc_free_rt(JSRuntime *p) {
    js_std_free_handlers(p);
    JS_FreeRuntime(p);
}

struct lanyt_js {
    JSContext *ctx;
    int byte_swap;
    size_t bytecode_len;
    uint8_t *bytecode;
    char *filename;
    struct lanyt_js *next;
};

static lanyt_js *lanyt_new_js_noctx(JSRuntime *rt);

static int to_bytecode(JSContext *ctx, JSValueConst obj, lanyt_js *ljs) {
    uint8_t *bytecode_buf;
    size_t bytecode_buf_len;
    int flags;
    flags = JS_WRITE_OBJ_BYTECODE;
    if (ljs->byte_swap)
        flags |= JS_WRITE_OBJ_BSWAP;
    bytecode_buf = JS_WriteObject(ctx, &bytecode_buf_len, obj, flags);

    if (!bytecode_buf) {
        return -1;
    }

    ljs->bytecode_len = bytecode_buf_len;
    ljs->bytecode = bytecode_buf;

    return 0;
}

static JSModuleDef *jsc_module_loader(JSContext *ctx, const char *module_name,
                                      void *opaque) {

    JSModuleDef *m;
    size_t buf_len;
    uint8_t *buf;
    JSValue func_val;
    lanyt_js *ljs = opaque;

    /* check if it is a declared C or system module */
    m = lanyt_js_init_module(ctx, module_name);

    if (m) {
        return m;
    }

    buf = js_load_file(ctx, &buf_len, module_name);

    if (!buf) {
        size_t len = strlen(module_name);
        char *module_name_buf = js_malloc(ctx, len + 4);
        if (!module_name_buf) {
            js_std_dump_error(ctx);
            return NULL;
        }
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

    while (ljs->next != NULL)
        ljs = ljs->next;

    ljs->next = lanyt_new_js_noctx(JS_GetRuntime(ctx));
    if (to_bytecode(ctx, func_val, ljs->next)) {
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

static int compile_file(JSContext *ctx, lanyt_js *ljs, const char *filename) {
    uint8_t *buf;
    int eval_flags;
    JSValue obj;
    size_t buf_len;

    char *pc = "<lanyt>";
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
    dump:
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
    if (strcmp(pc_buf, pc)) {
        ljs->filename =
            js_malloc(ctx, strlen(filename) + 1 + 2 + strlen((char *)buf));
        if (!ljs->filename) {
            JS_FreeValue(ctx, obj);
            goto dump;
        }
        sprintf(ljs->filename, "<%s>", filename);
        strcat(ljs->filename, (char *)buf);
        js_free(ctx, buf);
    } else {
        ljs->filename = js_strdup(ctx, (char *)buf);
    }
    if (!ljs->filename) {
        JS_FreeValue(ctx, obj);
        goto dump;
    }
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, obj);
        goto dump;
    }
    to_bytecode(ctx, obj, ljs);
    JS_FreeValue(ctx, obj);
    return 0;
}

static lanyt_js *lanyt_new_js_noctx(JSRuntime *rt) {
    lanyt_js *r = mi_malloc(sizeof(lanyt_js));

    if (!r)
        return NULL;

    r->ctx = NULL;
    r->byte_swap = 0;
    r->bytecode_len = 0;
    r->bytecode = NULL;
    r->filename = NULL;
    r->next = NULL;

    return r;
}

lanyt_js *lanyt_new_js(JSRuntime *rt) {
    lanyt_js *r = mi_malloc(sizeof(lanyt_js));

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
    r->filename = NULL;
    r->next = NULL;
    JS_SetModuleLoaderFunc(rt, NULL, jsc_module_loader, r);

    return r;
}

JSContext *lanyt_js_get_ctx(lanyt_js *ljs) { return ljs->ctx; }
lanyt_js *lanyt_js_get_next(lanyt_js *ljs) { return ljs->next; }
char *lanyt_js_get_filename(lanyt_js *ljs) { return ljs->filename; }

static void free_help(JSContext *ctx, lanyt_js *ljs) {
    if (ljs == NULL)
        return;
    js_free(ctx, ljs->bytecode);
    js_free(ctx, ljs->filename);
    free_help(ctx, ljs->next);
    mi_free(ljs);
}

void lanyt_free_js(lanyt_js *ljs) {
    JSContext *ctx;
    if (ljs == NULL)
        return;
    ctx = ljs->ctx;
    free_help(ctx, ljs);
    JS_FreeContext(ctx);
}

int lanyt_js_eval(lanyt_js *ljs, const char *filename) {
    if (!ljs) {
        printf("ljs is null\n");
        return -1;
    }
    return compile_file(ljs->ctx, ljs, filename);
}

static int run(JSContext *ctx, const uint8_t *buf, size_t buf_len,
               int load_only, int silent) {
    JSValue obj, val;
    obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj))
        goto exception;
    if (load_only) {
        if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
            js_module_set_import_meta(ctx, obj, FALSE, FALSE);
        }
    } else {
        if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
            if (JS_ResolveModule(ctx, obj) < 0) {
                JS_FreeValue(ctx, obj);
                goto exception;
            }
            js_module_set_import_meta(ctx, obj, FALSE, TRUE);
        }
        val = JS_EvalFunction(ctx, obj);
        if (JS_IsException(val)) {
        exception:
            if (!silent)
                js_std_dump_error(ctx);
            return -1;
        }
        JS_FreeValue(ctx, val);
    }

    return 0;
}

int lanyt_js_run(lanyt_js *ljs, int silent) {
    if (!ljs) {
        printf("ljs is null\n");
        return -1;
    }
    lanyt_js *n = ljs->next;
    while (n != NULL) {
        if (n->bytecode == NULL)
            return -2;
        if (run(ljs->ctx, n->bytecode, n->bytecode_len, 1, silent))
            return -3;
        n = n->next;
    }
    if (ljs->bytecode == NULL)
        return -2;
    if (run(ljs->ctx, ljs->bytecode, ljs->bytecode_len, 0, silent))
        return -4;

    js_std_loop(ljs->ctx);

    return 0;
}

int lanyt_js_save(lanyt_js *ljs, const char *filename, int debug) {
    if (!ljs) {
        printf("ljs is null\n");
        return -1;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        JS_ThrowInternalError(ljs->ctx, "could not open '%s'", filename);
        js_std_dump_error(ljs->ctx);
        return -1;
    }

    if (fwrite(&debug, sizeof(debug), 1, fp) != 1)
        goto fail;
    while (ljs != NULL) {
        if (ljs->bytecode == NULL) {
            JS_ThrowInternalError(ljs->ctx, "bytecode is null");
            goto fail;
        }
        if (fwrite(&ljs->bytecode_len, sizeof(ljs->bytecode_len), 1, fp) != 1) {
            JS_ThrowInternalError(ljs->ctx, "could not write bytecode_len");
            goto fail;
        }
        if (fwrite(ljs->bytecode, 1, ljs->bytecode_len, fp) !=
            ljs->bytecode_len) {
            JS_ThrowInternalError(ljs->ctx, "could not write bytecode");
            goto fail;
        }
        if (!debug) {
            ljs = ljs->next;
            continue;
        }
        if (ljs->filename) {
            uint64_t len = strlen(ljs->filename);
            if (fwrite(&len, sizeof(len), 1, fp) != 1) {
                JS_ThrowInternalError(
                    ljs->ctx, "could not write file len: '%s'", ljs->filename);
                goto fail;
            }
            if (fwrite(ljs->filename, 1, len, fp) != len) {
                JS_ThrowInternalError(ljs->ctx, "could not write file: %s",
                                      ljs->filename);
                goto fail;
            }
        } else {
            char *buf = "(external call)";
            uint64_t len = strlen(buf);
            if (fwrite(&len, sizeof(len), 1, fp) != 1) {
                JS_ThrowInternalError(
                    ljs->ctx, "could not write file len: '%s'", ljs->filename);
                goto fail;
            }
            if (fwrite(buf, 1, len, fp) != len) {
                JS_ThrowInternalError(ljs->ctx, "could not write file: %s",
                                      ljs->filename);
                goto fail;
            }
        }
        ljs = ljs->next;
    }

    uint64_t _tmp = 0;
    if (fwrite(&_tmp, sizeof(_tmp), 1, fp) != 1)
        goto fail;

    fclose(fp);
    return 0;
fail:
    fclose(fp);
    js_std_dump_error(ljs->ctx);
    return -2;
}

int lanyt_js_read(lanyt_js *ljs, const char *filename, int *debug) {
    uint8_t *buf, *buf1;
    uint64_t buf_len;
    int is_debug = 0;
    if (!ljs) {
        printf("ljs is null\n");
        return -1;
    }
    if (!filename) {
        JS_ThrowInternalError(ljs->ctx, "filename is null");
        js_std_dump_error(ljs->ctx);
        return -1;
    }
    buf = js_load_file(ljs->ctx, &buf_len, filename);
    buf1 = buf;
    if (!buf) {
        JS_ThrowInternalError(ljs->ctx, "could not load '%s'", filename);
        js_std_dump_error(ljs->ctx);
        return -2;
    }
    is_debug = *(int *)buf;
    buf += sizeof(int);
    buf_len -= sizeof(int);
    if (debug)
        *debug = is_debug;
    int f = 0;
    while (buf_len > 0) {
        uint64_t len;
        uint8_t *p;

        len = *(uint64_t *)buf;
        if (len == 0)
            break;
        p = buf + sizeof(uint64_t);
        if (len > buf_len) {
            JS_ThrowInternalError(ljs->ctx, "invalid file format");
            js_std_dump_error(ljs->ctx);
            goto fail;
        }

        lanyt_js *n = lanyt_new_js_noctx(JS_GetRuntime(ljs->ctx));
        if (!n) {
        mem_fail:
            js_std_dump_error(ljs->ctx);
            goto fail;
        }
        n->bytecode_len = len;
        n->bytecode = js_malloc(ljs->ctx, len);
        if (!n->bytecode) {
            js_free(ljs->ctx, n);
            goto mem_fail;
        }
        memcpy(n->bytecode, p, len);

        if (is_debug) {
            buf += len + sizeof(uint64_t);
            buf_len -= len + sizeof(uint64_t);

            if (buf_len < sizeof(uint64_t)) {
                JS_ThrowInternalError(ljs->ctx, "invalid file format");
                js_std_dump_error(ljs->ctx);
                goto fail;
            }

            len = *(uint64_t *)buf;
            p = buf + sizeof(uint64_t);
            if (len > buf_len) {
                JS_ThrowInternalError(ljs->ctx, "invalid file format");
                js_std_dump_error(ljs->ctx);
                goto fail;
            }
            n->filename = js_malloc(ljs->ctx, len + 1);
            if (!n->filename) {
                js_free(ljs->ctx, n->bytecode);
                js_free(ljs->ctx, n);
                goto mem_fail;
            }
            memcpy(n->filename, p, len);
            n->filename[len] = '\0';
        }

        if (!f) {
            ljs->bytecode_len = len;
            ljs->bytecode = n->bytecode;
            f = 1;
            js_free(ljs->ctx, n);
        } else {
            lanyt_js *t = ljs->next;
            while (t->next != NULL) {
                t = t->next;
            }
            t->next = n;
        }

        buf += len + sizeof(uint64_t);
        buf_len -= len + sizeof(uint64_t);
    }

    js_free(ljs->ctx, buf1);
    return 0;
fail:
    js_free(ljs->ctx, buf1);
    return -3;
}