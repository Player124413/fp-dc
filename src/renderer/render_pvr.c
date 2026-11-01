/* PowerVR2 backend (Dreamcast). All game drawing goes into the punch-through
 * list: ARGB1555 textures give 1-bit alpha which matches the pixel art.
 * Logical 426x240 is scaled x1.5 and letterboxed on the 640x480 framebuffer (D-012). */
#include <string.h>
#include <kos.h>
#include "../fp.h"

#define MAXTEX 192
#ifdef FP_VIEW_320
static float SX = 2.0f, SY = 2.0f, OFF_X = 0.0f, OFF_Y = 0.0f;
static int letterbox = 0;
void r_set_letterbox(int on) { (void)on; }
#else
/* Presentation (SV.letterbox, Options > SCREEN SIZE). The logical view stays 426x240 either way:
 *  on : x1.5 both ways -> 639x360, centred, opaque bars 60 px top and bottom (16:9 picture on a 4:3 frame);
 *  off: x1.5 across, x2 down -> 639x480 (fills the 4:3 frame; integer vertical scale). D-025. */
/* Sub-pixel offsets (session 16): logical coordinates are integers, so with x1.5 / x2 every quad edge lands on a
 * whole or half screen pixel. On the hardware the PVR then samples exactly on a texture's first texel row/column edge
 * and can take the texel before it (the atlas neighbour / transparent padding): a 1 px gap along tile tops (vertical
 * offset was 0; the old horizontal 0.5 only moved even-x edges off the sample points). A 0.25 px offset on both axes
 * keeps every edge between sample points for either sampling convention (pixel corners or centres); shared tile edges
 * stay identical, so tiling stays watertight. */
static float SX = 1.5f, SY = 1.5f, OFF_X = 0.25f, OFF_Y = 60.25f;
static int letterbox = 1;
void r_set_letterbox(int on) {
    letterbox = on ? 1 : 0;
    SY = on ? 1.5f : 2.0f; OFF_Y = on ? 60.25f : 0.25f;
}
#endif

static struct { pvr_ptr_t ptr; pvr_poly_hdr_t hdr, hdr_tr, hdr_ia, hdr_ib, hdr_add; int w, h; } texs[MAXTEX];
/* hit flash (Chowdren invert shader 21): two translucent passes, A = white silhouette (texel alpha, colour
 * from the specular offset), B = destination x (1 - texel colour); transparent texels are black (index 0) */
static int invert_on; static uint32_t cur_oargb;
void r_set_invert(int on) { invert_on = on; }
/* additive blend (Chowdren shader 0x20, e.g. the elemental shields): replayed in the translucent list */
static int add_on, add_pass;   /* add_pass: replaying an additive sprite */
void r_set_additive(int on) { add_on = on; }
/* Fades: r_fade() queues a translucent black quad; everything drawn after it in the same frame is
 * deferred and replayed in the translucent list on top of the fade (PVR lists are submitted once). */
typedef struct { int kind; const tex_t *t; int u, v, uw, vh, flip; float a[8]; uint32_t argb; int inv, add; float z; } dcmd_t;
#define NDQ 640
static dcmd_t dq[NDQ]; static int ndq, overlay, in_tr;
static pvr_poly_hdr_t col_hdr_tr, col_hdr_mul, col_hdr_add;   /* col_hdr_add: additive colour rects (session 26: the view tints) */
static int ntex;
static pvr_poly_hdr_t col_hdr;
static int cur_hdr;           /* -2 none, -1 colour, >=0 texture */
static float zc;
uint32_t r_stat_polys;

int r_init(void) {
    pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        .vertex_buf_size = 128 * 1024,     /* ~150-300 polys per frame (perf log): 128 KB x 2 is ample; frees VRAM for textures */
        .dma_enabled = 0,
        .fsaa_enabled = 0,
        .autosort_disabled = 0,
        .opb_overflow_count = 3,
    };
    if (pvr_init(&params) < 0) return -1;
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_PT_POLY);
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&col_hdr, &cxt);
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&col_hdr_tr, &cxt);
    cxt.blend.src = PVR_BLEND_SRCALPHA; cxt.blend.dst = PVR_BLEND_ONE;          /* additive: dst + src * a */
    pvr_poly_compile(&col_hdr_add, &cxt);
    cxt.blend.src = PVR_BLEND_ZERO; cxt.blend.dst = PVR_BLEND_INVDESTCOLOR;   /* dst factor INVDESTCOLOR = 1 - source colour */
    pvr_poly_compile(&col_hdr_mul, &cxt);
    return 0;
}


