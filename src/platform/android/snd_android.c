/* Audio backend of the Android port: AAudio output + AICA (YMZ280B) 4-bit ADPCM decoding + mixer.
 *
 * The Dreamcast's data is AICA ADPCM everywhere (music at 32 kHz stereo, sound effects at 22.05 kHz
 * mono, voices down to 11 kHz), decoded by the AICA chip.  Android has no such hardware, so the same
 * streams are decoded in software with the same step function (the step table of the AICA, which
 * third_party/wav2adpcm encodes against):
 *
 *   - a sound effect is decoded once when it is loaded (they are 0.1-3 s) so mixing is a plain read;
 *     22 kHz mono at 16 bit is about 44 KB per second of sound;
 *   - music stays compressed: a worker thread reads and decodes the .adp stream into a one-second
 *     ring buffer at the source rate, and the AAudio callback resamples it to the device rate, so no
 *     filesystem call ever happens on the audio thread.  The worker owns the file: requests (play /
 *     stop) are queued for it, so no FILE* is ever shared with the callback.
 *
 * Mixing is integer, 24 voices, linear pan, and the master volumes of the port settings multiply the
 * game's own volume options (the game still owns its MUSIC/SFX options in its own menu). */
#include <aaudio/AAudio.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "snd_android.h"
#include "plat_android.h"
#include "../../fp.h"

#define MAX_SND 768
#define MAX_VOICES 24
#define MUS_RATE 32000                 /* the ADPCM music stream (tools/audio/convert_audio.py) */
#define MUS_RING 32000                 /* one second of stereo at the source rate */
#define MUS_CHUNK 1024

typedef struct {
    int16_t *pcm;
    int frames, rate;
    char name[48];
    int used;
} snd_t;

static snd_t smp[MAX_SND];
static int nsmp;

typedef struct {
    const snd_t *s;
    int pos;                           /* 16.16 source frames */
    uint32_t step;                     /* 16.16 source frames per output frame */
    int32_t gl, gr;                    /* mixing gains (vol * pan * master) */
    int vol, pan;
    int loop;
    int on;
    int serial;
} voice_t;

static voice_t vox[MAX_VOICES];
static int voice_serial;
static int out_rate = 48000;
static int master_music = 80, master_sfx = 90;
static int underruns;

/* ------------------------------------------------------------------ ADPCM */
/* AICA / YMZ280B 4-bit step, the same arithmetic as third_party/wav2adpcm (ymz_step) */
static const int step_table[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };

static inline int16_t ymz_step(int step, int *history, int *step_size) {
    int sign = step & 8, delta = step & 7;
    int diff = ((1 + (delta << 1)) * *step_size) >> 3;
    int newval = *history;
    int nstep = (step_table[delta] * *step_size) >> 8;
    if (diff > 32767) diff = 32767;
    if (diff < 0) diff = 0;
    if (sign) newval -= diff;
    else newval += diff;
    if (nstep < 127) nstep = 127;
    if (nstep > 24576) nstep = 24576;
    *step_size = nstep;
    if (newval > 32767) newval = 32767;
    if (newval < -32768) newval = -32768;
    *history = newval;
    return (int16_t)newval;
}

/* one channel of a 4-bit stream: low nibble first, as wav2adpcm writes it */
static void adpcm_decode_mono(const uint8_t *in, int bytes, int16_t *out) {
    int hist = 0, sz = 127;
    for (int i = 0; i < bytes * 2; i++) {
        int8_t b = (int8_t)(in[i >> 1] << ((i & 1) * 4));
        out[i] = ymz_step((b >> 4) & 15, &hist, &sz);
    }
}

/* stereo interleaved ADPCM (one byte holds both channels: high nibble left, low nibble right) */
static void adpcm_decode_stereo(const uint8_t *in, int bytes, int16_t *out, int *hist, int *step) {
    for (int i = 0; i < bytes; i++) {
        out[i * 2 + 0] = ymz_step((in[i] >> 4) & 15, &hist[0], &step[0]);
        out[i * 2 + 1] = ymz_step(in[i] & 15, &hist[1], &step[1]);
    }
}

