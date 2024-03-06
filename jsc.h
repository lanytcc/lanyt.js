
#ifndef JSC_H
#define JSC_H

#include <cutils.h>
#include <quickjs-libc.h>
#include <quickjs.h>

JSRuntime *lanyt_jsc_new_rt();
void lanyt_jsc_free_rt(JSRuntime *p);

typedef struct lanyt_js lanyt_js;

lanyt_js *lanyt_new_js(JSRuntime *rt);
JSContext *lanyt_js_get_ctx(lanyt_js *ljs);
lanyt_js *lanyt_js_get_next(lanyt_js *ljs);
char *lanyt_js_get_filename(lanyt_js *ljs);
void lanyt_free_js(lanyt_js *ljs);

int lanyt_js_eval(lanyt_js *ljs, const char *filename);
int lanyt_js_run(lanyt_js *ljs, int silent);

int lanyt_js_save(lanyt_js *ljs, const char *filename, int debug);
int lanyt_js_read(lanyt_js *ljs, const char *filename, int *debug);

#endif // !JSC_H