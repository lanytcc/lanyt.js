
#ifndef JSC_H
#define JSC_H

#include <cutils.h>
#include <quickjs-libc.h>
#include <quickjs.h>

JSRuntime *lanyt_jsc_new_rt();
void lanyt_jsc_free_rt(JSRuntime *p);

typedef struct lanyt_js lanyt_js;

lanyt_js *lanyt_new_js(JSRuntime *rt);
JSContext *lanyt_js_get_ctx(lanyt_js *pjs);
lanyt_js *lanyt_js_get_next(lanyt_js *pjs);
char *lanyt_js_get_filename(lanyt_js *pjs);
void lanyt_free_js(lanyt_js *pjs);

int lanyt_js_eval(lanyt_js *pjs, const char *filename);
int lanyt_js_run(lanyt_js *pjs, int silent);

int lanyt_js_save(lanyt_js *pjs, const char *filename, int debug);
int lanyt_js_read(lanyt_js *pjs, const char *filename, int *debug);

#endif // !JSC_H