#include <stdint.h>
/* Resource loading (FPTX/FPPL/FPS2/FPL2) with bounds validation, and the
 * Fusion-style animation player. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../fp.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int tex_load_file(tex_t *t, const char *name) {
    size_t sz; char fn[64];
    snprintf(fn, sizeof fn, "%s.tex", name);
    void *d = plat_load_file(fn, &sz);
    if (!d) { plat_log("tex %s: missing\n", fn); return -1; }
    uint64_t tu = plat_time_us();
    int r = r_tex_load(t, d, sz);
    LP.upload_us += plat_time_us() - tu; LP.ntex++; LP.tex_bytes += sz;
    free(d);
    if (r < 0) plat_log("tex %s: invalid\n", fn);
    return r;
}

/* like tex_load_file, but a texture whose texels are all 0 (fully transparent) is not uploaded */
int tex_load_file_skip_empty(tex_t *t, const char *name) {
    size_t sz; char fn[64];
    snprintf(fn, sizeof fn, "%s.tex", name);
    uint8_t *d = plat_load_file(fn, &sz);
    if (!d) return -1;
    int empty = sz > 16;
    for (size_t i = 16; i < sz && empty; i++) if (d[i]) empty = 0;
    int r = 0;
    if (empty) { memset(t, 0, sizeof *t); t->id = -1; LP.nskip++; LP.skip_bytes += sz; }
    else {
        uint64_t tu = plat_time_us();
        r = r_tex_load(t, d, sz);
        LP.upload_us += plat_time_us() - tu; LP.ntex++; LP.tex_bytes += sz;
        if (r < 0) plat_log("tex %s: invalid\n", fn);
    }
    free(d);
    return r;
}

int pal_load_file(const char *name) {
    size_t sz;
    void *d = plat_load_file(name, &sz);
    if (!d) return -1;
    int r = r_pal_load(d, sz);
    free(d);
    return r;
}