int r_tex_load(tex_t *t, const void *data, size_t size) {
    const uint8_t *d = data;
    uint16_t hdr[6];
    int slot = 0;
    while (slot < ntex && texs[slot].ptr) slot++;                            /* reuse freed slots */
    if (size < 16 || memcmp(d, "FPTX", 4) || slot >= MAXTEX) return -1;
    memcpy(hdr, d + 4, 12);
    int w = hdr[0], h = hdr[1], fmt = hdr[2], grp = hdr[5];
    size_t bytes = fmt == 3 ? (size_t)w * h / 2 : fmt == 4 ? (size_t)w * h * 2 : fmt >= 1 ? (size_t)w * h : (size_t)w * h * 2;   /* fmt 1: PAL8 linear, 2: PAL8 pre-twiddled (tools/twiddle_disc.py), 3: PAL4 linear, 4: ARGB4444 linear */
    if (fmt > 4 || (fmt >= 1 && fmt <= 2 && grp > 3) || (fmt == 3 && grp > 63) || w < 8 || h < 8 || (w & (w - 1)) || (h & (h - 1)) || bytes + 16 > size) return -1;
    pvr_ptr_t p = pvr_mem_malloc(bytes);
    if (!p) { plat_log("VRAM exhausted (%dx%d)\n", w, h); return -1; }
    int txfmt;
    if (fmt == 3) {       /* 4-bit palettized (parallax): KOS twiddles while uploading; bank = 16-entry palette block */
        pvr_txr_load_ex(d + 16, p, w, h, PVR_TXRLOAD_4BPP);
        txfmt = PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(grp);
    } else if (fmt == 2) {       /* already in VRAM order: straight store-queue copy */
        pvr_txr_load(d + 16, p, w * h);
        txfmt = PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(grp);
    } else if (fmt == 1) { /* 8-bit palettized, linear: KOS twiddles every texel while uploading */
        pvr_txr_load_ex(d + 16, p, w, h, PVR_TXRLOAD_8BPP);
        txfmt = PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL(grp);
    } else if (fmt == 4) {  /* ARGB4444 (session 19: Sky Battalion's mist, per-pixel alpha; drawn with vertex alpha < 255 = TR list) */
        pvr_txr_load(d + 16, p, w * h * 2);
        txfmt = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED;
    } else {
        pvr_txr_load(d + 16, p, w * h * 2);
        txfmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED;
    }
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, txfmt, w, h, p, PVR_FILTER_NONE);
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.txr.env = PVR_TXRENV_MODULATE;
    pvr_poly_compile(&texs[slot].hdr, &cxt);
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, txfmt, w, h, p, PVR_FILTER_NONE);
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.gen.culling = PVR_CULLING_NONE;
    cxt.txr.env = PVR_TXRENV_MODULATEALPHA;   /* session 13: alpha = texture x vertex (MODULATE ignored the vertex alpha, so
                                               * every sprite drawn with alpha < 255 - translucent UI layers, results panels,
                                               * fades - was opaque on the DC; same result as before for alpha 255) */
    pvr_poly_compile(&texs[slot].hdr_tr, &cxt);
    cxt.blend.src = PVR_BLEND_SRCALPHA; cxt.blend.dst = PVR_BLEND_ONE;          /* additive */
    pvr_poly_compile(&texs[slot].hdr_add, &cxt);
    cxt.gen.specular = PVR_SPECULAR_ENABLE;
    cxt.blend.src = PVR_BLEND_SRCALPHA; cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&texs[slot].hdr_ia, &cxt);
    cxt.gen.specular = PVR_SPECULAR_DISABLE;
    cxt.blend.src = PVR_BLEND_ZERO; cxt.blend.dst = PVR_BLEND_INVDESTCOLOR;   /* as a destination factor: 1 - source colour */
    pvr_poly_compile(&texs[slot].hdr_ib, &cxt);
    texs[slot].ptr = p; texs[slot].w = w; texs[slot].h = h;
    t->id = slot; if (slot == ntex) ntex++; t->w = w; t->h = h; t->srcw = hdr[3]; t->srch = hdr[4];
    return 0;
}

