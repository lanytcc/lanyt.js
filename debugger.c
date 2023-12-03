
#include "jsc.h"
#include "module.h"

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <mimalloc.h>
#include <pthread.h>

#define PJSD_VERSION "0.0.1"

enum {
    OPTION_PORT,
    OPTION_HELP,
    OPTION_VERSION,
    OPTION_COUNT,
};

static const char *option_str[] = {
    "--port", "--help", "--version", "-p", "-h", "-v",
};

enum {
    COMMAND_LIST,
    COMMAND_BREAKPOINT,
    COMMAND_BREAKPOINT_CLEAR,
    COMMAND_BREAKPOINT_LIST,
    COMMAND_CONTINUE,
    COMMAND_STEP,
    COMMAND_LOCAL_VARS,
    COMMAND_CLOSE_VARS,
    COMMAND_BACKTRACE,
    COMMAND_SETARG,
    COMMAND_HELP,
    COMMAND_COUNT,
};

static const char *command_str[] = {
    "list",
    "breakpoint",
    "breakpoint-clear",
    "breakpoint-list",
    "continue",
    "step",
    "local-vars",
    "close-vars",
    "backtrace",
    "arg",
    "help",
    "l",
    "b",
    "bc",
    "bl",
    "c",
    "s",
    "lv",
    "cv",
    "bt",
    "a",
    "h",
};

static void help(FILE *fp) {
    fprintf(fp, "Usage: pjsd <file> [Option]\n");
    fprintf(fp, "Option:\n");
    fprintf(fp, "  --port, -p:            --port <int>, set remote port\n");
    fprintf(fp, "  --help, -h:            print help\n");
    fprintf(fp, "  --version, -v:         print version\n");
    fprintf(fp, "Debugger Commands:\n");
    fprintf(fp, "  list, l:               l <line>:[line], list source code\n");
    fprintf(fp, "  breakpoint, b:         b [file]:<line>, set or unset "
                "breakpoint\n");
    fprintf(fp, "  breakpoint-clear, bc:  bc [file], clear breakpoint\n");
    fprintf(fp, "  breakpoint-list, bl:   bl [file], list breakpoint\n");
    fprintf(fp,
            "  continue, c:           continue or run to next breakpoint\n");
    fprintf(fp, "  step, s:               step execution\n");
    fprintf(fp, "  local-vars, lv:        lv [index], print the local "
                "variable at the specified stack index\n");
    fprintf(fp, "  close-vars, cv:        cv [index], print the closure "
                "variable at the specified stack index\n");
    fprintf(fp, "  backtrace, bt:         print backtrace\n");
    fprintf(fp, "  arg, a:                a <arg>, set script argument\n");
    fprintf(fp, "  help, h:               print help\n");
    fprintf(fp, "  exit, quit, q:         exit debugger\n");
}

typedef struct {
    char *filename;
    size_t *bps;
    size_t bp_cap;
    size_t bp_len;
} bp_list_t;

static int bp_list_add(bp_list_t *bp_list, size_t bp) {
    if (!bp_list) {
        return -1;
    }
    if (bp_list->bp_len >= bp_list->bp_cap) {
        size_t new_cap =
            bp_list->bp_cap ? bp_list->bp_cap + bp_list->bp_cap / 2 + 4 : 4;
        size_t *new_bps = mi_realloc(bp_list->bps, new_cap * sizeof(size_t));
        if (!new_bps) {
            return -1;
        }
        bp_list->bps = new_bps;
        bp_list->bp_cap = new_cap;
    }
    bp_list->bps[bp_list->bp_len++] = bp;
    return 0;
}

static void bp_list_free(bp_list_t *bp_list) {
    if (!bp_list) {
        return;
    }
    if (bp_list->filename) {
        mi_free(bp_list->filename);
    }
    if (bp_list->bps) {
        mi_free(bp_list->bps);
    }
}

typedef struct {
    bp_list_t *bp_list;
    size_t bp_list_len;
    JSValue args;
    int args_c;
    JSRuntime *rt;
    JSContext *ctx;
    panda_js *pjs;
    const char *s_filename;
} db_ctx_t;

static int ctx_bpl_add(db_ctx_t *db_ctx, const char *filename, size_t bp) {
    if (!db_ctx) {
        return -1;
    }
    for (int i = 0; i < db_ctx->bp_list_len; ++i) {
        if (!strcmp(db_ctx->bp_list[i].filename, filename)) {
            return bp_list_add(&db_ctx->bp_list[i], bp);
        }
    }
    bp_list_t new_bp_list = {0};
    new_bp_list.filename = mi_strdup(filename);
    if (!new_bp_list.filename)
        return -1;
    if (bp_list_add(&new_bp_list, bp)) {
        bp_list_free(&new_bp_list);
        return -1;
    }
    db_ctx->bp_list = mi_realloc(db_ctx->bp_list,
                                 (db_ctx->bp_list_len + 1) * sizeof(bp_list_t));
    if (!db_ctx->bp_list) {
        bp_list_free(&new_bp_list);
        return -1;
    }
    db_ctx->bp_list[db_ctx->bp_list_len++] = new_bp_list;
    return 0;
}

