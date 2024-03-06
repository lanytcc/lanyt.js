#include "jsc.h"
#include "module.h"
#include <stdio.h>

#define ljs_VERSION "0.0.1"

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
    OPTION_RUN_SILENT,
    OPTION_RUN_COUNT,
};

static const char *option_str[] = {
    "--bytecode", "--args", "--silent", "-b", "-a", "-s",
};

enum {
    OPTION_O,
    OPTION_COMPILE_COUNT,
};

static const char *option_compile_str[] = {
    "--output",
    "-o",
};

static int run(int argc, char **argv) {
    int sargc = 0, silent = 0, pos = 0, bc = 0;
    char **sargv = NULL;
    JSContext *ctx;
    JSRuntime *rt = lanyt_jsc_new_rt();
    if (!rt) {
        fprintf(stderr, "create runtime failed\n");
        return 1;
    }
    lanyt_js *ljs = lanyt_new_js(rt);
    if (!ljs) {
        fprintf(stderr, "create js context failed\n");
        return 1;
    }
    ctx = lanyt_js_get_ctx(ljs);

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
    js_std_add_helpers(ctx, sargc, sargv);
    if (bc) {
        if (lanyt_js_read(ljs, argv[pos], NULL))
            return 1;
    } else {
        if (lanyt_js_eval(ljs, argv[pos]))
            return 1;
    }
    if (lanyt_js_run(ljs, silent))
        return 1;
    lanyt_free_js(ljs);
    lanyt_jsc_free_rt(rt);
    return 0;
}

static int compile(int argc, char **argv) {
    int debug = 0, pos = 0, o_pos = 0;
    JSRuntime *rt = lanyt_jsc_new_rt();
    if (!rt) {
        fprintf(stderr, "create runtime failed\n");
        return 1;
    }
    lanyt_js *ljs = lanyt_new_js(rt);
    if (!ljs) {
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
        } else if (pos == 0) {
            pos = i;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    if (lanyt_js_eval(ljs, argv[pos]))
        return 1;
    if (o_pos && lanyt_js_save(ljs, argv[o_pos], debug))
        return 1;
    if (!o_pos && lanyt_js_save(ljs, "a.pbc", debug))
        return 1;
    lanyt_free_js(ljs);
    lanyt_jsc_free_rt(rt);
    return 0;
}

static int help(int argc, char **argv) {
    if (argc == 2) {
        printf("Usage: ljs <command> [options]\n");
        printf("Options:\n");
        printf("  --version, -v:    print version\n");
        printf("Commands:\n");
        printf("  run, r:           run <file>, run js file\n");
        printf(
            "  compile, c:       compile <file>, compile js file to binary\n");
        printf("  help, h:          help [command], print help\n");
        printf("More help use: ljs help [command]\n");
        return 0;
    } else if (argc == 3) {
        for (size_t i = 0; i < COMMAND_COUNT; i++) {
            if (!strcmp(argv[2], command_str[i]) ||
                !strcmp(argv[2], command_str[i + COMMAND_COUNT])) {
                printf("Usage: ljs %s [options]\n", command_str[i]);
                printf("Options:\n");
                switch (i) {
                case COMMAND_RUN:
                    printf("  --bytecode, -b:    run bytecode\n");
                    printf("  --args, -a:        --args <arg> [args] set "
                           "args for js "
                           "file\n");
                    printf("  --silent, -s:      silent mode\n");
                    break;
                case COMMAND_COMPILE:
                    printf("  --output, -o:      --output <file> set output "
                           "file\n");
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
    printf("ljs version: %s\n", ljs_VERSION);
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
    lanyt_js_module_init();
    size_t i;
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (!strcmp(argv[1], command_str[i]) ||
            !strcmp(argv[1], command_str[i + COMMAND_COUNT])) {
            ret = command_func_list[i](argc, argv);
            break;
        }
    }
    if (i == COMMAND_COUNT) {
        fprintf(stderr, "unknown command: %s\n", argv[1]);
        ret = 1;
        help(2, argv);
    }
    lanyt_js_module_free();
    return ret;
}