/* OpenGL ES 3 backend of the shell UI (ui.h): flat colour quads (rects, quads, gradients, the 5x7
 * font) in one batch, and signed-distance shapes (rounded boxes, circles, rings) in a second one.
 *
 * Two programs because the two kinds of geometry have very different volume: a launcher frame has a
 * few hundred text pixels (12-byte vertices) and a few dozen circles (44-byte vertices), and both
 * batches go out in one draw call each.  The distance functions are the same as
 * tools/preview/ui_soft.c, so the host preview shows what a phone shows. */
#include <GLES3/gl3.h>
#include <math.h>
#include <string.h>
#include "../fp.h"
#include "../platform/android/ui.h"
#include "../platform/android/ui_font.h"

#define UIQ 6000                     /* quads per batch */

/* ------------------------------------------------------------------ flat geometry */
typedef struct { float x, y; uint8_t c[4]; } fvtx_t;
static fvtx_t fv[UIQ * 4];
static int nfq;

/* ------------------------------------------------------------------ sdf geometry */
typedef struct {
    float x, y;        /* position, px */
    float lx, ly;      /* position relative to the shape centre */
    float hx, hy;      /* half size */
    float rad, th;     /* corner radius / half thickness */
    float shape;       /* 0 fill rrect, 1 outline rrect, 2 circle/ring, 3 ... */
    uint8_t c0[4], c1[4];
} svtx_t;
static svtx_t sv[512 * 4];
static int nsq;
#define SDF_MAX 512

static GLuint prog_flat, prog_sdf;
static GLuint fvbo, fvibo, svbo, svibo;
static GLuint vao;                       /* explicit: a driver must not rely on the default one */
static int st_flat, st_sdf, st_flush, st_err;
static int W, H;
static float DENS = 2.0f;

static const char *VS_FLAT =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec4 a_col;\n"
    "uniform vec2 u_target;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos.x / u_target.x * 2.0 - 1.0, 1.0 - a_pos.y / u_target.y * 2.0, 0.0, 1.0);\n"
    "  v_col = a_col;\n"
    "}\n";
static const char *FS_FLAT =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 v_col; out vec4 o;\n"
    "void main() { o = v_col; }\n";

static const char *VS_SDF =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_local;\n"
    "layout(location=2) in vec2 a_half;\n"
    "layout(location=3) in vec3 a_par;\n"          /* radius, half thickness, shape */
    "layout(location=4) in vec4 a_col;\n"
    "layout(location=5) in vec4 a_col2;\n"
    "uniform vec2 u_target;\n"
    "out vec2 v_local; out vec2 v_half; out vec3 v_par; out vec4 v_col; out vec4 v_col2;\n"
    "void main() {\n"
    "  gl_Position = vec4(a_pos.x / u_target.x * 2.0 - 1.0, 1.0 - a_pos.y / u_target.y * 2.0, 0.0, 1.0);\n"
    "  v_local = a_local; v_half = a_half; v_par = a_par; v_col = a_col; v_col2 = a_col2;\n"
    "}\n";
static const char *FS_SDF =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 v_local; in vec2 v_half; in vec3 v_par; in vec4 v_col; in vec4 v_col2;\n"
    "out vec4 o;\n"
    "float sd_rrect(vec2 p, vec2 h, float r) {\n"
    "  vec2 q = abs(p) - h + r;\n"
    "  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "}\n"
    "void main() {\n"
    "  float shape = v_par.z;\n"
    "  float d;\n"
    "  if (shape < 0.5) d = sd_rrect(v_local, v_half, v_par.x);\n"
    "  else if (shape < 1.5) d = abs(sd_rrect(v_local, v_half, v_par.x)) - v_par.y;\n"
    "  else d = abs(length(v_local) - v_half.x) - v_par.y;\n"
    "  float cov = clamp(0.5 - d, 0.0, 1.0);\n"
    "  vec4 c = v_col;\n"
    "  if (shape > 2.5) { cov = 1.0; c = mix(v_col, v_col2, clamp(v_local.y / (2.0 * max(v_half.y, 0.001)) + 0.5, 0.0, 1.0)); }\n"
    "  o = vec4(c.rgb, c.a * cov);\n"
    "}\n";