static int ctx_bpl_clear(db_ctx_t *db_ctx, const char *filename) {
    if (!db_ctx) {
        return -1;
    }
    for (int i = 0; i < db_ctx->bp_list_len; ++i) {
        if (!strcmp(db_ctx->bp_list[i].filename, filename) || !filename) {
            bp_list_free(&db_ctx->bp_list[i]);
            for (int j = i + 1; j < db_ctx->bp_list_len; ++j) {
                db_ctx->bp_list[j - 1] = db_ctx->bp_list[j];
            }
            --db_ctx->bp_list_len;
        }
    }
    return 0;
}

static pthread_mutex_t db_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;
static db_ctx_t *db_ctx = NULL;

static int parse_command(char *line_buf) {
    if (!strcmp(line_buf, command_str[COMMAND_LIST]) ||
        !strcmp(line_buf, command_str[COMMAND_LIST + COMMAND_COUNT])) {
        return COMMAND_LIST;
    } else if (!strcmp(line_buf, command_str[COMMAND_BREAKPOINT]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_BREAKPOINT + COMMAND_COUNT])) {
        return COMMAND_BREAKPOINT;
    } else if (!strcmp(line_buf, command_str[COMMAND_BREAKPOINT_CLEAR]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_BREAKPOINT_CLEAR + COMMAND_COUNT])) {
        return COMMAND_BREAKPOINT_CLEAR;
    } else if (!strcmp(line_buf, command_str[COMMAND_BREAKPOINT_LIST]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_BREAKPOINT_LIST + COMMAND_COUNT])) {
        return COMMAND_BREAKPOINT_LIST;
    } else if (!strcmp(line_buf, command_str[COMMAND_CONTINUE]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_CONTINUE + COMMAND_COUNT])) {
        return COMMAND_CONTINUE;
    } else if (!strcmp(line_buf, command_str[COMMAND_STEP]) ||
               !strcmp(line_buf, command_str[COMMAND_STEP + COMMAND_COUNT])) {
        return COMMAND_STEP;
    } else if (!strcmp(line_buf, command_str[COMMAND_LOCAL_VARS]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_LOCAL_VARS + COMMAND_COUNT])) {
        return COMMAND_LOCAL_VARS;
    } else if (!strcmp(line_buf, command_str[COMMAND_CLOSE_VARS]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_CLOSE_VARS + COMMAND_COUNT])) {
        return COMMAND_CLOSE_VARS;
    } else if (!strcmp(line_buf, command_str[COMMAND_BACKTRACE]) ||
               !strcmp(line_buf,
                       command_str[COMMAND_BACKTRACE + COMMAND_COUNT])) {
        return COMMAND_BACKTRACE;
    } else if (!strcmp(line_buf, command_str[COMMAND_SETARG]) ||
               !strcmp(line_buf, command_str[COMMAND_SETARG + COMMAND_COUNT])) {
        return COMMAND_SETARG;
    } else if (!strcmp(line_buf, command_str[COMMAND_HELP]) ||
               !strcmp(line_buf, command_str[COMMAND_HELP + COMMAND_COUNT])) {
        return COMMAND_HELP;
    } else {
        return COMMAND_COUNT;
    }
}

