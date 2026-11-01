/* The lifecycle of a texture handle: does it survive the texture table being emptied?
 *
 * The bug this test pins: engine_stop() -> game_unload_all() -> r_reset() frees every atlas and starts
 * the table over, but the front end only remembered "I loaded the title once" (a flag). On the next
 * PLAY the flag said "already loaded", every handle it owned pointed into a table that no longer held
 * those atlases, and the title screen drew nothing at all: a black screen behind the touch pad on the
 * second launch. A handle has to be able to answer "am I still a texture?", and freeing a stale handle
 * must never delete the atlas that took the slot afterwards.
 *
 * Links the real render_gles.c against a fake GL (no device, no NDK). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <GLES3/gl3.h>

/* ------------------------------------------------------------------ the fake GL */
static int gen_calls, del_calls;
int glGenTextures(GLsizei n, GLuint *v) { (void)n; if (v) *v = 1000u + (GLuint)gen_calls; gen_calls++; return 0; }
int glDeleteTextures(GLsizei n, const GLuint *v) { (void)n; (void)v; del_calls++; return 0; }
/* which texture is bound on which unit: the upscale has to sample the framebuffer's texture */
static GLuint bound_on[2];
static GLenum active_unit = GL_TEXTURE0;
int glActiveTexture(GLenum u) { active_unit = u; return 0; }
int glBindTexture(GLenum target, GLuint id) {
    (void)target;
    int slot = (active_unit == GL_TEXTURE1) ? 1 : 0;
    bound_on[slot] = id;
    return 0;
}
int glBindVertexArray(GLuint v) { (void)v; return 0; }
int glGenVertexArrays(GLsizei n, GLuint *v) { (void)n; if (v) *v = 1; return 0; }
int glDeleteVertexArrays(GLsizei n, GLuint *v) { (void)n; (void)v; return 0; }
int glTexParameteri() { return 0; }
int glTexImage2D() { return 0; }
int glTexSubImage2D() { return 0; }
int glCompressedTexImage2D() { return 0; }
int glPixelStorei() { return 0; }
int glGetError() { return 0; }
int glUseProgram() { return 0; }
int glCreateShader() { return 0; }
int glShaderSource() { return 0; }
int glCompileShader() { return 0; }
int glGetShaderiv() { return 0; }
int glGetShaderInfoLog() { return 0; }
int glCreateProgram() { return 0; }
int glAttachShader() { return 0; }
int glLinkProgram() { return 0; }
int glGetProgramiv() { return 0; }
int glGetProgramInfoLog() { return 0; }
int glDeleteShader() { return 0; }
int glDeleteProgram() { return 0; }
int glGetUniformLocation() { return 0; }
int glUniform1i() { return 0; }
int glUniform2f() { return 0; }
int glUniform1f() { return 0; }
int glGenBuffers() { return 0; }
int glDeleteBuffers() { return 0; }
int glBindBuffer() { return 0; }
int glBufferData() { return 0; }
int glBufferSubData() { return 0; }
int glEnableVertexAttribArray() { return 0; }
int glDisableVertexAttribArray() { return 0; }
int glVertexAttribPointer() { return 0; }
int glDrawElements() { return 0; }
int glGenFramebuffers() { return 0; }
int glDeleteFramebuffers() { return 0; }
int glBindFramebuffer() { return 0; }
int glFramebufferTexture2D() { return 0; }
int glCheckFramebufferStatus() { return 0; }
int glViewport() { return 0; }
int glEnable() { return 0; }
int glDisable() { return 0; }
int glBlendFunc() { return 0; }
int glClearColor() { return 0; }
int glClear() { return 0; }

static int log_lines;
void plat_log(const char *fmt, ...) { (void)fmt; log_lines++; }
int ui_gles_init(void) { return 0; }         /* the shell UI shares the context: not needed here */

/* the backend itself, so the test sees its real slot table and generation counter */
#include "../../src/renderer/twiddle.c"
#include "../../src/renderer/render_gles.c"

/* ------------------------------------------------------------------ the checks */
static int fails;
static void check(int cond, const char *what) {
    printf("  %-62s %s\n", what, cond ? "ok" : "FAILED");
    if (!cond) fails++;
}

/* a minimal FPTX: PAL8 (fmt 1, not twiddled), 8x8, palette group 0 */
static void fptx_make(uint8_t *b, int w, int h, uint8_t fill) {
    uint16_t hdr[6] = { (uint16_t)w, (uint16_t)h, 1, (uint16_t)w, (uint16_t)h, 0 };
    memcpy(b, "FPTX", 4);
    memcpy(b + 4, hdr, 12);
    memset(b + 16, fill, (size_t)w * h);
}

static int load(tex_t *t, const uint8_t *buf, size_t n) {
    memset(t, 0, sizeof *t); t->id = -1;     /* what bank_load/ui_banks_load start from */
    return r_tex_load(t, buf, n);
}

int main(void) {
    uint8_t buf[16 + 8 * 8];
    tex_t a, b, c;
    fptx_make(buf, 8, 8, 1);

    /* 1. a loaded atlas is alive */
    check(load(&a, buf, sizeof buf) == 0, "a valid FPTX loads");
    check(r_tex_alive(&a), "and its handle says so");
    check(load(&b, buf, sizeof buf) == 0 && r_tex_alive(&b), "a second one loads into its own slot");
    check(r_tex_alive(&a), "which does not disturb the first");

    /* 2. the engine restart: game_unload_all() -> r_reset() empties the table. This is the black screen:
     *    without the generation check the handle still "looked" loaded and the front end drew nothing. */
    r_reset();
    check(!r_tex_alive(&a) && !r_tex_alive(&b), "a handle is dead after the table was emptied");

    /* 3. freeing a stale handle must not delete the atlas that took its slot */
    check(load(&c, buf, sizeof buf) == 0, "the table loads again after the reset");
    check(r_tex_alive(&c), "the new atlas is alive");
    {
        int del_before = del_calls;
        r_tex_free(&a);                                    /* a: stale, its slot now belongs to c */
        check(del_calls == del_before, "freeing a stale handle deletes nothing");
        check(r_tex_alive(&c), "and the newer atlas in that slot survives");
        check(a.id == -1, "the stale handle is dropped");
    }

    /* 4. a live handle still frees its own texture */
    {
        int del_before = del_calls;
        r_tex_free(&c);
        check(del_calls == del_before + 1, "freeing a live handle deletes its texture");
        check(!r_tex_alive(&c), "and the handle goes dead");
    }

    /* 5. a failed load leaves a dead handle, never a slot number (0 is a real slot) */
    {
        uint8_t bad[16] = { 0 };
        tex_t d;
        check(load(&d, bad, sizeof bad) < 0, "garbage is rejected");
        check(d.id < 0, "and the handle is dead, not slot 0");
        check(!r_tex_alive(&d), "so it never draws somebody else's atlas");
    }

    /* 6. the upscale must sample the framebuffer's own texture. It used to sample whatever atlas the
     *    last flush left on unit 0, so PIXEL mode showed a magnified slice of a sprite sheet (a wall of
     *    tiny character faces and smeared text) instead of the 426x240 picture. */
    {
        fbo_make(VIEW_W, VIEW_H);
        in_fbo = 1;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 4242);              /* a "sprite sheet" left bound by the world pass */
        rgl_present();
        check(fbo_tex != 0 && bound_on[0] == fbo_tex, "the upscale samples the framebuffer texture");
        in_fbo = 0;
    }

    printf("texture handle lifecycle %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
