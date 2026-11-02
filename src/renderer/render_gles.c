/* OpenGL ES 3 renderer of the Android port.
 *
 * It draws the same 426x240 view as the Dreamcast backend, from the same FPTX textures, and keeps
 * the picture identical: point sampling, 1-bit palette alpha, the two translucent modes (invert for
 * the hit flash, additive for the shields), the darkening tint of GeneralWater and the fades.  Draw
 * order is the engine's submission order (the PowerVR needed two lists because a list is submitted
 * once; a GL pipeline does not, so painter's order is both simpler and what the game expects).
 *
 * Textures: the levels ship 8-bit paletted atlases and the palette is a per-level table, so the
 * cheapest and most faithful upload is the INDEX ITSELF (GL_R8, one byte per texel -- the same
 * bytes the disc has) plus the palette as a 256x4 lookup texture the fragment shader samples with
 * texelFetch.  That keeps texture memory at the Dreamcast's 8 bpp, makes r_pal_load a 1 KB upload,
 * and needs no CPU conversion at all.  Pre-converted ETC2 atlases (FPTX fmt 5/6, produced by
 * tools/android/make_android_data.py) are uploaded straight as compressed blocks when the tool baked
 * them; ETC2 is mandatory in GLES 3.0, so they always work on a conforming device.
 *
 * Performance: one interleaved dynamic vertex buffer, quads batched per (texture, blend) and drawn
 * with 16-bit indices -- a game frame is a handful of draw calls.  In PIXEL mode the whole view is
 * rendered into a 426x240 texture and upscaled once: fill cost drops by ~20x on a 1080p phone and
 * the pixel grid stays perfectly uniform. */
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fp.h"
#include "render_gles.h"
#include "twiddle.h"
#include "present_geom.h"
#include "../platform/android/ui.h"   /* ui_gles_init(): the shell UI shares the GL context */
#include <math.h>

#define MAXTEX 512                          /* phone VRAM is not the Dreamcast's 8 MB: a level needs ~70, and a
                                               long session used to run out at 192 and silently lose its sprites */
#define MAXQUAD 2048                       /* quads per batch (8192 vertices, fits 16-bit indices) */
#define VIEW_W 426                         /* the logical view (SCREEN_W unless FP_VIEW_320) */
#define VIEW_H 240

/* ------------------------------------------------------------------ vertex format */
typedef struct {
    float x, y, u, v;
    uint32_t rgba;
    uint8_t inv, pad[3];
} vtx_t;

typedef struct {
    GLuint id;
    uint32_t gen;                          /* which load this slot holds (see tex_t.gen) */
    int w, h, indexed, palrow, etc2;
    /* The indices as they were uploaded (indexed atlases only, NULL otherwise). The GPU has no way to tell
     * "this frame is drawn but two thirds of it is empty": r_sprite_report() reads this copy and prints the
     * rect, so a "part of the sprite is missing" report can be answered from the log alone. A level's
     * atlases are the Dreamcast's whole 8 MB of VRAM at most, so the copy costs a few MB. */
    uint8_t *ix;
} gtex_t;

static gtex_t texs[MAXTEX];
static int ntex;
static uint16_t pals[4][256];
static GLuint pal_tex;

static GLuint prog_world, prog_present;
static GLuint vbo, ibo;
static int ibo_quads;
static vtx_t vbuf[MAXQUAD * 4];
static int nquad;
static GLuint cur_tex, cur_pal_grp;
static int cur_blend, cur_indexed;

/* batches actually drawn in the frame (the FPS overlay shows it) */
int rgl_drawcalls, rgl_quads;

/* presentation */
static int win_w = 1280, win_h = 720;
static int opt_mode = RGL_PIXEL, opt_linear, opt_letterbox = 1, opt_scan;
static GLuint fbo, fbo_tex;
static int fbo_w, fbo_h;
static int in_fbo;
static float vsx = 1, vsy = 1, vox, voy;
static int vtex_kb;

/* tint / fade / additive state, as in the PVR backend */
static float rscale = 1.f, rxscale = 1.f;
static int invert_on;
static int add_on;                 /* r_set_additive: the elemental shields and other glow sprites */
uint32_t r_stat_polys;
uint32_t rgl_skips;                        /* sprites dropped for a dead texture handle (see sprite_skip) */
static int rgl_skip_logged;

/* ------------------------------------------------------------------ shaders */
static const char *VS_WORLD =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_col;\n"
    "layout(location=3) in float a_inv;\n"
    "uniform vec2 u_scale, u_offset, u_target;\n"
    "out vec2 v_uv; out vec4 v_col; out float v_inv;\n"
    "void main() {\n"
    "  vec2 p = a_pos * u_scale + u_offset;\n"
    "  gl_Position = vec4(p.x / u_target.x * 2.0 - 1.0, 1.0 - p.y / u_target.y * 2.0, 0.0, 1.0);\n"
    "  v_uv = a_uv; v_col = a_col; v_inv = a_inv;\n"
    "}\n";