static unsigned bank_gen = 1;                       /* bank loads so far (bank_anim cache, below) */
int bank_load(bank_t *b, const char *name) {
    bank_gen++;                                       /* invalidates the bank_anim cache */
    char fn[64]; size_t sz;
    /* Dead handles, not 0: 0 is a real texture-table slot, and a failed bank used to draw every one of its
     * sprites with whatever atlas happened to sit in slot 0 (stretched garbage or nothing at all). */
    memset(b, 0, sizeof *b); b->tex.id = -1; b->tex2.id = -1;
    if (tex_load_file(&b->tex, name) < 0) return -1;
    snprintf(fn, sizeof fn, "%s.spr", name);
    uint8_t *d = plat_load_file(fn, &sz);
    if (!d) { plat_log("bank %s: no %s\n", name, fn); if (b->tex.id >= 0) r_tex_free(&b->tex); b->tex.id = -1; return -1; }
    if (sz < 12 || memcmp(d, "FPS2", 4)) goto bad;
    b->nframes = rd16(d + 4); b->nanims = rd16(d + 6);
    size_t need = 12 + (size_t)b->nframes * 12 + (size_t)b->nanims * 24;
    if (need > sz || !b->nframes || !b->nanims) goto bad;
    b->frames = malloc(sizeof(frame_t) * b->nframes);
    b->anims = malloc(sizeof(anim_t) * b->nanims);
    const uint8_t *p = d + 12;
    for (int i = 0; i < b->nframes; i++, p += 12) {
        frame_t *f = &b->frames[i];
        f->u = rd16(p); f->v = rd16(p + 2); f->w = rd16(p + 4); f->h = rd16(p + 6);
        f->hx = (int16_t)rd16(p + 8); f->hy = (int16_t)rd16(p + 10);
        if (f->v & 0x8000) {                       /* second atlas */
            if (b->tex2.id < 0 && b->tex2.w == 0) { char n2[64]; snprintf(n2, sizeof n2, "%s_2", name); if (tex_load_file(&b->tex2, n2) < 0) { b->tex2.id = -1; goto bad; } }
            if (f->u + f->w > b->tex2.w || (f->v & 0x7fff) + f->h > b->tex2.h) goto bad;
        } else if (f->u + f->w > b->tex.w || f->v + f->h > b->tex.h) {
            plat_log("bank %s: frame %d (%d,%d %dx%d) outside its %dx%d atlas\n", name, i, f->u, f->v, f->w, f->h, b->tex.w, b->tex.h);
            goto bad;
        }
    }
    int total = 0;
    for (int i = 0; i < b->nanims; i++, p += 24) {
        anim_t *a = &b->anims[i];
        memcpy(a->name, p, 16); a->name[15] = 0;
        a->first = rd16(p + 16); a->count = rd16(p + 18);
        a->speed = p[20]; a->back_to = p[21]; a->loop = (int16_t)rd16(p + 22);
        if (!a->count || a->back_to >= a->count) goto bad;
        if (a->first + a->count > total) total = a->first + a->count;
    }
    if (need + (size_t)total * 2 > sz) goto bad;
    b->seq = malloc(2 * total);
    for (int i = 0; i < total; i++) {
        b->seq[i] = rd16(p + 2 * i);
        if (b->seq[i] >= b->nframes) goto bad;
    }
    {   /* optional collision masks after the sequence table */
        size_t off = need + (size_t)total * 2, msz = 0;
        for (int i = 0; i < b->nframes; i++) msz += (size_t)((b->frames[i].w + 7) / 8) * b->frames[i].h;
        if (off + 4 + (size_t)b->nframes * 4 <= sz && !memcmp(d + off, "ACT1", 4)) {
            b->act = malloc(4 * (size_t)b->nframes);
            for (int i = 0; i < 2 * b->nframes; i++) b->act[i] = (int16_t)rd16(d + off + 4 + 2 * i);
            off += 4 + (size_t)b->nframes * 4;
        }
        if (off + 4 + msz <= sz && !memcmp(d + off, "MSK1", 4)) {
            b->mbits = malloc(msz); b->moff = malloc(sizeof(uint32_t) * b->nframes);
            memcpy(b->mbits, d + off + 4, msz);
            uint32_t o = 0;
            for (int i = 0; i < b->nframes; i++) { b->moff[i] = o; o += (uint32_t)((b->frames[i].w + 7) / 8) * b->frames[i].h; }
            off += 4 + msz;
        }
        if (off + 4 + (size_t)b->nframes * 8 <= sz && !memcmp(d + off, "BOX1", 4)) {   /* session 24: the uncropped image boxes (is_near_border) */
            b->fbox = malloc(8 * (size_t)b->nframes);
            for (int i = 0; i < 4 * b->nframes; i++) b->fbox[i] = (int16_t)rd16(d + off + 4 + 2 * i);
        }
    }
    free(d);
    return 0;
bad:
    /* A broken bank used to keep its uploaded atlas forever: the caller zeroed the bank, the slot was never
     * freed, and after enough attempts the texture table filled up and NEW banks (a boss, an enemy) loaded
     * nothing at all - sprites that "disappear" after a while of play. Give the memory back. */
    plat_log("bank %s: invalid file\n", name);
    if (b->tex.id >= 0) r_tex_free(&b->tex);
    if (b->tex2.id >= 0) r_tex_free(&b->tex2);
    free(b->frames); free(b->anims); free(b->seq); free(b->act); free(b->mbits); free(b->moff); free(b->fbox);
    memset(b, 0, sizeof *b); b->tex.id = -1; b->tex2.id = -1;
    free(d);
    return -1;
}

int frame_mask_px(const bank_t *b, const frame_t *f, int x, int y) {
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return 0;
    if (!b->mbits) return 1;                         /* no masks: whole image box */
    const uint8_t *m = b->mbits + b->moff[f - b->frames] + (size_t)y * ((f->w + 7) / 8);
    return (m[x >> 3] >> (7 - (x & 7))) & 1;
}

const tex_t *bank_frame_tex(const bank_t *b, const frame_t *f, int *v) {
    if (f->v & 0x8000) { *v = f->v & 0x7fff; return &b->tex2; }
    *v = f->v; return &b->tex;
}

/* name -> index cache (session 18 perf: objects set their animation by name every frame; FN levels have 500-700
 * animations per bank). Keyed by bank and name text; a hit is re-checked against the bank's own name, a miss (-1) is only
 * trusted for the same bank load (bank_gen). Same results as the plain search. */
