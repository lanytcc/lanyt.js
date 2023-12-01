#include "jsc.h"
#include "module.h"

int main(int argc, char **argv) {
    panda_js_module_init();
    JSRuntime *rt = panda_jsc_new_rt();
    panda_js *pjs = panda_new_js(rt, 0, NULL);
    panda_js_eval(pjs, argv[1]);
    panda_js_run(pjs);
    panda_free_js(pjs);
    panda_js_module_free();
}