static const char *FS_WORLD =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex, u_pal;\n"
    "uniform int u_indexed, u_palrow;\n"
    "in vec2 v_uv; in vec4 v_col; in float v_inv;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 c;\n"
    "  if (u_indexed == 2) {\n"                    /* solid colour (rects, fades) */
    "    c = vec4(1.0);\n"
    "  } else if (u_indexed == 1) {\n"
    "    float ix = texture(u_tex, v_uv).r * 255.0;\n"
    "    c = texelFetch(u_pal, ivec2(int(ix + 0.5), u_palrow), 0);\n"
    "  } else {\n"
    "    c = texture(u_tex, v_uv);\n"
    "  }\n"
    "  vec3 rgb = c.rgb * v_col.rgb;\n"
    "  if (v_inv > 0.5) rgb = 1.0 - rgb;\n"        /* hit flash: the texel colours inverted */
    "  o = vec4(rgb, c.a * v_col.a);\n"
    "}\n";

static const char *VS_PRESENT =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos;\n"
    "uniform vec2 u_scale, u_offset, u_target, u_view, u_viewsize;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 p = a_pos * u_scale + u_offset;\n"
    "  gl_Position = vec4(p.x / u_target.x * 2.0 - 1.0, 1.0 - p.y / u_target.y * 2.0, 0.0, 1.0);\n"
    /* a_pos is the view rect in window pixels: the uv has to span that rect, not the window. Dividing
       by the window zoomed the picture by 1.25x and cropped its sides on a 2.2:1 phone. The target's
       row 0 is the BOTTOM of the view (the world pass maps logical y = 0 to clip y = +1), so v is
       flipped here once. tools/android/check_present.c pins the same arithmetic. */
    "  v_uv = vec2((a_pos.x - u_view.x) / u_viewsize.x, 1.0 - (a_pos.y - u_view.y) / u_viewsize.y);\n"
    "}\n";

static const char *FS_PRESENT =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texsize;\n"
    "uniform int u_scan, u_blur;\n"
    "uniform float u_tint;\n"
    "in vec2 v_uv; out vec4 o;\n"
    "void main() {\n"
    "  vec2 uv = v_uv;\n"
    "  vec4 c;\n"
    "  if (u_blur == 1) {\n"      /* the ambient backdrop: the same picture, smeared and dimmed */
    "    vec2 px = 1.0 / u_texsize;\n"
    "    c  = texture(u_tex, uv) * 0.28;\n"
    "    c += texture(u_tex, uv + vec2( 5.0,  0.0) * px) * 0.14;\n"
    "    c += texture(u_tex, uv + vec2(-5.0,  0.0) * px) * 0.14;\n"
    "    c += texture(u_tex, uv + vec2( 0.0,  5.0) * px) * 0.14;\n"
    "    c += texture(u_tex, uv + vec2( 0.0, -5.0) * px) * 0.14;\n"
    "    c += texture(u_tex, uv + vec2( 6.0,  6.0) * px) * 0.08;\n"
    "    c += texture(u_tex, uv + vec2(-6.0,  6.0) * px) * 0.08;\n"
    "    c += texture(u_tex, uv + vec2( 6.0, -6.0) * px) * 0.08;\n"
    "    c += texture(u_tex, uv + vec2(-6.0, -6.0) * px) * 0.08;\n"
    "  } else {\n"
    "    c = texture(u_tex, uv);\n"
    "  }\n"
    "  if (u_scan == 1) {\n"
    "    float line = mod(floor(gl_FragCoord.y), 3.0);\n"      /* every third row a bit darker */
    "    c.rgb *= (line < 0.5) ? 0.78 : 1.0;\n"
    "  }\n"
    "  o = vec4(c.rgb * u_tint, 1.0);\n"
    "}\n";