/* ------------------------------------------------------------------ WAV (KOS sfx) */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* <id>.wav: RIFF/WAVE, fmt 0x14 (AICA ADPCM mono 4 bit) or 0x01 (PCM16), then data */
static int load_wav(const char *name, const uint8_t *d, size_t size, snd_t *out) {
    if (size < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { plat_log("snd: %s is not a WAV\n", name); return -1; }
    int fmt = 0, ch = 1, rate = 22050, bits = 16;
    const uint8_t *pcm = NULL;
    size_t pcm_bytes = 0;
    size_t p = 12;
    while (p + 8 <= size) {
        uint32_t len = rd32(d + p + 4);
        const uint8_t *body = d + p + 8;
        if (p + 8 + (size_t)len > size) break;
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            fmt = rd16(body); ch = rd16(body + 2); rate = (int)rd32(body + 4); bits = rd16(body + 14);
        } else if (!memcmp(d + p, "data", 4)) {
            pcm = body;
            pcm_bytes = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!pcm || !pcm_bytes) { plat_log("snd: %s has no data chunk\n", name); return -1; }
    int frames;
    int16_t *outp;
    if (fmt == 0x14) {                                  /* AICA ADPCM, 4 bit mono */
        frames = (int)pcm_bytes * 2;
        outp = malloc((size_t)frames * 2);
        if (!outp) return -1;
        adpcm_decode_mono(pcm, (int)pcm_bytes, outp);
    } else if (fmt == 0x01 && bits == 16) {             /* PCM16 (the gallery voices) */
        int step_ch = ch > 0 ? ch : 1;
        frames = (int)(pcm_bytes / (size_t)(2 * step_ch));
        outp = malloc((size_t)frames * 2);
        if (!outp) return -1;
        for (int i = 0; i < frames; i++) outp[i] = (int16_t)rd16(pcm + (size_t)i * 2 * step_ch);
    } else {
        plat_log("snd: %s unsupported WAV format %d/%d bits\n", name, fmt, bits);
        return -1;
    }
    out->pcm = outp;
    out->frames = frames;
    out->rate = rate > 1000 ? rate : 22050;
    out->used = 1;
    snprintf(out->name, sizeof out->name, "%s", name);
    return 0;
}

/* ------------------------------------------------------------------ AAudio */
static AAudioStream *stream;
static int snd_suspended;

static void mix(int16_t *dst, int frames);

static aaudio_data_callback_result_t audio_cb(AAudioStream *s, void *ud, void *data, int32_t frames) {
    (void)s; (void)ud;
    if (snd_suspended) {
        memset(data, 0, (size_t)frames * 4);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    mix((int16_t *)data, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_error(AAudioStream *s, void *ud, aaudio_result_t err) {
    (void)s; (void)ud;
    plat_log("snd: AAudio error %d\n", (int)err);
}

int snd_init_state(void) {
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return -1;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    AAudioStreamBuilder_setErrorCallback(b, audio_error, NULL);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream);
    if (r != AAUDIO_OK) {
        /* some phones only accept their own rate: let AAudio pick it */
        AAudioStreamBuilder_setSampleRate(b, AAUDIO_UNSPECIFIED);
        r = AAudioStreamBuilder_openStream(b, &stream);
    }
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) {
        plat_log("snd: cannot open an output stream (%d), running silent\n", (int)r);
        stream = NULL;
        return -1;
    }
    out_rate = AAudioStream_getSampleRate(stream);
    if (out_rate <= 0) out_rate = 48000;
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        AAudioStream_close(stream);
        stream = NULL;
        return -1;
    }
    plat_log("snd: AAudio %d Hz, %d frames/burst (~%d us)\n", out_rate,
             (int)AAudioStream_getFramesPerBurst(stream),
             (int)AAudioStream_getFramesPerBurst(stream) * 1000000 / out_rate);
    return 0;
}

void snd_suspend(void) {
    snd_suspended = 1;
    if (stream) AAudioStream_requestPause(stream);
}
void snd_resume(void) {
    snd_suspended = 0;
    if (stream) AAudioStream_requestStart(stream);
}

int snd_out_rate(void) { return stream ? out_rate : 0; }
int snd_active_voices(void) {
    int n = 0;
    for (int i = 0; i < MAX_VOICES; i++) n += vox[i].on ? 1 : 0;
    return n;
}
int snd_underruns(void) { return underruns; }

static void voice_gain(voice_t *o) {
    int vol = o->vol, pan = o->pan;
    int volm = vol * master_sfx / 100 * 256 / 255;     /* 0..256 */
    int l = pan < 0 ? 0 : pan > 255 ? 255 : pan;
    int gl = l <= 128 ? 256 : (256 * (255 - l)) / 127;   /* centre = unity on both sides */
    int gr = l >= 128 ? 256 : (256 * l) / 128;
    o->gl = (int32_t)volm * gl / 256;
    o->gr = (int32_t)volm * gr / 256;
}

void snd_master(int music_pct, int sfx_pct) {
    if (music_pct < 0) music_pct = 0;
    if (music_pct > 100) music_pct = 100;
    if (sfx_pct < 0) sfx_pct = 0;
    if (sfx_pct > 100) sfx_pct = 100;
    master_music = music_pct;
    master_sfx = sfx_pct;
    for (int i = 0; i < MAX_VOICES; i++) if (vox[i].on && vox[i].s) voice_gain(&vox[i]);
}

/* ------------------------------------------------------------------ sound effects */
void snd_init_fp(void) { snd_init_state(); }

int snd_load(const char *name) {
    for (int i = 0; i < nsmp; i++)
        if (smp[i].used && !strcmp(smp[i].name, name)) return i;     /* already resident */
    int slot = -1;
    for (int i = 0; i < nsmp; i++)
        if (!smp[i].used) { slot = i; break; }
    if (slot < 0) {
        if (nsmp >= MAX_SND) { plat_log("snd: table full, cannot load %s\n", name); return -1; }
        slot = nsmp++;
    }
    size_t sz = 0;
    uint8_t *d = plat_load_file(name, &sz);
    if (!d) return -1;
    memset(&smp[slot], 0, sizeof smp[slot]);
    int r = load_wav(name, d, sz, &smp[slot]);
    free(d);
    if (r < 0) { smp[slot].used = 0; return -1; }
    return slot;
}

void snd_unload(int h) {
    if (h < 0 || h >= nsmp || !smp[h].used) return;
    for (int i = 0; i < MAX_VOICES; i++)
        if (vox[i].on && vox[i].s == &smp[h]) vox[i].on = 0;
    free(smp[h].pcm);
    smp[h].pcm = NULL;
    smp[h].used = 0;
}

int snd_mem_free_kb(void) {
    long used = 0;
    for (int i = 0; i < nsmp; i++)
        if (smp[i].used) used += (long)smp[i].frames * 2;
    long budget = 32L * 1024 * 1024;            /* the game budgets against ~2 MB of AICA RAM */
    long free_kb = (budget - used) / 1024;
    return free_kb > 0 ? (int)free_kb : 0;
}

static int voice_alloc(void) {
    int best = -1, best_serial = 0x7fffffff;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!vox[i].on) return i;
        if (vox[i].serial < best_serial) { best_serial = vox[i].serial; best = i; }   /* steal the oldest */
    }
    return best;
}

