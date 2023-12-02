#include "jsc.h"
#include "module.h"
#include "quickjs-libc.h"
#include "quickjs.h"
#include <minwindef.h>
#include <stdio.h>

#define PJS_VERSION "0.0.1"

enum {
    COMMAND_RUN,
    COMMAND_COMPILE,
    COMMAND_HELP,
    OPTION_VERSION,
    COMMAND_COUNT,
};

static const char *command_str[] = {
    "run", "compile", "help", "--version", "r", "c", "h", "-v",
};
typedef int (*command_func)(int argc, char **argv);

enum {
    OPTION_RUN_BYTECODE,
    OPTION_RUN_ARGS,
    OPTION_RUN_ENV,
    OPTION_RUN_SILENT,
    OPTION_RUN_COUNT,
};

static const char *option_str[] = {
    "--bytecode", "--args", "--env", "--silent", "-b", "-a", "-e", "-s",
};

enum {
    OPTION_COMPILE_DEBUG,
    OPTION_O,
    OPTION_COMPILE_COUNT,
};

static const char *option_compile_str[] = {
    "--debug",
    "-o",
    "-d",
    "-o",
};

static int set_env(JSContext *ctx, char *env, JSValue *obj) {
    char *p = strchr(env, '=');
    if (!p) {
        fprintf(stderr, "env option need a name=value\n");
        return 1;
    } else {
        *p = '\0';
        if (JS_SetPropertyStr(ctx, *obj, env, JS_NewString(ctx, p + 1)) == -1) {
            js_std_dump_error(ctx);
            return 1;
        }
    }
    return 0;
}

static int run(int argc, char **argv) {
    int sargc = 0, silent = 0, pos = 0, bc = 0;
    char **sargv = NULL;
    JSValue env;
    JSContext *ctx;
    JSRuntime *rt = panda_jsc_new_rt();
    if (!rt) {
        fprintf(stderr, "create runtime failed\n");
        return 1;
    }
    panda_js *pjs = panda_new_js(rt);
    if (!pjs) {
        fprintf(stderr, "create js context failed\n");
        return 1;
    }
    ctx = panda_js_get_ctx(pjs);
    env = JS_NewObject(ctx);
    if (JS_IsException(env)) {
        js_std_dump_error(ctx);
        return 1;
    }

    for (size_t i = 2; i < argc; i++) {
        if (!strcmp(argv[i], option_str[OPTION_RUN_BYTECODE]) ||
            !strcmp(argv[i],
                    option_str[OPTION_RUN_BYTECODE + OPTION_RUN_COUNT])) {
            bc = 1;
        } else if (!strcmp(argv[i], option_str[OPTION_RUN_ARGS]) ||
                   !strcmp(argv[i],
                           option_str[OPTION_RUN_ARGS + OPTION_RUN_COUNT])) {
            sargc = argc - i - 1;
            sargv = &argv[i + 1];
            break;
        } else if (!strcmp(argv[i], option_str[OPTION_RUN_ENV]) ||
                   !strcmp(argv[i],
                           option_str[OPTION_RUN_ENV + OPTION_RUN_COUNT])) {
            if (i + 1 >= argc) {
                fprintf(stderr, "env option need a name=value\n");
                return 1;
            }
            if (set_env(ctx, argv[i + 1], &env))
                return 1;
            printf("%s\n", argv[i + 1]);
            ++i;
        } else if (!strcmp(argv[i], option_str[OPTION_RUN_SILENT]) ||
                   !strcmp(argv[i],
                           option_str[OPTION_RUN_SILENT + OPTION_RUN_COUNT])) {
            silent = 1;
        } else if (pos == 0) {
            pos = i;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    if (JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "env", env) == -1) {
        js_std_dump_error(ctx);
        return 1;
    }
    js_std_add_helpers(ctx, sargc, sargv);
    if (bc) {
        if (panda_js_read(pjs, argv[pos], 0))
            return 1;
    } else {
        if (panda_js_eval(pjs, argv[pos]))
            return 1;
    }
    if (panda_js_run(pjs, silent))
        return 1;
    panda_free_js(pjs);
    panda_jsc_free_rt(rt);
    return 0;
}

static int compile(int argc, char **argv) {
    int debug = 0, pos = 0, o_pos = 0;
    JSRuntime *rt = panda_jsc_new_rt();
    if (!rt) {
        fprintf(stderr, "create runtime failed\n");
        return 1;
    }
    panda_js *pjs = panda_new_js(rt);
    if (!pjs) {
        fprintf(stderr, "create js context failed\n");
        return 1;
    }

    for (size_t i = 2; i < argc; i++) {
        if (!strcmp(argv[i], option_compile_str[OPTION_O]) ||
            !strcmp(argv[i],
                    option_compile_str[OPTION_O + OPTION_COMPILE_COUNT])) {
            if (i + 1 >= argc) {
                fprintf(stderr, "o option need a file name\n");
                return 1;
            }
            o_pos = i + 1;
            ++i;
        } else if (!strcmp(argv[i], option_compile_str[OPTION_COMPILE_DEBUG]) ||
                   !strcmp(argv[i], option_compile_str[OPTION_COMPILE_DEBUG +
                                                       OPTION_COMPILE_COUNT])) {
            debug = 1;
        } else if (pos == 0) {
            pos = i;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    if (panda_js_eval(pjs, argv[pos]))
        return 1;
    if (panda_js_save(pjs, argv[o_pos], debug))
        return 1;
    panda_free_js(pjs);
    panda_jsc_free_rt(rt);
    return 0;
}

static int help(int argc, char **argv) {
    if (argc == 2) {
        printf("Usage: pjs [command] [options]\n");
        printf("Options:\n");
        printf("  --version, -v:    print version\n");
        printf("Commands:\n");
        printf("  run, r:           run js file\n");
        printf("  compile, c:       compile js file to binary\n");
        printf("  help, h:          print help\n");
        printf("More help use: pjs help [command]\n");
        return 0;
    } else if (argc == 3) {
        for (size_t i = 0; i < COMMAND_COUNT; i++) {
            if (!strcmp(argv[2], command_str[i]) ||
                !strcmp(argv[2], command_str[i + COMMAND_COUNT])) {
                printf("Usage: pjs %s [options]\n", command_str[i]);
                printf("Options:\n");
                switch (i) {
                case COMMAND_RUN:
                    printf("  --bytecode, -b:    run bytecode\n");
                    printf(
                        "  --args, -a:        --args [args]... set args for js "
                        "file\n");
                    printf(
                        "  --env, -e:         --env [name=value] set env for "
                        "js file\n");
                    printf("  --silent, -s:      silent mode\n");
                    break;
                case COMMAND_COMPILE:
                    printf("  --debug, -d:       debug mode\n");
                    break;
                default:
                    break;
                }
                return 0;
            }
        }
    } else {
        fprintf(stderr, "unknown command: %s\n", argv[2]);
    }
    return 0;
}

static int version(int argc, char **argv) {
    printf("pjs version: %s\n", PJS_VERSION);
    return 0;
}

static const command_func command_func_list[] = {
    run,
    compile,
    help,
    version,
};

int main(int argc, char **argv) {
    int ret = 0;
    panda_js_module_init();
    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        if (!strcmp(argv[1], command_str[i]) ||
            !strcmp(argv[1], command_str[i + COMMAND_COUNT])) {
            ret = command_func_list[i](argc, argv);
            break;
        }
    }
    panda_js_module_free();
    return ret;
}