static GLuint compile(const char *vs, const char *fs, const char *name) {
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    GLint ok = 0;
    glShaderSource(v, 1, &vs, NULL);
    glCompileShader(v);
    glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(v, sizeof log, NULL, log);
        plat_log("gles: %s vertex: %s\n", name, log);
        return 0;
    }
    glShaderSource(f, 1, &fs, NULL);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(f, sizeof log, NULL, log);
        plat_log("gles: %s fragment: %s\n", name, log);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        plat_log("gles: %s link: %s\n", name, log);
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* ------------------------------------------------------------------ init */
static void make_palette_tex(void) {
    uint8_t px[256 * 4 * 4];
    memset(px, 0, sizeof px);
    glGenTextures(1, &pal_tex);
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void palette_upload(int grp) {
    uint8_t px[256 * 4];
    for (int i = 0; i < 256; i++) {
        uint16_t c = pals[grp & 3][i];
        int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        px[i * 4 + 0] = (uint8_t)((r << 3) | (r >> 2));
        px[i * 4 + 1] = (uint8_t)((g << 3) | (g >> 2));
        px[i * 4 + 2] = (uint8_t)((b << 3) | (b >> 2));
        px[i * 4 + 3] = (c & 0x8000) ? 255 : 0;      /* ARGB1555: the alpha bit is the punch-through */
    }
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, grp & 3, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

int rgl_init(void) {
    if (ui_gles_init() != 0) { plat_log("gles: the UI backend failed to initialise\n"); return -1; }
    prog_world = compile(VS_WORLD, FS_WORLD, "world");
    prog_present = compile(VS_PRESENT, FS_PRESENT, "present");
    if (!prog_world || !prog_present) return -1;

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof vbuf, NULL, GL_STREAM_DRAW);

    /* one static index buffer: 0,1,2, 0,2,3 per quad */
    static uint16_t idx[MAXQUAD * 6];
    if (!ibo_quads) {
        for (int q = 0; q < MAXQUAD; q++) {
            uint16_t *p = &idx[q * 6];
            p[0] = (uint16_t)(q * 4); p[1] = (uint16_t)(q * 4 + 1); p[2] = (uint16_t)(q * 4 + 2);
            p[3] = (uint16_t)(q * 4); p[4] = (uint16_t)(q * 4 + 2); p[5] = (uint16_t)(q * 4 + 3);
        }
        glGenBuffers(1, &ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof idx, idx, GL_STATIC_DRAW);
        ibo_quads = MAXQUAD;
    }

    make_palette_tex();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    for (int i = 0; i < MAXTEX; i++) texs[i].id = 0;
    ntex = 0;
    plat_log("gles: renderer up (%dx%d)\n", win_w, win_h);
    return 0;
}

void rgl_shutdown(void) {
    for (int i = 0; i < ntex; i++)
        if (texs[i].id) glDeleteTextures(1, &texs[i].id);
    ntex = 0;
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (fbo_tex) { glDeleteTextures(1, &fbo_tex); fbo_tex = 0; }
    if (pal_tex) { glDeleteTextures(1, &pal_tex); pal_tex = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; ibo_quads = 0; }
    if (prog_world) { glDeleteProgram(prog_world); prog_world = 0; }
    if (prog_present) { glDeleteProgram(prog_present); prog_present = 0; }
}

int rgl_window_w(void) { return win_w; }
int rgl_window_h(void) { return win_h; }
int rgl_vram_kb(void) { return vtex_kb; }
uint32_t rgl_sprite_skips(void) { return rgl_skips; }
int rgl_tex_slots(void) { return ntex; }
static uint32_t tex_gen;                   /* bumped per successful load: stale handles go dead */
static int tex_live(const tex_t *t) {
    return t && t->id >= 0 && t->id < ntex && texs[t->id].id && texs[t->id].gen == t->gen && t->gen != 0;
}
int r_tex_alive(const tex_t *t) { return tex_live(t); }

static void fbo_make(int w, int h) {
    if (fbo_tex) glDeleteTextures(1, &fbo_tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glGenTextures(1, &fbo_tex);
    glActiveTexture(GL_TEXTURE0);          /* flush() leaves unit 1 (the palette) active */
    glBindTexture(GL_TEXTURE_2D, fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
    fbo_w = w; fbo_h = h;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        plat_log("gles: %dx%d framebuffer incomplete, falling back to direct drawing\n", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void rgl_resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    win_w = w; win_h = h;
    if (!fbo || (opt_mode == RGL_PIXEL && (fbo_w != VIEW_W || fbo_h != VIEW_H))) fbo_make(VIEW_W, VIEW_H);
}

void rgl_set_mode(int mode, int filter_linear, int letterbox, int scanlines) {
    opt_mode = mode == RGL_SHARP ? RGL_SHARP : RGL_PIXEL;
    opt_linear = filter_linear ? 1 : 0;
    opt_letterbox = letterbox ? 1 : 0;
    opt_scan = scanlines ? 1 : 0;
    if (opt_mode == RGL_PIXEL && (!fbo || fbo_w != VIEW_W || fbo_h != VIEW_H)) fbo_make(VIEW_W, VIEW_H);
}

/* the placement of the logical view inside the window (present_geom.h has the arithmetic) */
static void view_transform(float *sx, float *sy, float *ox, float *oy) {
    view_rect_t r = view_fit(win_w, win_h, VIEW_W, VIEW_H, opt_letterbox);
    *sx = (r.x1 - r.x0) / (float)VIEW_W;
    *sy = (r.y1 - r.y0) / (float)VIEW_H;
    *ox = r.x0;
    *oy = r.y0;
}

/* the rectangle this frame was drawn for: rgl_present() must not re-read opt_letterbox, or a game
 * that flips its own SCREEN SIZE option mid-frame presents half a frame in one mode and half in the
 * other (a horizontal seam in the middle of the picture) */
static float pv_x0, pv_y0, pv_x1, pv_y1;

/* ------------------------------------------------------------------ batching */
static void flush(void) {
    if (!nquad) return;
    glUseProgram(prog_world);
    glUniform2f(glGetUniformLocation(prog_world, "u_scale"), vsx, vsy);
    glUniform2f(glGetUniformLocation(prog_world, "u_offset"), vox, voy);
    glUniform2f(glGetUniformLocation(prog_world, "u_target"), in_fbo ? (float)VIEW_W : (float)win_w,
                in_fbo ? (float)VIEW_H : (float)win_h);
    glUniform1i(glGetUniformLocation(prog_world, "u_indexed"), cur_indexed);
    glUniform1i(glGetUniformLocation(prog_world, "u_palrow"), (int)(cur_pal_grp & 3));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_tex ? cur_tex : pal_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pal_tex);
    glUniform1i(glGetUniformLocation(prog_world, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(prog_world, "u_pal"), 1);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)nquad * 4 * (GLsizeiptr)sizeof(vtx_t), vbuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (void *)(sizeof(float) * 2));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vtx_t), (void *)(sizeof(float) * 4));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(vtx_t), (void *)(sizeof(float) * 4 + 4));
    glDrawElements(GL_TRIANGLES, nquad * 6, GL_UNSIGNED_SHORT, (void *)0);
    rgl_drawcalls++;
    rgl_quads += nquad;
    nquad = 0;
}