static int loop(JSContext *ctx, int start, const char *filename, size_t line,
                const uint8_t *cur_pc) {
    char line_buf[256];

    while (1) {
        printf("pjsd> ");
        if (!fgets(line_buf, sizeof(line_buf), stdin)) {
            continue;
        }
        line_buf[strlen(line_buf) - 1] = '\0';
        char *p = strchr(line_buf, ' ');
        if (p) {
            *p = '\0';
        }
        if (!strcmp(line_buf, "exit") || !strcmp(line_buf, "quit") ||
            !strcmp(line_buf, "q")) {
            break;
        }
        int command = parse_command(line_buf);
        if (p)
            *p = ' ';
        switch (command) {
        case COMMAND_LIST:
            // TODO: list source code
            break;
        case COMMAND_BREAKPOINT:
            const char *_filename = strchr(line_buf, ' ');
            if (!_filename || *(_filename + 1) == '\0' ||
                *(_filename + 1) == ':')
                _filename = filename;
            else
                ++_filename;
            char *line_str = strchr(line_buf, ':');
            if (!line_str) {
                fprintf(stderr, "invalid breakpoint: \n  ->%s\n", line_buf);
                break;
            }
            *line_str = '\0';
            size_t _line = atoll(line_str + 1);
            if (!_line) {
                fprintf(stderr, "invalid breakpoint: \n  ->%s\n", line_buf);
                break;
            }
            printf("set breakpoint at %s:%lld\n", _filename, _line);
            ctx_bpl_add(db_ctx, _filename, _line);
            break;
        case COMMAND_BREAKPOINT_CLEAR:
            _filename = strchr(line_buf, ' ');
            if (!_filename || *(_filename + 1) == '\0')
                _filename = filename;
            else
                ++_filename;
            printf("clear breakpoint at %s\n", _filename);
            if (ctx_bpl_clear(db_ctx, _filename)) {
                fprintf(stderr, "clear breakpoint failed\n");
            }
            break;
        case COMMAND_BREAKPOINT_LIST:
            printf("breakpoint list:\n");
            for (size_t i = 0; i < db_ctx->bp_list_len; ++i) {
                printf("  -> %s: ", db_ctx->bp_list[i].filename);
                for (size_t j = 0; j < db_ctx->bp_list[i].bp_len; ++j) {
                    printf("%lld, ", db_ctx->bp_list[i].bps[j]);
                }
                printf("\n");
            }
            break;
        case COMMAND_CONTINUE:
            if (start)
                return panda_js_run(db_ctx->pjs, 0);
            return 0;
        case COMMAND_STEP:
            return ctx_bpl_add(db_ctx, filename, line + 1);
        case COMMAND_LOCAL_VARS:
            char *index_str = strchr(line_buf, ' ');
            if (!index_str) {
                fprintf(stderr, "invalid local vars: \n  ->%s\n", line_buf);
                break;
            }
            size_t index = atoll(index_str + 1);
            if (index < 0) {
                fprintf(stderr, "invalid local vars: \n  ->%s\n", line_buf);
                break;
            }
            js_debugger_local_variables(ctx, index);
            break;
        case COMMAND_CLOSE_VARS:
            index_str = strchr(line_buf, ' ');
            if (!index_str) {
                fprintf(stderr, "invalid close vars: \n  ->%s\n", line_buf);
                break;
            }
            index = atoll(index_str + 1);
            if (index < 0) {
                fprintf(stderr, "invalid close vars: \n  ->%s\n", line_buf);
                break;
            }
            js_debugger_closure_variables(ctx, index);
            break;
        case COMMAND_BACKTRACE:
            if (start) {
                fprintf(stderr, "can't backtrace before run\n");
            }
            js_debugger_build_backtrace(ctx, cur_pc);
            break;
        case COMMAND_SETARG:
            char *arg_str = strchr(line_buf, ' ');
            if (!arg_str) {
                fprintf(stderr, "invalid args: \n  ->%s\n", line_buf);
                break;
            }
            if (JS_SetPropertyUint32(ctx, db_ctx->args, db_ctx->args_c++,
                                     JS_NewString(ctx, arg_str)) == -1) {
                js_std_dump_error(ctx);
            }
            break;
        case COMMAND_HELP:
            help(stdout);
            break;
        default:
            fprintf(stderr, "unknown command: \n  ->%s\n", line_buf);
            break;
        }
    }

    return 0;
}

static void show_js_line(const char *filename, size_t line) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "open file failed: %s\n", filename);
        return;
    }
    char buf[768];
    size_t _line = 1;
    while (fgets(buf, sizeof(buf), fp)) {
        if (_line == line) {
            printf("  -> %s", buf);
            break;
        }
        ++_line;
    }
    fclose(fp);
}

static void show_str_line(panda_js *pjs, const char *filename, size_t line) {
    while (pjs) {
        char *p = panda_js_get_filename(pjs);
        if (has_suffix(p, filename)) {
            show_js_line(p, line);
            break;
        }
        if (p[0] != '<') {
            printf("  -> %s\n", p);
            break;
        }
        char *p1 = strchr(p, '>');
        if (!p1) {
            fprintf(stderr, "invalid file: %s\n check your pjsd version\n",
                    filename);
            break;
        }
        *p1 = '\0';
        if (has_suffix(p + 1, filename)) {
            *p1 = '>';
            char *line_str = strtok(p1 + 1, "\n");
            size_t _line = 1;
            if (!line_str) {
                fprintf(stderr, "invalid line: %s\n check your pjsd version\n",
                        filename);
                break;
            }
            while (line_str) {
                if (_line == line) {
                    printf("  -> %s\n", line_str);
                    break;
                }
                line_str = strtok(NULL, "\n");
                ++_line;
            }
            break;
        }
        pjs = panda_js_get_next(pjs);
    }
}