static struct { const bank_t *b; unsigned gen; short idx; char name[26]; } ACACHE[512];
static int bank_anim_scan(const bank_t *b, const char *name) {
    for (int i = 0; i < b->nanims; i++)
        if (!strcmp(b->anims[i].name, name)) return i;
    return -1;
}
int bank_anim(const bank_t *b, const char *name) {
    if (!b->nanims) return -1;
    unsigned h = 2166136261u; size_t n = 0;
    for (const char *c = name; *c; c++, n++) h = (h ^ (unsigned char)*c) * 16777619u;
    if (n >= sizeof ACACHE[0].name) return bank_anim_scan(b, name);
    h = (h ^ (unsigned)(uintptr_t)b) * 16777619u;
    unsigned k = (h ^ (h >> 9)) & 511;
    if (ACACHE[k].b == b && ACACHE[k].gen == bank_gen && !strcmp(ACACHE[k].name, name)) {
        int i = ACACHE[k].idx;
        if (i < 0 || (i < b->nanims && !strcmp(b->anims[i].name, name))) return i;
    }
    int i = bank_anim_scan(b, name);
    ACACHE[k].b = b; ACACHE[k].gen = bank_gen; ACACHE[k].idx = (short)i; memcpy(ACACHE[k].name, name, n + 1);
    return i;
}

/* RE (Active::force_animation): an animation change/restart resets the frame but KEEPS the speed counter */
void anim_restart(animator_t *a) {
    a->frame = 0; a->done = 0;
    a->loops = a->b ? a->b->anims[a->anim].loop : -1;
}

void anim_set(animator_t *a, const bank_t *b, int anim) {
    if (anim < 0) anim = 0;
    if (a->b == b && a->anim == anim) return;
    a->b = b; a->anim = anim;
    anim_restart(a);
}

void anim_tick(animator_t *a, int speed_override) {
    if (!a->b || a->done) return;
    const anim_t *an = &a->b->anims[a->anim];
    int spd = speed_override >= 0 ? speed_override : an->speed;
    /* RE (Active::update): counter += speed; a frame advances only when the counter goes ABOVE 100 */
    a->acc += spd;
    if (a->acc <= 100) return;
    while (a->acc > 100) {
        a->acc -= 100;
        if (++a->frame >= an->count) {
            if (an->loop < 0) a->frame = an->back_to;
            else if (a->loops > 1) { a->loops--; a->frame = an->back_to; }
            else { a->frame = an->count - 1; a->done = 1; break; }
        }
    }
}

const frame_t *anim_frame(const animator_t *a) {
    const anim_t *an = &a->b->anims[a->anim];
    return &a->b->frames[a->b->seq[an->first + a->frame]];
}

/* draw with hotspot at (x,y). flip mirrors (used only for single-direction objects) */
void frame_draw(const bank_t *b, const frame_t *f, float x, float y, int flip, uint32_t argb) {
    float dx = flip ? x - (f->w - f->hx) : x - f->hx;
    if (f->v & 0x8000) r_sprite(&b->tex2, f->u, f->v & 0x7fff, f->w, f->h, dx, y - f->hy, f->w, f->h, flip, argb);
    else r_sprite(&b->tex, f->u, f->v, f->w, f->h, dx, y - f->hy, f->w, f->h, flip, argb);
}

void frame_draw_scaled(const bank_t *b, const frame_t *f, float x, float y, float w, float h, uint32_t argb) {   /* top-left x, y, drawn w x h */
    if (f->v & 0x8000) r_sprite(&b->tex2, f->u, f->v & 0x7fff, f->w, f->h, x, y, w, h, 0, argb);
    else r_sprite(&b->tex, f->u, f->v, f->w, f->h, x, y, w, h, 0, argb);
}
/* rotation around the hotspot (the original rotates Actives around their hot spot) */
void frame_draw_rot(const bank_t *b, const frame_t *f, float x, float y, float angle_deg, int flip, uint32_t argb) {
    if (angle_deg == 0 && !flip && r_get_scale() == 1.f && r_get_xscale() == 1.f) { frame_draw(b, f, x, y, 0, argb); return; }
    if (f->v & 0x8000) r_sprite_rot(&b->tex2, f->u, f->v & 0x7fff, f->w, f->h, x, y, f->hx, f->hy, angle_deg, flip, argb);
    else r_sprite_rot(&b->tex, f->u, f->v, f->w, f->h, x, y, f->hx, f->hy, angle_deg, flip, argb);
}