static void set_blend(int mode) {
    if (mode == cur_blend && nquad) return;
    flush();
    cur_blend = mode;
    switch (mode) {
    case 1:                                     /* additive: dst + src * a */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case 2:                                     /* r_tint: dst *= (1 - src) */
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
        break;
    default:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

static void bind_tex(const tex_t *t) {
    const gtex_t *g = &texs[t->id];
    if (nquad && (cur_tex != g->id || cur_indexed != g->indexed || cur_pal_grp != (GLuint)g->palrow)) flush();
    cur_tex = g->id;
    cur_indexed = g->indexed;
    cur_pal_grp = (GLuint)g->palrow;
}

static inline void push_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                             uint32_t argb, int inv) {
    if (nquad >= MAXQUAD) flush();
    vtx_t *v = &vbuf[nquad * 4];
    uint32_t c = argb;
    /* u8 colour, alpha first: the shader multiplies the texel by it */
    v[0] = (vtx_t){ x0, y0, u0, v0, c, (uint8_t)(inv ? 1 : 0), { 0, 0, 0 } };
    v[1] = (vtx_t){ x1, y0, u1, v0, c, (uint8_t)(inv ? 1 : 0), { 0, 0, 0 } };
    v[2] = (vtx_t){ x1, y1, u1, v1, c, (uint8_t)(inv ? 1 : 0), { 0, 0, 0 } };
    v[3] = (vtx_t){ x0, y1, u0, v1, c, (uint8_t)(inv ? 1 : 0), { 0, 0, 0 } };
    nquad++;
    r_stat_polys++;
}

/* ------------------------------------------------------------------ r_* API */
int r_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return 0;
}

/* The game's own SCREEN SIZE option (4:3 bars on a Dreamcast output). On the phone the port's ASPECT
 * setting decides how the view fits the screen, and letting the game flip the same flag meant that
 * apply_settings() and the game fought over it every frame: the presentation changed size between the
 * background and the sprites of one frame. The game's option is a no-op here, as it already is in the
 * Dreamcast build with FP_VIEW_320. */
void r_set_letterbox(int on) { (void)on; }
void r_set_additive(int on) { add_on = on ? 1 : 0; }
void r_set_invert(int on) { invert_on = on ? 1 : 0; }
void r_set_scale(float s) { rscale = s; }
float r_get_scale(void) { return rscale; }
void r_set_xscale(float s) { rxscale = s; }
float r_get_xscale(void) { return rxscale; }

static int tex_slot(void) {
    for (int i = 0; i < ntex; i++)
        if (!texs[i].id) return i;
    if (ntex >= MAXTEX) return -1;
    return ntex++;
}