static GLuint mkprog(const char *vs, const char *fs, const char *name) {
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    GLint ok = 0;
    /* the shared compile/link helper lives in render_gles.c; duplicating 20 lines keeps the two
     * renderers independent (either can be built without the other) */
    glShaderSource(v, 1, &vs, NULL); glCompileShader(v); glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(v, sizeof l, NULL, l); plat_log("ui: %s vs: %s\n", name, l); return 0; }
    glShaderSource(f, 1, &fs, NULL); glCompileShader(f); glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(f, sizeof l, NULL, l); plat_log("ui: %s fs: %s\n", name, l); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[512]; glGetProgramInfoLog(p, sizeof l, NULL, l); plat_log("ui: %s link: %s\n", name, l); return 0; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

static void indices(GLuint *ibo, int quads) {
    static uint16_t idx[UIQ * 6];
    for (int q = 0; q < quads; q++) {
        uint16_t *p = &idx[q * 6];
        p[0] = (uint16_t)(q * 4); p[1] = (uint16_t)(q * 4 + 1); p[2] = (uint16_t)(q * 4 + 2);
        p[3] = (uint16_t)(q * 4); p[4] = (uint16_t)(q * 4 + 2); p[5] = (uint16_t)(q * 4 + 3);
    }
    glGenBuffers(1, ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)quads * 6 * 2, idx, GL_STATIC_DRAW);
}

int ui_gles_init(void) {
    prog_flat = mkprog(VS_FLAT, FS_FLAT, "flat");
    prog_sdf = mkprog(VS_SDF, FS_SDF, "sdf");
    if (!prog_flat || !prog_sdf) return -1;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &fvbo);
    glBindBuffer(GL_ARRAY_BUFFER, fvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof fv, NULL, GL_STREAM_DRAW);
    glGenBuffers(1, &svbo);
    glBindBuffer(GL_ARRAY_BUFFER, svbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof sv, NULL, GL_STREAM_DRAW);
    indices(&fvibo, UIQ);
    indices(&svibo, SDF_MAX);
    return 0;
}

/* what the last frame actually drew: a report from a phone needs these numbers */
void ui_stats(int *quads, int *sdf, int *flushes, int *glerr) {
    if (quads) *quads = st_flat;
    if (sdf) *sdf = st_sdf;
    if (flushes) *flushes = st_flush;
    if (glerr) *glerr = st_err;
}

void ui_release(void) {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (prog_flat) { glDeleteProgram(prog_flat); prog_flat = 0; }
    if (prog_sdf) { glDeleteProgram(prog_sdf); prog_sdf = 0; }
    if (fvbo) { glDeleteBuffers(1, &fvbo); fvbo = 0; }
    if (svbo) { glDeleteBuffers(1, &svbo); svbo = 0; }
    if (fvibo) { glDeleteBuffers(1, &fvibo); fvibo = 0; }
    if (svibo) { glDeleteBuffers(1, &svibo); svibo = 0; }
    nfq = nsq = 0;
}

/* ------------------------------------------------------------------ batches */
static void flush_flat(void) {
    if (!nfq) return;
    glBindVertexArray(vao);
    glUseProgram(prog_flat);
    glUniform2f(glGetUniformLocation(prog_flat, "u_target"), (float)W, (float)H);
    glBindBuffer(GL_ARRAY_BUFFER, fvbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nfq * 4 * (GLsizeiptr)sizeof(fvtx_t), fv);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fvibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(fvtx_t), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(fvtx_t), (void *)8);
    for (GLuint a = 2; a < 6; a++) glDisableVertexAttribArray(a);   /* the SDF arrays point elsewhere */
    glDrawElements(GL_TRIANGLES, nfq * 6, GL_UNSIGNED_SHORT, (void *)0);
    st_flat += nfq; st_flush++;
    st_err |= (int)glGetError();
    nfq = 0;
}