static JS_BOOL debugger_callback(JSContext *ctx, JSAtom file_name,
                                 uint32_t line_no, const uint8_t *pc) {
    pthread_mutex_lock(&db_ctx_mutex);
    if (!db_ctx) {
        pthread_mutex_unlock(&db_ctx_mutex);
        return 0;
    }
    if (db_ctx->bp_list_len > 0 && !db_ctx->s_filename) {
        db_ctx->s_filename = JS_AtomToCString(ctx, file_name);
        if (!db_ctx->s_filename) {
        mem_fail:
            JS_ThrowOutOfMemory(ctx);
            pthread_mutex_unlock(&db_ctx_mutex);
            return 0;
        }
        for (int i = 0; i < db_ctx->bp_list_len; ++i) {
            if (!strcmp(db_ctx->bp_list[i].filename, "<panda>")) {
                mi_free(db_ctx->bp_list[i].filename);
                db_ctx->bp_list[i].filename = mi_strdup(db_ctx->s_filename);
                if (!db_ctx->bp_list[i].filename)
                    goto mem_fail;
                break;
            }
        }
    }
    const char *filename = JS_AtomToCString(ctx, file_name);
    if (!filename)
        goto mem_fail;
    for (int i = 0; i < db_ctx->bp_list_len; ++i) {
        if (!strcmp(db_ctx->bp_list[i].filename, filename)) {
            for (int j = 0; j < db_ctx->bp_list[i].bp_len; ++j) {
                if (db_ctx->bp_list[i].bps[j] == line_no) {
                    show_str_line(db_ctx->pjs, filename, line_no);
                    loop(ctx, 0, filename, line_no, pc);
                    break;
                }
            }
        }
    }
    JS_FreeCString(ctx, filename);
    pthread_mutex_unlock(&db_ctx_mutex);
    return 1;
}

int main(int argc, char **argv) {
    int ret = 0, pos = 0;
    int bc = 0, port = 0;
    if (argc < 2) {
        fprintf(stderr, "No input file\n");
        help(stderr);
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], option_str[OPTION_HELP]) ||
            !strcmp(argv[i], option_str[OPTION_HELP + OPTION_COUNT])) {
            help(stdout);
            return 0;
        } else if (!strcmp(argv[i], option_str[OPTION_VERSION]) ||
                   !strcmp(argv[i],
                           option_str[OPTION_VERSION + OPTION_COUNT])) {
            printf("pjsd version %s\n", PJSD_VERSION);
            return 0;
        } else if (!strcmp(argv[i], option_str[OPTION_PORT]) ||
                   !strcmp(argv[i], option_str[OPTION_PORT + OPTION_COUNT])) {
            if (i + 1 <= argc) {
                port = atoi(argv[i + 1]);
                ++i;
            } else {
                fprintf(stderr, "port option need a port number\n");
                return 1;
            }
        } else if (pos == 0) {
            pos = i;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            help(stderr);
            return 1;
        }
    }
    db_ctx = mi_malloc(sizeof(db_ctx_t));
    if (!db_ctx) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    memset(db_ctx, 0, sizeof(db_ctx_t));
    panda_js_module_init();
    db_ctx->rt = panda_jsc_new_rt();
    if (!db_ctx->rt) {
        fprintf(stderr, "create runtime failed\n");
        return 1;
    }
    db_ctx->pjs = panda_new_js(db_ctx->rt);
    if (!db_ctx->pjs) {
        fprintf(stderr, "create js context failed\n");
        return 1;
    }
    db_ctx->ctx = panda_js_get_ctx(db_ctx->pjs);
    JS_SetBreakpointHandler(db_ctx->ctx, debugger_callback);
    JS_SetDebuggerMode(db_ctx->ctx, 1);
    js_std_add_helpers(db_ctx->ctx, 0, NULL);
    db_ctx->args = JS_GetPropertyStr(
        db_ctx->ctx, JS_GetGlobalObject(db_ctx->ctx), "scriptArgs");
    if (JS_IsException(db_ctx->args)) {
        js_std_dump_error(db_ctx->ctx);
        return 1;
    }
    if (bc) {
        if (panda_js_read(db_ctx->pjs, argv[pos], 0))
            return 1;
    } else {
        if (panda_js_eval(db_ctx->pjs, argv[pos]))
            return 1;
    }
    ret = loop(db_ctx->ctx, 1, "<panda>", 1, NULL);
    JS_FreeValue(db_ctx->ctx, db_ctx->args);
    JS_FreeCString(db_ctx->ctx, db_ctx->s_filename);
    panda_free_js(db_ctx->pjs);
    panda_jsc_free_rt(db_ctx->rt);
    panda_js_module_free();
    for (int i = 0; i < db_ctx->bp_list_len; ++i) {
        bp_list_free(&db_ctx->bp_list[i]);
    }
    mi_free(db_ctx->bp_list);
    mi_free(db_ctx);
    return ret;
}