static void upload_rgba8(GLuint id, int w, int h, const uint8_t *px) {
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void tex_params(int linear) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* A texture that failed to load leaves a DEAD handle (id -1). It used to leave whatever was there - 0 after
 * a memset - and 0 is a real table slot, so the game drew the sprite with somebody else's atlas: stretched
 * garbage, or nothing at all when that atlas was transparent there. Every early return now goes through
 * here, and every reason is logged: "sprites missing" reports had no trace at all before. */
int r_tex_load(tex_t *t, const void *data, size_t size) {
    const uint8_t *d = data;
    uint16_t hdr[6];
    int w = 0, h = 0, fmt = -1, grp = 0;
    t->id = -1; t->w = t->h = t->srcw = t->srch = 0; t->gen = 0;
    if (size < 16 || memcmp(d, "FPTX", 4)) { plat_log("gles: texture not FPTX (%zu bytes)\n", size); return -1; }
    memcpy(hdr, d + 4, 12);
    w = hdr[0]; h = hdr[1]; fmt = hdr[2]; grp = hdr[5];
    const uint8_t *src = d + 16;
    if (w < 8 || h < 8 || (w & (w - 1)) || (h & (h - 1))) { plat_log("gles: texture %dx%d is not power of two\n", w, h); return -1; }
    int slot = tex_slot();
    if (slot < 0) { plat_log("gles: texture table full (%dx%d)\n", w, h); return -1; }
    if (ntex == MAXTEX * 3 / 4) plat_log("gles: %d of %d texture slots in use\n", ntex, MAXTEX);
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    tex_params(0);
    gtex_t *g = &texs[slot];
    g->w = w; g->h = h; g->id = id; g->indexed = 0; g->palrow = grp & 3; g->etc2 = 0; g->ix = NULL;
    int ok = 0, bytes = 0;

    if (fmt == 1 || fmt == 2 || fmt == 3) {
        /* paletted: upload the index bytes, 1 byte per texel (the Dreamcast's 8 bpp) */
        uint8_t *ix = malloc((size_t)w * h);
        if (!ix) { glDeleteTextures(1, &id); g->id = 0; return -1; }
        if (fmt == 1 || fmt == 2) {
            if (grp > 3 || size < 16 + (size_t)w * h) { free(ix); glDeleteTextures(1, &id); g->id = 0; return -1; }
            if (fmt == 1) memcpy(ix, src, (size_t)w * h);
            else for (int y = 0; y < h; y++)                 /* pre-twiddled for the PVR: undo it */
                for (int x = 0; x < w; x++) ix[y * w + x] = src[fp_untwiddle_offset(x, y, w, h)];
        } else {
            if (grp > 63 || size < 16 + (size_t)w * h / 2) { free(ix); glDeleteTextures(1, &id); g->id = 0; return -1; }
            int base = (grp % 16) * 16;                      /* 16-entry palette bank (parallax) */
            for (size_t i = 0; i < (size_t)w * h; i++) {
                int v = (src[i / 2] >> ((i & 1) * 4)) & 15;
                ix[i] = (uint8_t)(v ? base + v : 0);
            }
            g->palrow = grp / 16;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, ix);
        g->ix = ix;                                      /* kept for r_sprite_report, freed with the slot */
        g->indexed = 1;
        bytes = w * h;
        ok = 1;
    } else if (fmt == 0 || fmt == 4) {
        /* direct colour: expand to RGBA8 (a handful of textures: mist, some UI layers) */
        if (size < 16 + (size_t)w * h * 2) { glDeleteTextures(1, &id); g->id = 0; return -1; }
        uint8_t *px = malloc((size_t)w * h * 4);
        if (!px) { glDeleteTextures(1, &id); g->id = 0; return -1; }
        for (int i = 0; i < w * h; i++) {
            uint16_t c = (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
            if (fmt == 4) {                                  /* ARGB4444 */
                px[i * 4 + 0] = (uint8_t)(((c >> 8) & 15) * 17);
                px[i * 4 + 1] = (uint8_t)(((c >> 4) & 15) * 17);
                px[i * 4 + 2] = (uint8_t)((c & 15) * 17);
                px[i * 4 + 3] = (uint8_t)(((c >> 12) & 15) * 17);
            } else {                                         /* ARGB1555 */
                int r = (c >> 10) & 31, gg = (c >> 5) & 31, b = c & 31;
                px[i * 4 + 0] = (uint8_t)((r << 3) | (r >> 2));
                px[i * 4 + 1] = (uint8_t)((gg << 3) | (gg >> 2));
                px[i * 4 + 2] = (uint8_t)((b << 3) | (b >> 2));
                px[i * 4 + 3] = (c & 0x8000) ? 255 : 0;
            }
        }
        upload_rgba8(id, w, h, px);
        free(px);
        bytes = w * h * 4;
        ok = 1;
    } else if (fmt == 5 || fmt == 6) {
        /* ETC2 baked by tools/android/make_android_data.py: upload the blocks as they are */
        int blocks = (w / 4) * (h / 4);
        size_t need = (size_t)blocks * (fmt == 5 ? 8 : 16);
        GLenum ifmt = fmt == 5 ? GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 : GL_COMPRESSED_RGBA8_ETC2_EAC;
        if (size < 16 + need) { glDeleteTextures(1, &id); g->id = 0; return -1; }
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, (GLsizei)need, src);
        if (glGetError() == GL_NO_ERROR) { ok = 1; bytes = (int)need; g->etc2 = 1; }
    } else {
        plat_log("gles: unknown texture format %d\n", fmt);
    }
    if (!ok) { glDeleteTextures(1, &id); g->id = 0; return -1; }
    vtex_kb += bytes / 1024;
    g->gen = ++tex_gen;
    t->id = slot; t->w = w; t->h = h; t->srcw = hdr[3]; t->srch = hdr[4]; t->gen = g->gen;
    return 0;
}

void r_tex_free(tex_t *t) {
    /* the slot may have been reused (another load, or r_reset then a new level): then the atlas in it is
     * NOT this handle's, and deleting it would blank somebody else's sprites */
    if (!tex_live(t)) { t->id = -1; t->gen = 0; return; }
    flush();
    glDeleteTextures(1, &texs[t->id].id);
    free(texs[t->id].ix);
    texs[t->id].ix = NULL;
    texs[t->id].id = 0;
    texs[t->id].gen = 0;
    t->id = -1;
    t->gen = 0;
}

void r_reset(void) {
    flush();
    vtex_kb = 0;
    for (int i = 0; i < ntex; i++) {
        if (texs[i].id) { glDeleteTextures(1, &texs[i].id); texs[i].id = 0; }
        free(texs[i].ix);
        texs[i].ix = NULL;
    }
    ntex = 0;
}

/* The palette is what makes a paletted sprite visible at all: its alpha bit is the punch-through, so a
 * group whose 256 entries all came out transparent draws nothing, however correct the geometry is. That is
 * a silent, screen-wide "the characters and the enemies are gone" bug, so it is logged, and an all-invisible
 * group is repaired (entry 0 stays transparent: that is the sprite's own transparent texel). */
int r_pal_load(const void *data, size_t size) {
    const uint8_t *d = data;
    uint16_t g;
    if (size < 8 + 512 || memcmp(d, "FPPL", 4)) { plat_log("pal: not FPPL (%zu bytes)\n", size); return -1; }
    memcpy(&g, d + 4, 2);
    if (g > 3) { plat_log("pal: group %u out of range\n", (unsigned)g); return -1; }
    memcpy(pals[g], d + 8, 512);
    int opaque = 0;
    for (int i = 0; i < 256; i++) if (pals[g][i] & 0x8000) opaque++;
    if (!opaque) {
        for (int i = 1; i < 256; i++) pals[g][i] |= 0x8000;
        plat_log("pal %u: every entry transparent, forced opaque\n", (unsigned)g);
        opaque = 255;
    }
    plat_log("pal %u: %d of 256 opaque\n", (unsigned)g, opaque);
    palette_upload(g);
    return 0;
}

/* geometry the game pushed into the frame that was just drawn: 0 means the picture is a bare clear */
int rgl_quad_count(void) { return rgl_quads; }

void r_begin(void) {
    rgl_drawcalls = 0;
    rgl_quads = 0;
    nquad = 0;
    cur_tex = 0; cur_indexed = -1; cur_pal_grp = 0xffffffffu; cur_blend = -1;
    invert_on = 0;
    rscale = rxscale = 1.f;
    r_stat_polys = 0;
    glBindVertexArray(0);                 /* the shell UI leaves its own VAO bound after ui_begin() */
    in_fbo = opt_mode == RGL_PIXEL && fbo && fbo_w == VIEW_W;
    {
        view_rect_t r = view_fit(win_w, win_h, VIEW_W, VIEW_H, opt_letterbox);
        pv_x0 = r.x0; pv_y0 = r.y0; pv_x1 = r.x1; pv_y1 = r.y1;
    }
    if (in_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, VIEW_W, VIEW_H);
        vsx = vsy = 1; vox = voy = 0;
        glClearColor(0, 0, 0, 1);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, win_w, win_h);
        view_transform(&vsx, &vsy, &vox, &voy);
        /* direct drawing has no finished frame to smear into the bars, so they get a dark matte
         * instead of black.  The clear colour is process-wide state that the shell's ui_clear() also
         * sets, so every branch has to set it: in FILL mode (no letterbox) the game used to clear with
         * whatever the launcher left behind -- a navy blue backdrop around the picture. */
        if (opt_letterbox) glClearColor(0.055f, 0.062f, 0.078f, 1.0f);
        else glClearColor(0, 0, 0, 1);
    }
    glClear(GL_COLOR_BUFFER_BIT);
}

