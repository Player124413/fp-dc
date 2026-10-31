/* Tiny key/value store of the Android shell (port settings + touch layout), persisted as a
 * human-editable text file ("<files>/port.txt"):
 *
 *     render.mode=0
 *     pad.stick=1
 *     ctl.jump=0.865 0.66 27 1 200
 *
 * One line per key, "key=value", '#' starts a comment.  Unknown keys read from a file are kept (a
 * newer build's settings survive a downgrade) and unknown writes are appended, so the format needs
 * no version number: every reader falls back to its default when its key is missing or unparsable.
 * The same store is used by the launcher, the control editor and the engine glue, and the host
 * preview/tests use this file's parser too -- one implementation, no JSON dependency in C. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"

#define CFG_MAX 96
#define CFG_KEY 24
#define CFG_VAL 96

static struct { char key[CFG_KEY]; char val[CFG_VAL]; } ent[CFG_MAX];
static int nent;

static int find(const char *key) {
    for (int i = 0; i < nent; i++)
        if (!strcmp(ent[i].key, key)) return i;
    return -1;
}

static void set(const char *key, const char *val) {
    if (!key || !*key || strlen(key) >= CFG_KEY) return;
    int i = find(key);
    if (i < 0) {
        if (nent >= CFG_MAX) return;
        i = nent++;
        snprintf(ent[i].key, sizeof ent[i].key, "%s", key);
    }
    snprintf(ent[i].val, sizeof ent[i].val, "%s", val ? val : "");
}

int cfg_has(const char *key) { return key && find(key) >= 0; }

int cfg_int(const char *key, int def) {
    int i = find(key);
    if (i < 0 || !ent[i].val[0]) return def;
    char *end = NULL;
    long v = strtol(ent[i].val, &end, 10);
    return end && *end == 0 ? (int)v : def;
}

float cfg_float(const char *key, float def) {
    int i = find(key);
    if (i < 0 || !ent[i].val[0]) return def;
    char *end = NULL;
    float v = strtof(ent[i].val, &end);
    return end && *end == 0 ? v : def;
}

const char *cfg_str(const char *key, const char *def) {
    int i = find(key);
    return i < 0 ? def : ent[i].val;
}

void cfg_set_int(const char *key, int v) {
    char b[24];
    snprintf(b, sizeof b, "%d", v);
    set(key, b);
}
void cfg_set_float(const char *key, float v) {
    char b[32];
    snprintf(b, sizeof b, "%.4f", v);
    set(key, b);
}
void cfg_set_str(const char *key, const char *v) { set(key, v); }

void cfg_clear(void) { nent = 0; }

int cfg_parse(const char *text, int len) {
    if (!text) return 0;
    int count = 0;
    const char *p = text, *end = text + (len > 0 ? len : (int)strlen(text));
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        const char *eq = memchr(p, '=', (size_t)(le - p));
        if (p < le && *p != '#' && eq) {
            char k[CFG_KEY], v[CFG_VAL];
            size_t kl = (size_t)(eq - p), vl = (size_t)(le - eq - 1);
            while (kl && (p[kl - 1] == ' ' || p[kl - 1] == '\r' || p[kl - 1] == '\t')) kl--;
            const char *vs = eq + 1;
            while (vl && (vs[0] == ' ' || vs[0] == '\t')) { vs++; vl--; }
            while (vl && (vs[vl - 1] == ' ' || vs[vl - 1] == '\r' || vs[vl - 1] == '\t')) vl--;
            if (kl && kl < sizeof k && vl < sizeof v) {
                memcpy(k, p, kl); k[kl] = 0;
                memcpy(v, vs, vl); v[vl] = 0;
                set(k, v);
                count++;
            }
        }
        p = nl ? nl + 1 : end;
    }
    return count;
}

int cfg_write(char *buf, int cap) {
    int n = 0;
    for (int i = 0; i < nent; i++) {
        int k = snprintf(buf + n, cap > n ? (size_t)(cap - n) : 0, "%s=%s\n", ent[i].key, ent[i].val);
        if (k < 0) break;
        n += k;
        if (n >= cap) return cap;          /* truncated: the caller sees cap */
    }
    return n;
}

int cfg_count(void) { return nent; }