/* ------------------------------------------------------------------ FPL2 */
int level_load(level_t *l, const char *name) {
    char fn[64]; size_t sz;
    memset(l, 0, sizeof *l);
    snprintf(fn, sizeof fn, "%s.lv2", name);
    uint8_t *d = plat_load_file(fn, &sz);
    if (!d) return -1;
    if (sz < 32 || memcmp(d, "FPL2", 4)) goto bad;
    l->w = rd32(d + 4); l->h = rd32(d + 8);
    l->npieces = rd16(d + 12); l->nobj = rd16(d + 14);
    l->natlas = rd16(d + 16); l->nuniq = rd16(d + 18);
    l->tw = rd16(d + 20); l->th = rd16(d + 22);
    if (l->natlas > MAX_ATLAS || l->nuniq < 2 || l->tw != (l->w + 15) / 16 || l->th != (l->h + 15) / 16) goto bad;
    size_t off = 28;
    size_t need = off + (size_t)l->npieces * 20 + (size_t)l->nobj * 12 + (size_t)l->nuniq * 32 + (size_t)l->tw * l->th * 4;
    if (need > sz || sz - need >= 2048) {   /* the disc pads files (tools/pad_disc.py) */ plat_log("level %s: size %u expected %u\n", name, (unsigned)sz, (unsigned)need); goto bad; }
    l->pieces = malloc(sizeof(piece_t) * (l->npieces ? l->npieces : 1));
    int np = 0;
    for (int i = 0; i < l->npieces; i++, off += 20) {
        piece_t *p = &l->pieces[np++];
        p->x = (int32_t)rd32(d + off); p->y = (int32_t)rd32(d + off + 4);
        p->u = rd16(d + off + 8); p->v = rd16(d + off + 10); p->w = rd16(d + off + 12); p->h = rd16(d + off + 14);
        p->atlas = d[off + 16]; p->layer = d[off + 17]; p->col = d[off + 18]; p->pad = d[off + 19]; p->tw = p->th = 0;
        if (p->atlas >= l->natlas) goto bad;
        if (p->pad & 4) {                          /* tiled piece (Quick Backdrop, session 19): the next record holds the size */
            if (++i >= l->npieces || d[off + 20 + 19] != 0x81) goto bad;
            off += 20; p->tw = p->w; p->th = p->h; p->w = (uint16_t)rd32(d + off); p->h = (uint16_t)rd32(d + off + 4); p->pad &= ~4;
        }
    }
    l->npieces = np;
    l->objs = malloc(sizeof(lvobj_t) * (l->nobj ? l->nobj : 1));
    for (int i = 0; i < l->nobj; i++, off += 12) {
        l->objs[i].type = rd16(d + off); l->objs[i].anim = rd16(d + off + 2);
        l->objs[i].x = (int32_t)rd32(d + off + 4); l->objs[i].y = (int32_t)rd32(d + off + 8);
    }
    /* tiles + maps stay in the loaded blob (little-endian u16, 2-aligned offsets) */
    if (off & 1) goto bad;
    l->tiles = (const uint16_t *)(d + off); off += (size_t)l->nuniq * 32;
    l->smap = (const uint16_t *)(d + off); off += (size_t)l->tw * l->th * 2;
    l->pmap = (const uint16_t *)(d + off);
    for (int i = 0; i < l->tw * l->th; i++)
        if (l->smap[i] >= l->nuniq || l->pmap[i] >= l->nuniq) goto bad;
    l->blob = d;
    for (int i = 0; i < l->natlas; i++) {
        snprintf(fn, sizeof fn, "%s_p%d", name, i);
        if (tex_load_file(&l->atlas[i], fn) < 0) return -1;
    }
    return 0;
bad:
    plat_log("level %s: invalid\n", name);
    free(d);
    return -1;
}
