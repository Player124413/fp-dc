/* What the GLES UI backend actually draws, and in which order.
 *
 * ui_gles.c keeps two batches -- flat colour quads (text, the 5x7 font, gradients) and signed-distance
 * shapes (every rounded panel and button fill) -- and they are two different programs.  Drawing the
 * shape batch after the flat one hid every button label under its own background on a phone, while the
 * software preview looked perfect (it draws in call order).  This test links the real ui_gles.c against
 * a fake GL that records each draw call with the colour of its first vertex, so the order and the colour
 * packing (0xAARRGGBB, not RGBA) are pinned where they can be checked without a device.
 */
#include <stdio.h>
#include <string.h>
#include <GLES3/gl3.h>

/* ------------------------------------------------------------------ the fake GL */
#define DRW_MAX 64
typedef struct { int prog; int quads; unsigned char c0[4]; } drw_t;
static drw_t drw[DRW_MAX];
static int ndrw;
static GLuint prog_id;                    /* programs are 1 (flat), 2 (sdf), like ui_gles_init makes them */
static GLuint shader_id = 100;
static GLuint cur_prog;
static const unsigned char *cur_verts;
static int ok_flag;

int glCreateShader(GLenum type) { (void)type; return (int)(shader_id++); }
int glShaderSource(GLuint s, GLsizei n, const GLchar *const *str, const GLint *len) { (void)s; (void)n; (void)str; (void)len; return 0; }
int glCompileShader(GLuint s) { (void)s; return 0; }
int glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void)s; (void)p; if (v) *v = 1; return 0; }
int glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei *l, GLchar *b) { (void)s; (void)n; (void)l; (void)b; return 0; }
int glCreateProgram(void) { return (int)(++prog_id); }
int glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; return 0; }
int glLinkProgram(GLuint p) { (void)p; return 0; }
int glGetProgramiv(GLuint p, GLenum n, GLint *v) { (void)p; (void)n; if (v) *v = 1; return 0; }
int glGetProgramInfoLog(GLuint p, GLsizei n, GLsizei *l, GLchar *b) { (void)p; (void)n; (void)l; (void)b; return 0; }
int glDeleteShader(GLuint s) { (void)s; return 0; }
int glDeleteProgram(GLuint p) { (void)p; return 0; }
int glGetUniformLocation(GLuint p, const GLchar *name) { (void)p; (void)name; return 0; }
/* the stub header declares these with an empty parameter list, so the definitions below take the
 * *promoted* types (float -> double, unsigned char -> int); a prototype definition would clash */
int glUniform2f(l, a, b) int l; double a; double b; { (void)l; (void)a; (void)b; return 0; }
int glUseProgram(GLuint p) { cur_prog = p; return 0; }
int glGenVertexArrays(GLsizei n, GLuint *v) { if (v) *v = 900; (void)n; return 0; }
int glDeleteVertexArrays(GLsizei n, GLuint *v) { (void)n; (void)v; return 0; }
int glBindVertexArray(GLuint v) { (void)v; return 0; }
static GLuint next_buf = 500;
int glGenBuffers(GLsizei n, GLuint *v) { (void)n; if (v) *v = next_buf++; return 0; }
int glDeleteBuffers(GLsizei n, GLuint *v) { (void)n; (void)v; return 0; }
int glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; return 0; }
int glBufferData(GLenum t, GLsizeiptr s, const void *d, GLenum u) { (void)t; (void)s; (void)d; (void)u; return 0; }
int glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const void *d) { (void)t; (void)o; (void)s; cur_verts = (const unsigned char *)d; return 0; }
int glEnableVertexAttribArray(GLuint i) { (void)i; return 0; }
int glDisableVertexAttribArray(GLuint i) { (void)i; return 0; }
int glVertexAttribPointer(i, n, t, norm, st, p)
    unsigned i; int n; unsigned t; int norm; int st; const void *p;
{ (void)i; (void)n; (void)t; (void)norm; (void)st; (void)p; return 0; }
int glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *idx) {
    (void)mode; (void)type; (void)idx;
    if (ndrw >= DRW_MAX) return 0;
    drw_t *d = &drw[ndrw++];
    d->prog = (int)cur_prog;
    d->quads = count / 6;
    memset(d->c0, 0, sizeof d->c0);
    /* flat vertices are {float x, y; u8 rgba[4]}, sdf ones are {5 floats..., u8 c0[4], c1[4]} */
    if (cur_verts) memcpy(d->c0, cur_verts + (cur_prog == 1 ? 8 : 36), 4);
    return 0;
}
int glGetError(void) { return ok_flag ? 0 : 0; }
int glBindFramebuffer(GLenum t, GLuint f) { (void)t; (void)f; return 0; }
int glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; return 0; }
int glEnable(GLenum c) { (void)c; return 0; }
int glDisable(GLenum c) { (void)c; return 0; }
int glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; return 0; }
int glClearColor(r, g, b, a) double r; double g; double b; double a; { (void)r; (void)g; (void)b; (void)a; return 0; }
int glClear(GLbitfield m) { (void)m; return 0; }