static void voice_start(int v, int h, int vol, int pan, int loop, int rate_hz) {
    if (v < 0 || h < 0 || h >= nsmp || !smp[h].used) return;
    voice_t *o = &vox[v];
    o->s = &smp[h];
    o->pos = 0;
    double pitch = rate_hz > 1000 ? (double)rate_hz / 22050.0 : 1.0;   /* the AICA FREQ argument, in Hz */
    o->step = (uint32_t)((double)(o->s->rate) * pitch / (double)out_rate * 65536.0);
    if (o->step == 0) o->step = 0x10000;
    o->vol = vol;
    o->pan = pan;
    o->loop = loop;
    o->on = 1;
    o->serial = ++voice_serial;
    voice_gain(o);
}

void snd_play(int h, int vol, int pan) {
    if (h < 0 || h >= nsmp || !smp[h].used || !stream) return;
    int v = voice_alloc();
    voice_start(v, h, vol < 0 ? 0 : vol > 255 ? 255 : vol, pan < 0 ? 128 : pan, 0, 0);
}

int snd_loop_start(int h, int vol, int freq) {
    if (h < 0 || h >= nsmp || !smp[h].used || !stream) return -1;
    int v = voice_alloc();
    if (v < 0) return -1;
    voice_start(v, h, vol < 0 ? 0 : vol > 255 ? 255 : vol, 128, 1, freq);
    return v;
}