void (*rgl_frame_end_hook)(void);

void r_end(void) {
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (rgl_frame_end_hook) rgl_frame_end_hook();
}

/* one quad of the upscale: the picture, or the dimmed backdrop behind it.  vx,vy/vw,vh is the part of
 * the 426x240 texture the quad shows (the view rect for the picture, the whole texture for the
 * backdrop), which is why the shader needs it: dividing by the *window* instead zoomed the picture by
 * 1.25x and cut its sides off on a 2.2:1 phone. */
static void present_draw(float qx0, float qy0, float qx1, float qy1,
                         float vx, float vy, float vw, float vh, int blur, float tint) {
    glUniform2f(glGetUniformLocation(prog_present, "u_scale"), 1, 1);
    glUniform2f(glGetUniformLocation(prog_present, "u_offset"), 0, 0);
    glUniform2f(glGetUniformLocation(prog_present, "u_target"), (float)win_w, (float)win_h);
    glUniform2f(glGetUniformLocation(prog_present, "u_view"), vx, vy);
    glUniform2f(glGetUniformLocation(prog_present, "u_viewsize"), vw, vh);
    glUniform2f(glGetUniformLocation(prog_present, "u_texsize"), (float)VIEW_W, (float)VIEW_H);
    glUniform1i(glGetUniformLocation(prog_present, "u_scan"), blur ? 0 : opt_scan);
    glUniform1i(glGetUniformLocation(prog_present, "u_blur"), blur);
    glUniform1f(glGetUniformLocation(prog_present, "u_tint"), tint);
    glUniform1i(glGetUniformLocation(prog_present, "u_tex"), 0);
    static vtx_t q[4];
    q[0] = (vtx_t){ qx0, qy0, 0, 0, 0xffffffffu, 0, { 0, 0, 0 } };
    q[1] = (vtx_t){ qx1, qy0, 1, 0, 0xffffffffu, 0, { 0, 0, 0 } };
    q[2] = (vtx_t){ qx1, qy1, 1, 1, 0xffffffffu, 0, { 0, 0, 0 } };
    q[3] = (vtx_t){ qx0, qy1, 0, 1, 0xffffffffu, 0, { 0, 0, 0 } };
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof q, q);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vtx_t), (void *)(sizeof(float) * 2));
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0);
    rgl_drawcalls++;
}

/* the frame's pixels are on screen: upscale the 240p view (the platform swaps the buffers) */
void rgl_present(void) {
    if (!in_fbo) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);            /* only a first-frame fallback: the backdrop covers it */
    glDisable(GL_BLEND);
    glUseProgram(prog_present);
    /* The upscale samples the *framebuffer's* texture. Without this bind it sampled whatever atlas the
     * last flush left on unit 0, and the 426x240 picture became a magnified slice of a sprite sheet: the
     * PIXEL render mode drew a wall of tiny character faces and smeared text instead of the game. (The
     * SHARP mode draws straight to the window and never noticed, which is why it looked like the mode
     * switch was the trigger.) */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo_tex);
    float tw = (float)win_w, th = (float)win_h;
    float x0 = pv_x0, y0 = pv_y0, x1 = pv_x1, y1 = pv_y1;
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) { x0 = 0; y0 = 0; x1 = tw; y1 = th; }
    /* the bars of the aspect-preserving mode are not black bars: the same picture, smeared and
     * dimmed, fills the window behind it, so a 2.2:1 phone looks filled and the play area keeps its
     * exact shape (nothing is stretched and nothing is cut off) */
    {
        int fills = x0 <= 1.0f && y0 <= 1.0f && x1 >= tw - 1.0f && y1 >= th - 1.0f;
        if (!fills) present_draw(0, 0, tw, th, 0, 0, tw, th, 1, 0.34f);
    }
    present_draw(x0, y0, x1, y1, x0, y0, x1 - x0, y1 - y0, 0, 1.0f);
}