static void flush_sdf(void) {
    if (!nsq) return;
    glBindVertexArray(vao);
    glUseProgram(prog_sdf);
    glUniform2f(glGetUniformLocation(prog_sdf, "u_target"), (float)W, (float)H);
    glBindBuffer(GL_ARRAY_BUFFER, svbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nsq * 4 * (GLsizeiptr)sizeof(svtx_t), sv);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, svibo);
    const GLsizei st = sizeof(svtx_t);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void *)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void *)16);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, (void *)24);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *)36);
    glEnableVertexAttribArray(5); glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *)40);
    glDrawElements(GL_TRIANGLES, nsq * 6, GL_UNSIGNED_SHORT, (void *)0);
    st_sdf += nsq; st_flush++;
    st_err |= (int)glGetError();
    nsq = 0;
}

/* at ui_end only one of the two can be pending (each emitter flushes the other), so the order here
 * never matters; ui_clear uses it too, and there nothing is drawn after it. */
static void flush_all(void) { flush_flat(); flush_sdf(); }

static void flat_quad(float x0, float y0, float x1, float y1, uint32_t c0, uint32_t c1) {
    /* Draw order *is* the layering: the two batches are separate programs, so a batch that is still
     * pending has to go out before this one starts. Without this the SDF batch (every rounded panel
     * and button fill) was drawn after all the flat geometry (all the text), and the labels of every
     * button disappeared under its own background. */
    if (nsq) flush_sdf();
    if (nfq >= UIQ) flush_flat();
    fvtx_t *v = &fv[nfq * 4];
    uint32_t cols[4] = { c0, c0, c1, c1 };
    float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].c[0] = ui_ch_r(cols[i]);
        v[i].c[1] = ui_ch_g(cols[i]);
        v[i].c[2] = ui_ch_b(cols[i]);
        v[i].c[3] = ui_ch_a(cols[i]);
    }
    nfq++;
}

static void sdf_quad(float cx, float cy, float hw, float hh, float rad, float th, int shape, uint32_t c0, uint32_t c1) {
    if (nfq) flush_flat();                 /* the flat geometry drawn so far stays under this shape */
    if (nsq >= SDF_MAX) flush_sdf();
    float x0 = cx - hw - 1, y0 = cy - hh - 1, x1 = cx + hw + 1, y1 = cy + hh + 1;
    if (shape == 2) { x0 = cx - hw - th - 1; x1 = cx + hw + th + 1; y0 = cy - hw - th - 1; y1 = cy + hw + th + 1; }
    svtx_t *v = &sv[nsq * 4];
    float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].lx = xs[i] - cx;
        v[i].ly = ys[i] - cy;
        v[i].hx = hw;
        v[i].hy = hh;
        v[i].rad = rad;
        v[i].th = th;
        v[i].shape = (float)shape;
        v[i].c0[0] = ui_ch_r(c0); v[i].c0[1] = ui_ch_g(c0); v[i].c0[2] = ui_ch_b(c0); v[i].c0[3] = ui_ch_a(c0);
        v[i].c1[0] = ui_ch_r(c1); v[i].c1[1] = ui_ch_g(c1); v[i].c1[2] = ui_ch_b(c1); v[i].c1[3] = ui_ch_a(c1);
    }
    nsq++;
}