void snd_loop_set(int ch, int vol, int freq) {
    if (ch < 0 || ch >= MAX_VOICES || !vox[ch].on) return;
    vox[ch].vol = vol < 0 ? 0 : vol > 255 ? 255 : vol;
    if (freq > 1000 && vox[ch].s)
        vox[ch].step = (uint32_t)((double)vox[ch].s->rate * ((double)freq / 22050.0) / (double)out_rate * 65536.0);
    voice_gain(&vox[ch]);
}

void snd_loop_end(int ch) {
    if (ch >= 0 && ch < MAX_VOICES) vox[ch].on = 0;
}

void snd_stop_all(void) {
    for (int i = 0; i < MAX_VOICES; i++) vox[i].on = 0;
}

void snd_free_all(void) {
    snd_stop_all();
    for (int i = 0; i < nsmp; i++) {
        if (!smp[i].used) continue;
        free(smp[i].pcm);
        smp[i].pcm = NULL;
        smp[i].used = 0;
    }
}

/* ------------------------------------------------------------------ music stream
 * The worker owns the file and the ADPCM state; the callback owns only the read side of the ring.
 * Requests (play / play_once / stop) are handed over through mus_req_* under the mutex. */
static int16_t mus_ring[MUS_RING * 2];
static volatile int mus_head, mus_tail;
static pthread_mutex_t mus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mus_cond = PTHREAD_COND_INITIALIZER;
static pthread_t mus_thread;
static int mus_thread_ok;
static FILE *mus_f;
static int mus_gen;
static char mus_req[64];
static int mus_req_kind;                 /* 0 idle, 1 play, 2 play once, 3 stop */
static char mus_name[64];
static int mus_vol = 255;                /* the game's own option, 0..255 */
static int mus_paused, mus_once, mus_ended, mus_serial, mus_stop_flag;
static int mus_hist[2], mus_step_size[2] = { 127, 127 };
static int mus_pcm_stream;              /* .pcm (s16le 22.05 kHz stereo) instead of .adp */
static int mus_rate = MUS_RATE;         /* source frames per second of the current track */
static double mus_phase;

int music_serial(void) { return mus_serial; }
int music_active(void) { return mus_f && !mus_paused && !mus_ended; }

static int ring_used(void) {
    int used = mus_head - mus_tail;
    if (used < 0) used += MUS_RING;
    return used;
}

static void cond_wait_ms(int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += (long)ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
    pthread_cond_timedwait(&mus_cond, &mus_lock, &ts);
}