/* Sprites thrown away because their handle is dead: the first few are logged with the handle and the atlas
 * size, which is what tells a "the sprite is not drawn at all" report apart from "it is drawn somewhere
 * else". A long tail of these once meant a leaked texture slot (a failed bank load kept its atlas). */
/* a frame whose rect runs off its atlas: the bank loader rejects those, so seeing one means the rect
 * was changed after the load (or came from a stale animator). Log the first few: the numbers say which
 * sprite it is (and that the frame is drawn whole, sampling the clamped edge, as the PVR does). */
static int clip_logged;
static void clip_log(const tex_t *t, int u, int v, int uw, int vh) {
    if (clip_logged >= 8) return;
    clip_logged++;
    plat_log("gles: frame (%d,%d %dx%d) runs past its %dx%d atlas, drawn whole (edge texel repeats)\n",
             u, v, uw, vh, t->w, t->h);
}

static void sprite_skip(const tex_t *t, int uw, int vh) {
    if (rgl_skip_logged >= 8) return;
    if (++rgl_skip_logged > 8) return;
    plat_log("gles: sprite skipped: handle %d, atlas %dx%d, quad %dx%d\n", t->id, t->w, t->h, uw, vh);
}

void r_sprite(const tex_t *t, int u, int v, int uw, int vh, float x, float y, float w, float h, int flip, uint32_t argb) {
    if (!tex_live(t) || w <= 0 || h <= 0) { rgl_skips++; sprite_skip(t, uw, vh); return; }
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    /* The Dreamcast backend draws the frame's whole rect and lets the PVR clamp the sample to the edge of
     * the texture (render_pvr.c r_sprite has no guard at all). This backend used to CUT the quad to the
     * atlas instead, so a frame whose rect overruns by even one texel lost that strip of the picture on the
     * phone and was whole on the Dreamcast -- the "she is not drawn all the way" report. GL_CLAMP_TO_EDGE
     * (set in tex_params) now does exactly what the hardware did; the numbers still go into the log. */
    {   const gtex_t *g = &texs[t->id];
        if (u < 0 || v < 0 || u + uw > g->w || v + vh > g->h) clip_log(t, u, v, uw, vh);
        if (uw <= 0 || vh <= 0) return;
    }
    bind_tex(t);
    set_blend(add_on ? 1 : 0);
    float iw = 1.0f / (float)texs[t->id].w, ih = 1.0f / (float)texs[t->id].h;
    float u0 = (float)u * iw, u1 = (float)(u + uw) * iw, v0 = (float)v * ih, v1 = (float)(v + vh) * ih;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    push_quad(x, y, x + w, y + h, u0, v0, u1, v1, argb, invert_on);
}

void r_sprite_rot(const tex_t *t, int u, int v, int uw, int vh, float px, float py, float hx, float hy,
                  float angle, int flip, uint32_t argb) {
    if (!tex_live(t)) { rgl_skips++; return; }
    {   /* same rule as r_sprite: the Dreamcast draws the whole rect and clamps the sample, it never cuts the
         * quad, so a frame that overruns loses nothing here either (it is still logged) */
        const gtex_t *g = &texs[t->id];
        if (u < 0 || v < 0 || u + uw > g->w || v + vh > g->h) clip_log(t, u, v, uw, vh);
        if (uw <= 0 || vh <= 0) return;
    }
    if (flip) hx = uw - 1 - hx;
    float ex = (hx > uw - hx ? hx : uw - hx) * (rxscale > 1.f ? rxscale : 1.f) * rscale;
    float ey = (hy > vh - hy ? hy : vh - hy) * rscale;
    float r = sqrtf(ex * ex + ey * ey) + 2;
    if (px + r < 0 || py + r < 0 || px - r >= SCREEN_W || py - r >= SCREEN_H) return;
    bind_tex(t);
    set_blend(add_on ? 1 : 0);
    float iw = 1.0f / (float)texs[t->id].w, ih = 1.0f / (float)texs[t->id].h;
    float u0 = (float)u * iw, u1 = (float)(u + uw) * iw, v0 = (float)v * ih, v1 = (float)(v + vh) * ih;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    float a = angle * 0.017453293f, c = cosf(a) * rscale, s = sinf(a) * rscale;
    float lx[4] = { -hx, uw - hx, -hx, uw - hx }, ly[4] = { -hy, -hy, vh - hy, vh - hy };
    float tu[4] = { u0, u1, u0, u1 }, tv[4] = { v0, v0, v1, v1 };
    if (nquad >= MAXQUAD) flush();
    vtx_t *vt = &vbuf[nquad * 4];
    for (int i = 0; i < 4; i++) {
        vt[i].x = px + lx[i] * rxscale * c + ly[i] * s;
        vt[i].y = py - lx[i] * rxscale * s + ly[i] * c;
        vt[i].u = tu[i];
        vt[i].v = tv[i];
        vt[i].rgba = argb;
        vt[i].inv = (uint8_t)(invert_on ? 1 : 0);
        vt[i].pad[0] = vt[i].pad[1] = vt[i].pad[2] = 0;
    }
    nquad++;
    r_stat_polys++;
}

