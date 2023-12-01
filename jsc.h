
#ifndef JSC_H
#define JSC_H

#include <cutils.h>
#include <quickjs-libc.h>
#include <quickjs.h>

JSRuntime *panda_jsc_new_rt();
void panda_jsc_free_rt(JSRuntime *p);

typedef struct panda_js panda_js;

panda_js *panda_new_js(JSRuntime *rt, int argc, char **argv);
void panda_free_js(panda_js *pjs);

int panda_js_eval(panda_js *pjs, const char *filename);
int panda_js_run(panda_js *pjs);

int panda_js_save(panda_js *pjs, const char *filename);
int panda_js_read(panda_js *pjs, const char *filename);

#endif // !JSC_H