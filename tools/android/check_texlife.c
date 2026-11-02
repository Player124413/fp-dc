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
#include <stdarg.h>
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
/* the picture the sprite dump reads back: a synthetic 426x240 view, top-down (row 0 = the top) */
#define FAKE_W 426
#define FAKE_H 240
static uint8_t fake_screen[FAKE_W * FAKE_H * 4];
static int read_calls;
static const uint8_t zero4[4];
int glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *buf) {
    (void)fmt; (void)type;
    read_calls++;
    uint8_t *d = buf;
    for (int r = 0; r < h; r++) {                     /* the target's row 0 is the BOTTOM of the picture */
        int row = FAKE_H - 1 - (y + r);
        for (int c = 0; c < w; c++) {
            int col = x + c;
            const uint8_t *src = (row >= 0 && row < FAKE_H && col >= 0 && col < FAKE_W)
                                 ? &fake_screen[((size_t)row * FAKE_W + col) * 4] : zero4;
            memcpy(d + ((size_t)r * w + c) * 4, src, 4);
        }
    }
    return 0;
}
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

/* the log is text here, not just a count: r_sprite_report() writes what it finds under a frame and the
 * checks below read it back (a "part of the sprite is missing" report has to be answerable from the log) */
static char logbuf[1 << 16];
static size_t loglen;
static int log_lines;
void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(logbuf + loglen, sizeof logbuf - loglen, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t room = sizeof logbuf - loglen - 1;
        loglen += (size_t)n < room ? (size_t)n : room;
    }
    log_lines++;
}
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


    /* 7. a frame whose rect runs off its atlas is drawn WHOLE. The Dreamcast backend never cuts the quad:
     *    the PVR clamps the sample to the edge of the texture. This backend used to truncate it, so the
     *    same data lost a strip of the picture on the phone and was whole on the Dreamcast -- the
     *    "she is not drawn all the way" report. The quad must keep the frame's size and sample past 1.0
     *    (GL_CLAMP_TO_EDGE repeats the edge texel, exactly like the hardware). */
    {
        tex_t t;
        fptx_make(buf, 8, 8, 1);                 /* an 8x8 sheet */
        check(load(&t, buf, sizeof buf) == 0, "an 8x8 sheet loads for the overrun test");
        r_begin();
        r_sprite(&t, 6, 6, 8, 8, 100, 100, 8, 8, 0, 0xffffffff);   /* rect reaches (14,14) of an 8x8 sheet */
        check(nquad == 1, "the overrunning frame is drawn (not dropped)");
        if (nquad == 1) {
            check(vbuf[1].x - vbuf[0].x == 8.0f && vbuf[3].y - vbuf[0].y == 8.0f,
                  "it keeps the frame's own size, nothing is cut off");
            check(vbuf[1].u > 1.0f && vbuf[3].v > 1.0f,
                  "and samples past the edge, which GL_CLAMP_TO_EDGE repeats like the PVR");
        }
        check(strstr(logbuf, "runs past its") != NULL, "the overrun is still written into the log");
        r_tex_free(&t);
    }

    /* 8. r_sprite_report() says what the sheet really holds under a frame: texels that draw, texels whose
     *    palette entry is transparent (invisible however right the geometry is) and empty texels. Without
     *    the difference, "the sprite is not drawn all the way" cannot be told from "the rect is wrong". */
    {
        tex_t t;
        uint8_t sheet[16 + 8 * 8];
        uint8_t pal[8 + 512];
        memset(pal, 0, sizeof pal);
        memcpy(pal, "FPPL", 4);
        {   /* group 0: entry 0 is the transparent texel, 1 is opaque, 2 has no alpha bit (invisible) */
            uint16_t hdr[2] = { 0, 3 };
            memcpy(pal + 4, hdr, 4);
            uint16_t e0 = 0x0000, e1 = 0x801f, e2 = 0x001f;
            memcpy(pal + 8, &e0, 2);
            memcpy(pal + 10, &e1, 2);
            memcpy(pal + 12, &e2, 2);
        }
        fptx_make(sheet, 8, 8, 1);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                sheet[16 + y * 8 + x] = (uint8_t)(y < 2 ? 1 : y < 4 ? 2 : 0);
        check(load(&t, sheet, sizeof sheet) == 0, "the sheet with three kinds of texel loads");
        check(r_pal_load(pal, sizeof pal) == 0, "and its palette loads");
        loglen = 0; logbuf[0] = 0;
        check(r_sprite_report(&t, 0, 0, 8, 8) == 0, "the frame is read out of the atlas");
        check(strstr(logbuf, "64 texels: 16 draw, 16 have an index but are invisible in that palette, 32 empty") != NULL,
              "the three kinds of texel are counted");
        check(strstr(logbuf, "|########|") != NULL, "the rows that draw are marked '#'");
        check(strstr(logbuf, "|--------|") != NULL, "the rows the palette hides are marked '-'");
        check(strstr(logbuf, "|........|") != NULL, "the empty rows are marked '.'");
        r_tex_free(&t);
    }

    /* 9. the sprite dump answers the other half of "she is not drawn all the way": not what the sheet holds
     *    but what the picture got. It reads the target back before her quad, right after it and at the end of
     *    the frame; the three masks have to point at the texels the atlas draws and the picture does not
     *    (her own quad left them empty) and at the ones something repainted after her. */
    {
        tex_t t;
        uint8_t pal[8 + 512];
        memset(pal, 0, sizeof pal);
        memcpy(pal, "FPPL", 4);
        {   uint16_t hdr[2] = { 0, 1 }, e1 = 0x801f;
            memcpy(pal + 4, hdr, 4);
            memcpy(pal + 10, &e1, 2);                 /* entry 1 opaque, everything else transparent */
        }
        fptx_make(buf, 8, 8, 1);                       /* an 8x8 sheet that draws in every texel */
        check(load(&t, buf, sizeof buf) == 0, "a sheet for the sprite dump loads");
        r_pal_load(pal, sizeof pal);
        memset(fake_screen, 0, sizeof fake_screen);    /* the picture before her: nothing drawn */
        win_w = VIEW_W; win_h = VIEW_H;                /* a window the size of the view: the dump's
                                                        * SHARP-path scale/offset is the identity, so the
                                                        * read-back coordinates are the view's own */
        loglen = 0; logbuf[0] = 0;
        read_calls = 0;
        r_begin();
        r_dump_begin(&t, 0, 0, 8, 8, 100, 100, 0, 0, 0, 0, "idle", 0);
        check(read_calls == 1, "the dump reads the target back before her quads");
        check(strstr(logbuf, "sprite dump: anim \"idle\" frame 0, rect (0,0 8x8)") != NULL,
              "and names the frame it is about");
        /* her quads reach only half of the box: the rest of the frame stayed empty on screen */
        for (int y = 100; y < 108; y++)
            for (int x = 100; x < 104; x++) memset(&fake_screen[((size_t)y * FAKE_W + x) * 4], 0xff, 4);
        r_dump_drawn();
        check(read_calls == 2, "and again right after them");
        check(strstr(logbuf, "sprite dump: atlas  |########|") != NULL, "the atlas' own mask is printed next to it");
        check(strstr(logbuf, "sprite dump: drawn  |####....|") != NULL,
              "the texels the picture did not get stay '.', the four it got are '#'");
        /* then something is drawn over her: rows 0..1 of her box change after her quads */
        r_sprite(&t, 0, 0, 8, 8, 100, 100, 8, 8, 0, 0xffffffff);
        check(strstr(logbuf, "after her: atlas") != NULL, "a sprite drawn over her is named");
        for (int y = 100; y < 102; y++)
            for (int x = 100; x < 108; x++) memset(&fake_screen[((size_t)y * FAKE_W + x) * 4], 0x11, 4);
        r_dump_frame_end();
        check(read_calls == 3, "and once more at the end of the frame");
        check(strstr(logbuf, "sprite dump: over   |++++++++|") != NULL, "the repainted rows are marked '+'");
        check(strstr(logbuf, "sprite dump: over   |........|") != NULL, "and the untouched ones stay '.'");
        r_tex_free(&t);
    }

    /* 10. both triangles of a quad have to wind the SAME way. The batch's index buffer splits every quad
     *     as (0,1,2) + (0,2,3), i.e. TL,TR,BR + TL,BR,BL. The rotated path used to hand it TL,TR,BL,BR, so
     *     the two triangles wound in opposite directions and back-face culling dropped one of them: the
     *     sprite lost a clean half along the quad's diagonal and the background showed through the
     *     straight edge -- "she is cut off, and only on the phone". push_quad() always had it right, which
     *     is why only sprites drawn with a rotation (the heroine's lean, most enemies) were cut. */
    {
        tex_t t;
        fptx_make(buf, 8, 8, 1);
        check(load(&t, buf, sizeof buf) == 0, "a sheet for the winding test loads");
        /* signed area of a triangle (screen y points down, so one sign is "front" for both) */
        #define AREA2(p, q, r) (((q).x - (p).x) * ((r).y - (p).y) - ((q).y - (p).y) * ((r).x - (p).x))
        r_begin();
        r_sprite(&t, 0, 0, 8, 8, 100, 100, 8, 8, 0, 0xffffffff);
        check(nquad == 1, "an upright sprite pushes one quad");
        if (nquad == 1) {
            float s1 = AREA2(vbuf[0], vbuf[1], vbuf[2]), s2 = AREA2(vbuf[0], vbuf[2], vbuf[3]);
            check((s1 > 0) == (s2 > 0) && s1 != 0 && s2 != 0, "its two triangles wind the same way");
        }
        r_begin();
        r_sprite_rot(&t, 0, 0, 8, 8, 100, 100, 4, 4, 20, 0, 0xffffffff);
        check(nquad == 1, "a rotated sprite pushes one quad");
        if (nquad == 1) {
            float s1 = AREA2(vbuf[0], vbuf[1], vbuf[2]), s2 = AREA2(vbuf[0], vbuf[2], vbuf[3]);
            check((s1 > 0) == (s2 > 0) && s1 != 0 && s2 != 0,
                  "and so do its, or culling cuts the sprite in half");
            check(vbuf[0].u == 0.0f && vbuf[2].u == 1.0f && vbuf[3].u == 0.0f && vbuf[1].v == 0.0f,
                  "with the frame's own texture coordinates on the same corners");
        }
        /* the same for a mirrored draw and for a draw under a scale (both only reachable through here) */
        r_begin();
        r_sprite_rot(&t, 0, 0, 8, 8, 100, 100, 4, 4, -35, 1, 0xffffffff);
        if (nquad == 1) {
            float s1 = AREA2(vbuf[0], vbuf[1], vbuf[2]), s2 = AREA2(vbuf[0], vbuf[2], vbuf[3]);
            check((s1 > 0) == (s2 > 0), "a mirrored, mirrored-and-leaning quad keeps one winding");
        }
        r_tex_free(&t);
    }

    printf("texture handle lifecycle %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