/* free one texture (front-end screen banks are swapped) */
void r_tex_free(tex_t *t) {
    if (!r_tex_alive(t)) { t->id = -1; return; }
    pvr_wait_ready(); pvr_mem_free(texs[t->id].ptr); texs[t->id].ptr = NULL; t->id = -1;
}
/* the PVR table is only emptied through r_reset on a level change, and nothing keeps drawing across it */
int r_tex_alive(const tex_t *t) {
    return t && t->id >= 0 && t->id < ntex && texs[t->id].ptr != NULL;
}

/* drop every texture (level change): VRAM texture memory is reset as a whole */
void r_reset(void) { pvr_wait_ready(); pvr_mem_reset(); for (int i = 0; i < ntex; i++) texs[i].ptr = NULL; ntex = 0; }

int r_pal_load(const void *data, size_t size) {
    const uint8_t *d = data;
    uint16_t g, pal[256];
    if (size < 8 + 512 || memcmp(d, "FPPL", 4)) return -1;
    memcpy(&g, d + 4, 2);
    if (g > 3) return -1;
    memcpy(pal, d + 8, 512);
    pvr_set_pal_format(PVR_PAL_ARGB1555);
    for (int i = 0; i < 256; i++) pvr_set_pal_entry(g * 256 + i, pal[i]);
    return 0;
}

void r_begin(void) {
    PROF(PS_WAIT, pvr_wait_ready());
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_PT_POLY);
    cur_hdr = -2;
    zc = 1.0f;
    r_stat_polys = 0;
    ndq = 0; overlay = 0; in_tr = 0;
}

static void replay(void);
static void bars(void);
void r_end(void) {
    uint64_t t_sub = plat_time_us();
    bars();
    pvr_list_finish();
    if (ndq) {
        pvr_list_begin(PVR_LIST_TR_POLY);
        in_tr = 1; cur_hdr = -2;
        replay();
        bars();
        pvr_list_finish();
    }
    add_pass = 0;
    pvr_scene_finish();
    { uint32_t d = (uint32_t)(plat_time_us() - t_sub); prof_acc[PS_SUBMIT] += d; if (d > prof_max[PS_SUBMIT]) prof_max[PS_SUBMIT] = d; }
}

void r_fade(float alpha) {
    if (alpha <= 0) return;
    if (alpha > 1) alpha = 1;
    if (ndq < NDQ) { dcmd_t *c = &dq[ndq++]; c->kind = 2; c->argb = ((uint32_t)(alpha * 255) << 24); c->z = zc; zc += 0.0001f; }
    overlay = 1;
}
static int inv_pass;          /* 0 normal, 1 / 2 invert passes (replay) */
#define HDR(i) (in_tr ? (inv_pass == 1 ? &texs[i].hdr_ia : inv_pass == 2 ? &texs[i].hdr_ib : add_pass ? &texs[i].hdr_add : &texs[i].hdr_tr) : &texs[i].hdr)

static inline void quad(float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1, uint32_t argb) {
    pvr_vertex_t v;
    zc += 0.0001f;
    v.z = zc; v.argb = argb; v.oargb = cur_oargb;
    v.flags = PVR_CMD_VERTEX;
    v.x = x0; v.y = y0; v.u = u0; v.v = v0; pvr_prim(&v, sizeof v);
    v.x = x1; v.y = y0; v.u = u1; v.v = v0; pvr_prim(&v, sizeof v);
    v.x = x0; v.y = y1; v.u = u0; v.v = v1; pvr_prim(&v, sizeof v);
    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x1; v.y = y1; v.u = u1; v.v = v1; pvr_prim(&v, sizeof v);
    r_stat_polys++;
}

static void bars(void) {          /* letterbox: opaque black over anything drawn outside the 426x240 view */
    if (!letterbox) return;
    if (cur_hdr != -1) { pvr_prim(in_tr ? &col_hdr_tr : &col_hdr, sizeof col_hdr); cur_hdr = -1; }
    zc += 1.0f;                    /* above every sprite of this list */
    quad(0, 0, 640, 60, 0, 0, 0, 0, 0xff000000);
    quad(0, 60 + 240 * SY, 640, 480, 0, 0, 0, 0, 0xff000000);
}