static void *mus_worker(void *arg) {
    (void)arg;
    static uint8_t buf[MUS_CHUNK];
    static int16_t pcm[MUS_CHUNK * 2];
    for (;;) {
        pthread_mutex_lock(&mus_lock);
        if (mus_stop_flag) { pthread_mutex_unlock(&mus_lock); break; }

        if (mus_req_kind) {
            if (mus_f) { fclose(mus_f); mus_f = NULL; }
            int kind = mus_req_kind;
            mus_req_kind = 0;
            if (kind != 3) {
                char path[600];
                snprintf(path, sizeof path, "%s/%s", plat_data_root(), mus_req);
                mus_f = fopen(path, "rb");
                if (mus_f) {
                    snprintf(mus_name, sizeof mus_name, "%s", mus_req);
                    mus_pcm_stream = strstr(mus_req, ".pcm") != NULL;   /* the DC port played both */
                    mus_rate = mus_pcm_stream ? 22050 : MUS_RATE;
                } else {
                    plat_log("music missing %s\n", path);
                    mus_name[0] = 0;
                }
            } else {
                mus_name[0] = 0;
            }
            mus_head = mus_tail = 0;
            mus_hist[0] = mus_hist[1] = 0;
            mus_step_size[0] = mus_step_size[1] = 127;
            mus_phase = 0;
            mus_ended = 0;
            mus_once = kind == 2;
            mus_paused = 0;
            mus_gen++;
            mus_serial++;
        }

        if (!mus_f || mus_paused || ring_used() > MUS_RING - MUS_CHUNK) {
            cond_wait_ms(50);
            pthread_mutex_unlock(&mus_lock);
            continue;
        }
        int gen = mus_gen;
        FILE *f = mus_f;
        pthread_mutex_unlock(&mus_lock);

        int pcm_stream = mus_pcm_stream;
        size_t got = fread(buf, 1, sizeof buf, mus_f);
        if (got >= 2 * (size_t)(pcm_stream ? 2 : 1)) got &= ~(size_t)(pcm_stream ? 3 : 1);   /* whole stereo frames */

        pthread_mutex_lock(&mus_lock);
        if (gen != mus_gen) { pthread_mutex_unlock(&mus_lock); continue; }   /* a new track was requested */
        if (got == 0) {
            if (mus_once) {
                mus_ended = 1;
                pthread_mutex_unlock(&mus_lock);
                usleep(20000);
                continue;
            }
            fseek(f, 0, SEEK_SET);                        /* loop the whole file (as the DC port did) */
            mus_hist[0] = mus_hist[1] = 0;
            mus_step_size[0] = mus_step_size[1] = 127;
            pthread_mutex_unlock(&mus_lock);
            continue;
        }
        int frames = (int)got;
        if (pcm_stream) {                                /* s16le stereo */
            for (int i = 0; i < frames / 2; i++) {
                pcm[i * 2 + 0] = (int16_t)(buf[i * 4 + 0] | (buf[i * 4 + 1] << 8));
                pcm[i * 2 + 1] = (int16_t)(buf[i * 4 + 2] | (buf[i * 4 + 3] << 8));
            }
            frames /= 2;
        } else {
            adpcm_decode_stereo(buf, frames, pcm, mus_hist, mus_step_size);
        }
        int head = mus_head;
        for (int i = 0; i < frames; i++) {
            int idx = (head + i) % MUS_RING;
            mus_ring[idx * 2 + 0] = pcm[i * 2 + 0];
            mus_ring[idx * 2 + 1] = pcm[i * 2 + 1];
        }
        mus_head = (head + frames) % MUS_RING;
        pthread_mutex_unlock(&mus_lock);
    }
    return NULL;
}

static int music_request(int kind, const char *name) {
    if (!stream) return -1;
    if (!mus_thread_ok) mus_thread_ok = pthread_create(&mus_thread, NULL, mus_worker, NULL) == 0;
    if (!mus_thread_ok) return -1;
    pthread_mutex_lock(&mus_lock);
    mus_req_kind = kind;
    if (name) snprintf(mus_req, sizeof mus_req, "%s", name);
    if (kind != 3) {
        /* restart even if the same track is asked for again: the caller wants a fresh start */
        mus_name[0] = 0;
    }
    pthread_cond_broadcast(&mus_cond);
    pthread_mutex_unlock(&mus_lock);
    return 0;
}