void plat_log(const char *fmt, ...) { (void)fmt; }

/* the backend itself, so the test sees its statics and its real batching */
#include "../../src/renderer/ui_gles.c"

/* ------------------------------------------------------------------ the checks */
static int fails;

static void check(int cond, const char *what) {
    if (cond) return;
    printf("  FAIL: %s\n", what);
    fails++;
}

static void expect_colour(const unsigned char c[4], int r, int g, int b, int a, const char *what) {
    if (c[0] == r && c[1] == g && c[2] == b && c[3] == a) return;
    printf("  FAIL: %s: got %02x%02x%02x%02x, want %02x%02x%02x%02x\n", what, c[0], c[1], c[2], c[3], r, g, b, a);
    fails++;
}

int main(void) {
    if (ui_gles_init() != 0) { printf("ui_gles_init failed\n"); return 1; }

    /* 1. a button and its label: the label has to be drawn *after* the fill, or it is invisible */
    ndrw = 0;
    ui_begin(400, 200, 1.0f);
    ui_rrect(10, 10, 120, 44, 12, 0xff4fd8ff);          /* the blue PLAY button */
    ui_text(24, 24, 3, 0xff06101c, "PLAY");             /* its dark label */
    ui_end();
    check(ndrw == 2, "a fill and a label are two draw calls");
    if (ndrw == 2) {
        check(drw[0].prog == 2, "the fill (SDF program) goes first");
        check(drw[1].prog == 1, "the label (flat program) goes after the fill");
        check(drw[0].quads == 1, "one rounded box is one SDF quad");
        check(drw[1].quads > 0, "the label drew something");
        expect_colour(drw[0].c0, 0x4f, 0xd8, 0xff, 0xff, "UI_ACCENT is cyan as 0xAARRGGBB");
        expect_colour(drw[1].c0, 0x06, 0x10, 0x1c, 0xff, "the label ink is dark, not an alpha");
    }

    /* 2. the other way round: text first, then a panel over it (a modal dim, a tooltip) */
    ndrw = 0;
    ui_begin(400, 200, 1.0f);
    ui_text(24, 24, 3, 0xffe8eefc, "BEHIND");
    ui_rrect(0, 0, 400, 200, 20, 0x77060a14);
    ui_end();
    check(ndrw == 2, "text then a panel is two draw calls");
    if (ndrw == 2) {
        check(drw[0].prog == 1, "the text (flat) goes first here");
        check(drw[1].prog == 2, "the panel (SDF) covers it after");
    }

    /* 3. same-kind geometry still batches: two boxes, one draw call */
    ndrw = 0;
    ui_begin(400, 200, 1.0f);
    ui_rrect(10, 10, 100, 40, 8, 0xff1b2740);
    ui_rrect(10, 60, 100, 40, 8, 0xff1b2740);
    ui_end();
    check(ndrw == 1 && drw[0].quads == 2, "two boxes stay one batch");

    /* 4. the background gradient and a plain rect are flat, and their colours are ARGB */
    ndrw = 0;
    ui_begin(400, 200, 1.0f);
    ui_rect(0, 0, 400, 200, 0xff0a0e1a);                /* the launcher's dark blue */
    ui_vgrad(0, 0, 400, 200, 0xff0a0e1a, 0xff141a2e);
    ui_end();
    check(ndrw == 1 && drw[0].quads == 2, "rects and gradients share the flat batch");
    expect_colour(drw[0].c0, 0x0a, 0x0e, 0x1a, 0xff, "UI_BG_TOP is dark blue, not 10% red");

    /* 5. ui_clear draws nothing and leaves both batches empty */
    ndrw = 0;
    ui_begin(400, 200, 1.0f);
    ui_rect(0, 0, 10, 10, 0xffffffff);
    ui_clear(UI_BG_TOP);
    ui_end();
    check(ndrw == 1, "clear flushes what was pending, once");
    check(nfq == 0 && nsq == 0, "nothing is left in the batches");

    ui_release();
    printf("ui draw order %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