/* ------------------------------------------------------------------ ui.h */
void ui_begin(int w, int h, float dp_scale) {
    W = w > 0 ? w : 1;
    H = h > 0 ? h : 1;
    DENS = dp_scale > 0.1f ? dp_scale : 1.0f;
    nfq = nsq = 0;
    st_flat = st_sdf = st_flush = 0;
    st_err = (int)glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(vao);
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ui_end(void) { flush_all(); }

void ui_clear(uint32_t rgba) {
    flush_all();
    glClearColor((GLclampf)ui_ch_r(rgba) / 255.0f, (GLclampf)ui_ch_g(rgba) / 255.0f,
                 (GLclampf)ui_ch_b(rgba) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int ui_w(void) { return W; }
int ui_h(void) { return H; }
float ui_dp(float v) { return v * DENS; }
float ui_undp(float v) { return v / DENS; }
int ui_is_wide(void) { return (float)W >= (float)H * 1.2f; }

static inline float rf(float v) { return floorf(v + 0.5f); }

void ui_rect(float x, float y, float w, float h, uint32_t rgba) {
    if (w <= 0 || h <= 0 || (rgba >> 24) == 0) return;
    flat_quad(rf(x), rf(y), rf(x + w), rf(y + h), rgba, rgba);
}

void ui_rrect(float x, float y, float w, float h, float r, uint32_t rgba) {
    if (w <= 0 || h <= 0 || (rgba >> 24) == 0) return;
    if (r <= 0.01f) { ui_rect(x, y, w, h, rgba); return; }
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    sdf_quad(x + hw, y + hh, hw, hh, r, 0, 0, rgba, rgba);
}

void ui_rrect_line(float x, float y, float w, float h, float r, float th, uint32_t rgba) {
    if (w <= 0 || h <= 0 || th <= 0 || (rgba >> 24) == 0) return;
    float hw = w * 0.5f, hh = h * 0.5f;
    if (r > hw) r = hw;
    if (r > hh) r = hh;
    sdf_quad(x + hw, y + hh, hw, hh, r, th * 0.5f, 1, rgba, rgba);
}

void ui_circle(float cx, float cy, float r, uint32_t rgba) {
    if (r <= 0 || (rgba >> 24) == 0) return;
    sdf_quad(cx, cy, r, r, 0, 0, 2, rgba, rgba);
}

void ui_ring(float cx, float cy, float r, float th, uint32_t rgba) {
    if (r <= 0 || th <= 0 || (rgba >> 24) == 0) return;
    sdf_quad(cx, cy, r, r, 0, th * 0.5f, 2, rgba, rgba);
}

void ui_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;
    if ((top >> 24) == 0 && (bottom >> 24) == 0) return;
    flat_quad(x, y, x + w, y + h, top, bottom);
}

void ui_quad(const float xy[8], uint32_t rgba) {
    if ((rgba >> 24) == 0) return;
    /* two triangles through the flat batch (a quad is 4 vertices there; the second triangle reuses
     * two of them, so emit it as a degenerate second quad-free triangle pair) */
    if (nfq >= UIQ) flush_flat();
    fvtx_t *v = &fv[nfq * 4];
    const float *p = xy;
    for (int i = 0; i < 4; i++) {
        v[i].x = p[i * 2];
        v[i].y = p[i * 2 + 1];
        v[i].c[0] = ui_ch_r(rgba);
        v[i].c[1] = ui_ch_g(rgba);
        v[i].c[2] = ui_ch_b(rgba);
        v[i].c[3] = ui_ch_a(rgba);
    }
    nfq++;
}

/* ------------------------------------------------------------------ text */
static void glyph(float x, float y, float px, uint32_t rgba, int ch) {
    if (ch < UI_FONT_FIRST || ch > UI_FONT_LAST) ch = '?';
    const unsigned char *rows = ui_font_rows[ch - UI_FONT_FIRST];
    for (int r = 0; r < UI_FONT_H; r++)
        for (int c = 0; c < UI_FONT_W; c++)
            if (rows[r] & (1 << (4 - c))) ui_rect(x + (float)c * px, y + (float)r * px, px, px, rgba);
}

float ui_text_w(float px, const char *s) {
    if (!s || !*s) return 0;
    return (6.0f * px) * (float)strlen(s) - px;
}

float ui_text(float x, float y, float px, uint32_t rgba, const char *s) {
    if (!s) return 0;
    float cx = x;
    for (const char *p = s; *p; p++, cx += 6.0f * px)
        glyph(cx, y, px, rgba, (unsigned char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p));
    return ui_text_w(px, s);
}

float ui_text_c(float cx, float y, float px, uint32_t rgba, const char *s) {
    return ui_text(cx - ui_text_w(px, s) * 0.5f, y, px, rgba, s);
}
float ui_text_r(float rx, float y, float px, uint32_t rgba, const char *s) {
    return ui_text(rx - ui_text_w(px, s), y, px, rgba, s);
}
void ui_text_sh(float x, float y, float px, uint32_t rgba, uint32_t shadow, const char *s) {
    ui_text(x + px, y + px, px, shadow, s);
    ui_text(x, y, px, rgba, s);
}