void snd_stop_mixer(void) {
    music_stop();
    if (mus_thread_ok) {
        pthread_mutex_lock(&mus_lock);
        mus_stop_flag = 1;
        pthread_cond_broadcast(&mus_cond);
        pthread_mutex_unlock(&mus_lock);
        pthread_join(mus_thread, NULL);
        mus_thread_ok = 0;
    }
    if (stream) {
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
        stream = NULL;
    }
    snd_free_all();
}

int music_play(const char *name) { return music_request(1, name); }
int music_play_once(const char *name) { return music_request(2, name); }
void music_stop(void) { music_request(3, NULL); }

void music_pause(void) {
    pthread_mutex_lock(&mus_lock);
    mus_paused = 1;
    pthread_mutex_unlock(&mus_lock);
}
void music_resume(void) {
    pthread_mutex_lock(&mus_lock);
    mus_paused = 0;
    pthread_cond_broadcast(&mus_cond);
    pthread_mutex_unlock(&mus_lock);
}
void music_volume(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    mus_vol = pct * 255 / 100;
}
void music_update(void) {}

/* ------------------------------------------------------------------ the mixer */
static void mix(int16_t *dst, int frames) {
    pthread_mutex_lock(&mus_lock);
    int music_on = mus_f && !mus_paused;
    int head = mus_head, tail = mus_tail;
    int avail = ring_used();
    int gen = mus_gen;
    pthread_mutex_unlock(&mus_lock);
    (void)head; (void)gen;

    pthread_mutex_lock(&mus_lock);
    int src_rate = mus_rate;
    pthread_mutex_unlock(&mus_lock);
    const double src_step = (double)out_rate / (double)(src_rate > 1000 ? src_rate : MUS_RATE);
    const int32_t mg = mus_vol * master_music / 100;               /* 0..255 */
    int consumed = 0;
    int starve = 0;

    if (music_on && avail < 8) starve = 1;

    for (int i = 0; i < frames; i++) {
        int32_t l = 0, r = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            voice_t *o = &vox[v];
            if (!o->on) continue;
            if (!o->s || !o->s->pcm) { o->on = 0; continue; }
            int idx = o->pos >> 16;
            if (idx >= o->s->frames) {
                if (o->loop) { o->pos = 0; idx = 0; }
                else { o->on = 0; continue; }
            }
            int32_t s = o->s->pcm[idx];
            l += s * o->gl >> 8;
            r += s * o->gr >> 8;
            o->pos += (int)o->step;
        }
        if (music_on) {
            if (avail >= 4) {
                int i0 = tail, i1 = (tail + 1) % MUS_RING;
                int32_t a = mus_ring[i0 * 2 + 0], b = mus_ring[i1 * 2 + 0];
                int32_t c = mus_ring[i0 * 2 + 1], d = mus_ring[i1 * 2 + 1];
                int32_t fl = a + (int32_t)((b - a) * mus_phase);
                int32_t fr = c + (int32_t)((d - c) * mus_phase);
                l += fl * mg >> 8;
                r += fr * mg >> 8;
                mus_phase += src_step;
                while (mus_phase >= 1.0) {
                    mus_phase -= 1.0;
                    tail = (tail + 1) % MUS_RING;
                    avail--;
                }
                consumed = (tail - mus_tail + MUS_RING) % MUS_RING;
            } else {
                starve = 1;
            }
        }
        if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        dst[i * 2 + 0] = (int16_t)l;
        dst[i * 2 + 1] = (int16_t)r;
    }
    if (consumed) {
        pthread_mutex_lock(&mus_lock);
        mus_tail = tail;
        pthread_cond_broadcast(&mus_cond);
        pthread_mutex_unlock(&mus_lock);
    }
    if (starve && music_on) underruns++;
}
