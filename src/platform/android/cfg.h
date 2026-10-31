/* key/value store of the Android shell: port settings + touch layout (see cfg.c). */
#ifndef FP_ANDROID_CFG_H
#define FP_ANDROID_CFG_H

int         cfg_has(const char *key);               /* present (even with an empty value) */
int         cfg_int(const char *key, int def);
float       cfg_float(const char *key, float def);
const char *cfg_str(const char *key, const char *def);
void        cfg_set_int(const char *key, int v);
void        cfg_set_float(const char *key, float v);
void        cfg_set_str(const char *key, const char *v);
void        cfg_clear(void);
int         cfg_parse(const char *text, int len);   /* merge a file: known keys replaced, unknown kept */
int         cfg_write(char *buf, int cap);          /* whole store as text, returns the length */
int         cfg_count(void);

#endif