void r_sprite(const tex_t *t, int u, int v, int uw, int vh, float x, float y, float w, float h, int flip, uint32_t argb) {
    if (t->id < 0 || t->id >= ntex) return;
    if ((overlay || invert_on || add_on || (argb >> 24) < 0xff) && !in_tr) {   /* translucent / additive / flash: TR list */
        if (ndq < NDQ) { dcmd_t *c = &dq[ndq++]; *c = (dcmd_t){ 0, t, u, v, uw, vh, flip, { x, y, w, h }, argb, invert_on, add_on, zc }; zc += 0.0001f; }
        return;
    }
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    if (cur_hdr != t->id) { pvr_prim(HDR(t->id), sizeof(pvr_poly_hdr_t)); cur_hdr = t->id; }
    float iw = 1.0f / texs[t->id].w, ih = 1.0f / texs[t->id].h;
    float u0 = u * iw, u1 = (u + uw) * iw, v0 = v * ih, v1 = (v + vh) * ih;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    quad(x * SX + OFF_X, y * SY + OFF_Y, (x + w) * SX + OFF_X, (y + h) * SY + OFF_Y, u0, v0, u1, v1, argb);
}

/* rotated sprite: texel (hx,hy) of the rect lands on screen (px,py); angle in degrees CCW
 * (Fusion/Chowdren convention: Active::set_angle rotates around the hotspot) */
static float rscale = 1.f;
void r_set_scale(float s) { rscale = s; }
float r_get_scale(void) { return rscale; }
static float rxscale = 1.f;   /* extra x scale (Active::set_x_scale, e.g. the Panther's laser) */
void r_set_xscale(float s) { rxscale = s; }
float r_get_xscale(void) { return rxscale; }
void r_sprite_rot(const tex_t *t, int u, int v, int uw, int vh, float px, float py, float hx, float hy,
                  float angle, int flip, uint32_t argb) {
    if (t->id < 0 || t->id >= ntex) return;
    if ((overlay || invert_on || add_on || (argb >> 24) < 0xff) && !in_tr) {
        if (ndq < NDQ) { dcmd_t *c = &dq[ndq++]; *c = (dcmd_t){ 1, t, u, v, uw, vh, flip, { px, py, hx, hy, angle, rscale, rxscale }, argb, invert_on, add_on, zc }; zc += 0.0001f; }
        return;
    }
    if (flip) hx = uw - 1 - hx;                    /* the mirrored image's hotspot (texture u reversed below) */
    float ex = (hx > uw - hx ? hx : uw - hx) * (rxscale > 1.f ? rxscale : 1.f) * rscale, ey = (hy > vh - hy ? hy : vh - hy) * rscale;
    float r = fsqrt(ex * ex + ey * ey) + 2;          /* cull by the farthest corner from the hotspot (session 20: hotspots outside the image) */
    if (px + r < 0 || py + r < 0 || px - r >= SCREEN_W || py - r >= SCREEN_H) return;
    if (cur_hdr != t->id) { pvr_prim(HDR(t->id), sizeof(pvr_poly_hdr_t)); cur_hdr = t->id; }
    float iw = 1.0f / texs[t->id].w, ih = 1.0f / texs[t->id].h;
    float u0 = u * iw, u1 = (u + uw) * iw, v0 = v * ih, v1 = (v + vh) * ih;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    float a = angle * 0.017453293f, c = fcos(a) * rscale, sn = fsin(a) * rscale;
    float lx[4] = { -hx, uw - hx, -hx, uw - hx }, ly[4] = { -hy, -hy, vh - hy, vh - hy };
    float tu[4] = { u0, u1, u0, u1 }, tv[4] = { v0, v0, v1, v1 };
    pvr_vertex_t vt;
    zc += 0.0001f;
    vt.z = zc; vt.argb = argb; vt.oargb = cur_oargb;
    for (int i = 0; i < 4; i++) {
        vt.flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        vt.x = (px + lx[i] * rxscale * c + ly[i] * sn) * SX + OFF_X;     /* screen y points down: CCW = (c, -s; s, c) */
        vt.y = (py - lx[i] * rxscale * sn + ly[i] * c) * SY + OFF_Y;
        vt.u = tu[i]; vt.v = tv[i];
        pvr_prim(&vt, sizeof vt);
    }
    r_stat_polys++;
}