/* What the atlas really holds under one frame, into the log.
 *
 * "The sprite is not drawn all the way" reads the same whether the frame rect is right and the sheet is
 * empty there, or the sheet is fine and the palette makes part of it invisible -- so the two are told
 * apart with numbers: '#" draws, '-' has an index but the palette entry's alpha bit is 0 (that texel is
 * invisible however correct the geometry), '.' is index 0, nothing in the atlas at all. The ranges of
 * empty rows and columns say whether a hole runs across the whole picture, and the palette line says
 * whether that palette group can show anything in the first place. */
int r_sprite_report(const tex_t *t, int u, int v, int uw, int vh) {
    if (!t || !tex_live(t)) { plat_log("sprite report: the handle is dead, there is no atlas to read\n"); return -1; }
    const gtex_t *g = &texs[t->id];
    if (!g->ix) {
        plat_log("sprite report: %dx%d keeps no index copy (%s)\n", g->w, g->h,
                 g->etc2 ? "ETC2" : "direct colour");
        return -1;
    }
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u + uw > g->w) uw = g->w - u;
    if (v + vh > g->h) vh = g->h - v;
    if (uw <= 0 || vh <= 0) {
        plat_log("sprite report: frame (%d,%d %dx%d) does not overlap its %dx%d atlas\n", u, v, uw, vh, g->w, g->h);
        return -1;
    }
    static uint16_t rowany[1024], colany[1024];
    long draw = 0, hidden = 0, empty = 0;
    int nr = vh > 1024 ? 1024 : vh, nc = uw > 1024 ? 1024 : uw;
    memset(rowany, 0, sizeof rowany[0] * (size_t)nr);
    memset(colany, 0, sizeof colany[0] * (size_t)nc);
    for (int y = 0; y < nr; y++)
        for (int x = 0; x < nc; x++) {
            int ix = g->ix[(size_t)(v + y) * g->w + u + x];
            if (!ix) { empty++; continue; }
            if (pals[g->palrow & 3][ix] & 0x8000) draw++; else { hidden++; continue; }
            rowany[y]++; colany[x]++;
        }
    int op = 0;
    for (int i = 1; i < 256; i++) if (pals[g->palrow & 3][i] & 0x8000) op++;
    plat_log("sprite report: frame (%d,%d %dx%d) of its %dx%d atlas, palette row %d (%d of 256 opaque)\n",
             u, v, uw, vh, g->w, g->h, g->palrow & 3, op);
    plat_log("sprite report: %ld texels: %ld draw, %ld have an index but are invisible in that palette, %ld empty\n",
             (long)nr * nc, draw, hidden, empty);
    {   /* the rows and columns in which nothing draws at all: a frame cut by the edge of the sheet, or a rect
         * that lies across the empty padding between two sprites, shows up here as a range in the middle */
        int y = 0;
        while (y < nr) {
            if (rowany[y]) { y++; continue; }
            int s = y;
            while (y < nr && !rowany[y]) y++;
            plat_log("sprite report: rows %d..%d of the frame draw nothing\n", s, y - 1);
        }
        int x = 0;
        while (x < nc) {
            if (colany[x]) { x++; continue; }
            int s = x;
            while (x < nc && !colany[x]) x++;
            plat_log("sprite report: columns %d..%d of the frame draw nothing\n", s, x - 1);
        }
    }
    {   /* the picture itself, at most 64 columns: one character per texel (or per cell of step x step) */
        int sx = (uw + 63) / 64, sy = (vh + 63) / 64, n = 0;
        if (sx < 1) sx = 1;
        if (sy < 1) sy = 1;
        char line[80];
        for (int y = 0; y < vh && n < 64; y += sy, n++) {
            int k = 0;
            for (int x = 0; x < uw && k < 64; x += sx, k++) {
                int ix = g->ix[(size_t)(v + y) * g->w + u + x];
                line[k] = !ix ? '.' : (pals[g->palrow & 3][ix] & 0x8000) ? '#' : '-';
            }
            line[k] = 0;
            plat_log("sprite report: |%s|%s\n", line, (sx > 1 || sy > 1) ? " (every 2nd+ texel)" : "");
        }
    }
    return 0;
}

void r_rect(float x, float y, float w, float h, uint32_t argb) {
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    if ((argb >> 24) == 0) return;
    if (cur_tex || cur_indexed != 2) { flush(); cur_tex = 0; cur_indexed = 2; }   /* solid: no texture */
    set_blend(add_on ? 1 : 0);
    push_quad(x, y, x + w, y + h, 0, 0, 0, 0, argb, invert_on);
}

/* dst *= (1 - rgb): the water darkening (Chowdren blend 20) */
void r_tint(float x, float y, float w, float h, uint32_t rgb) {
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H || w <= 0 || h <= 0) return;
    flush();
    cur_tex = 0;
    cur_indexed = 2;
    cur_blend = -1;
    set_blend(2);
    push_quad(x, y, x + w, y + h, 0, 0, 0, 0, rgb | 0xff000000u, 0);
    flush();
    cur_blend = -1;
}

void r_fade(float alpha) {
    if (alpha <= 0) return;
    if (alpha > 1) alpha = 1;
    r_rect(0, 0, (float)SCREEN_W, (float)SCREEN_H, ((uint32_t)(alpha * 255) << 24));
}