/* darkening rectangle (Chowdren blend 20, GeneralWater): dst = dst * (1 - rgb); translucent list, src x 0 + dst x (1 - src colour) */
void r_tint(float x, float y, float w, float h, uint32_t rgb) {
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H || w <= 0 || h <= 0) return;
    if (!in_tr) { if (ndq < NDQ) { dcmd_t *c = &dq[ndq++]; *c = (dcmd_t){ 4, NULL, 0, 0, 0, 0, 0, { x, y, w, h }, rgb | 0xff000000, 0, 0, zc }; zc += 0.0001f; } return; }
    pvr_prim(&col_hdr_mul, sizeof col_hdr_mul); cur_hdr = -3;
    quad(x * SX + OFF_X, y * SY + OFF_Y, (x + w) * SX + OFF_X, (y + h) * SY + OFF_Y, 0, 0, 0, 0, rgb | 0xff000000);
}
void r_rect(float x, float y, float w, float h, uint32_t argb) {
    if (x + w < 0 || y + h < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    uint32_t a = argb >> 24;
    if (a == 0) return;                                  /* as the host renderer: alpha 0 draws nothing */
    if ((overlay || a < 0xff || add_on) && !in_tr) {     /* translucent / additive rects (session 16: they were forced opaque) */
        if (ndq < NDQ) { dcmd_t *c = &dq[ndq++]; *c = (dcmd_t){ 3, NULL, 0, 0, 0, 0, 0, { x, y, w, h }, argb, 0, add_on, zc }; zc += 0.0001f; }
        return;
    }
    if (in_tr && add_pass) { if (cur_hdr != -4) { pvr_prim(&col_hdr_add, sizeof col_hdr_add); cur_hdr = -4; } }   /* (session 26) */
    else if (cur_hdr != -1) { pvr_prim(in_tr ? &col_hdr_tr : &col_hdr, sizeof col_hdr); cur_hdr = -1; }
    quad(x * SX + OFF_X, y * SY + OFF_Y, (x + w) * SX + OFF_X, (y + h) * SY + OFF_Y, 0, 0, 0, 0, in_tr ? argb : argb | 0xff000000);
}

static void replay(void) {
    float ztop = zc;
    for (int i = 0; i < ndq; i++) {
        dcmd_t *c = &dq[i];
        zc = c->z;                                     /* the depth it had when it was drawn (not the end of the frame) */
        if ((c->kind == 0 || c->kind == 1) && c->inv) {
            for (inv_pass = 1; inv_pass <= 2; inv_pass++) {
                cur_hdr = -2; zc = c->z;                                  /* force the pass header */
                cur_oargb = inv_pass == 1 ? 0x00ffffff : 0;
                uint32_t col = inv_pass == 1 ? (c->argb & 0xff000000) : (c->argb | 0x00ffffff);
                if (c->kind == 0) r_sprite(c->t, c->u, c->v, c->uw, c->vh, c->a[0], c->a[1], c->a[2], c->a[3], c->flip, col);
                else { float ks = rscale, kx = rxscale; rscale = c->a[5]; rxscale = c->a[6]; r_sprite_rot(c->t, c->u, c->v, c->uw, c->vh, c->a[0], c->a[1], c->a[2], c->a[3], c->a[4], c->flip, col); rscale = ks; rxscale = kx; }
            }
            inv_pass = 0; cur_oargb = 0; cur_hdr = -2;
            continue;
        }
        if (c->add != add_pass) { add_pass = c->add; cur_hdr = -2; }
        switch (c->kind) {
        case 0: r_sprite(c->t, c->u, c->v, c->uw, c->vh, c->a[0], c->a[1], c->a[2], c->a[3], c->flip, c->argb); break;
        case 1: { float ks = rscale, kx = rxscale; rscale = c->a[5]; rxscale = c->a[6]; r_sprite_rot(c->t, c->u, c->v, c->uw, c->vh, c->a[0], c->a[1], c->a[2], c->a[3], c->a[4], c->flip, c->argb); rscale = ks; rxscale = kx; } break;
        case 2:   /* fade: translucent black over the whole display (incl. letterbox) */
            if (cur_hdr != -1) { pvr_prim(&col_hdr_tr, sizeof col_hdr_tr); cur_hdr = -1; }
            quad(0, 0, 640, 480, 0, 0, 0, 0, c->argb);
            break;
        case 3: r_rect(c->a[0], c->a[1], c->a[2], c->a[3], c->argb); break;
        case 4: r_tint(c->a[0], c->a[1], c->a[2], c->a[3], c->argb); break;
        }
    }
    zc = ztop;                                         /* the letterbox bars go above everything again */
}
