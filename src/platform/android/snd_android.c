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
#include "aica_adpcm.h"
#include "../../fp.h"

#define MAX_SND 768
/* The AICA had 64 channels, and the phone needs them: a log of DV1 on 32 showed 19, 26 and even 32
 * voices busy for minutes, so every new effect stole the oldest one still playing -- which is heard as
 * "the effects are gone / sound terrible", not as too many sounds. */
#define MAX_VOICES 64
#define MUS_RATE 32000                 /* the ADPCM music stream (tools/audio/convert_audio.py) */
#define MUS_RING 32000                 /* one second of stereo at the source rate */
#define MUS_CHUNK 1024

typedef struct {
    int16_t *pcm;
    int frames, rate;
    char name[48];
    int used;
    int gen;                           /* a sample's generation: a handle carries it, so a slot the game freed and
                                        * another sound took over can never play that other sound (see snd_at) */
} snd_t;

static snd_t smp[MAX_SND];
static int nsmp;
static int snd_gen;

/* A handle is <generation> << 16 | <slot>.  The port keeps the ids' handles in its own tables (snd_handle, the
 * character's voices, a stage's voices, the on-demand voice slots), and those tables can outlive the samples they
 * name: engine_stop() frees every sample (snd_free_all) while the tables still hold the old slot numbers, and the
 * next session's snd_load() hands those slots to other files.  A plain slot number then plays whatever moved in -
 * report after report of "a man's voice instead of Carol's", "the wrong sound in the character select".  A stale
 * handle now fails the generation check, so it plays nothing: silence where a bug used to be a stranger's voice. */
static snd_t *snd_at(int h) {
    if (h < 0) return NULL;
    int slot = h & 0xffff;
    if (slot >= nsmp) return NULL;
    snd_t *s = &smp[slot];
    if (!s->used || s->gen != ((h >> 16) & 0xffff)) return NULL;
    return s;
}
static int snd_handle_of(const snd_t *s) { return (s->gen << 16) | (int)(s - smp); }
static void snd_new_gen(snd_t *s) {
    if (++snd_gen > 0xffff) snd_gen = 1;          /* 0 never: 0 << 16 | slot must not look like a bare slot */
    s->gen = snd_gen;
}

typedef struct {
    const snd_t *s;
    int64_t pos;                       /* 16.16 source frames: an int overflowed at 1.5 s of sample */
    uint32_t step;                     /* 16.16 source frames per output frame */
    int32_t gl, gr;                    /* mixing gains (vol * pan * master) */
    int vol, pan;
    int freq;                          /* the AICA FREQ argument in Hz (0/<=1000: the sample's own rate) */
    int loop;
    int on;
    int frozen;                        /* held by snd_pause_samples(): silent, keeps its place */
    int reserved;                      /* a looping channel the game keeps a handle on: never stolen */
    int serial;
} voice_t;

static voice_t vox[MAX_VOICES];
static int voice_serial;
/* The callback mixes while the game thread starts voices, stops them and frees sample memory
 * (snd_unload, snd_free_all): without this, a freed sample could still be read, and a voice could be
 * half-started.  Held for the length of one mix (~4 ms) and for the tiny mutations on the game side. */
static pthread_mutex_t mix_lock = PTHREAD_MUTEX_INITIALIZER;
static int out_rate = 48000;
static int master_music = 80, master_sfx = 90;
static int underruns;
static volatile int steals;            /* one-shot voices taken over by a newer sound: a full pool */
static int trace;                      /* sys.sndtrace: log every voice start, to answer "what sound is that?" */

void snd_trace(int on) { trace = on ? 1 : 0; }

/* the AICA step function and the two stream layouts: aica_adpcm.h (pinned by tools/android/check_audio.c) */

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
        aica_decode_mono(pcm, (int)pcm_bytes, outp);
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
static int susp_mask;                     /* see snd_android.h: any bit keeps the stream paused */
static volatile int stream_dead;          /* the error callback: the device took the stream away */
static volatile long long cb_frames;       /* frames delivered, for the one-time rate check */
static volatile uint64_t cb_t0;
static int rate_settled;
static int rate_windows;                   /* how many windows agreed on the rate (see snd_recover) */
static double rate_last;
static int mus_taken_logged;

static void mix(int16_t *dst, int frames);

static aaudio_data_callback_result_t audio_cb(AAudioStream *s, void *ud, void *data, int32_t frames) {
    (void)s; (void)ud;
    if (!cb_t0) cb_t0 = plat_time_us();
    cb_frames += frames;
    if (susp_mask) {
        memset(data, 0, (size_t)frames * 4);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    mix((int16_t *)data, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_error(AAudioStream *s, void *ud, aaudio_result_t err) {
    (void)s; (void)ud;
    plat_log("snd: AAudio error %d\n", (int)err);
    stream_dead = 1;                       /* rebuilt by snd_recover() on the game thread */
}

static void stream_close(void) {
    if (!stream) return;
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    stream = NULL;
    plat_log("snd: stream closed");
}

int snd_init_state(void) {
    /* One stream only.  engine_start() runs on every PLAY, and opening a second stream left the first
     * one playing as well: two callbacks mixing the same rings and voices, so the music ran at double
     * tempo and starved the ring every few seconds (fast, silence, fast again). */
    if (stream) return 0;
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
    stream_dead = 0;
    rate_settled = 0;
    cb_t0 = 0;
    cb_frames = 0;
    plat_log("snd: AAudio %d Hz, %d frames/burst (~%d us)\n", out_rate,
             (int)AAudioStream_getFramesPerBurst(stream),
             (int)AAudioStream_getFramesPerBurst(stream) * 1000000 / out_rate);
    return 0;
}

void snd_suspend(int reason) {
    int was = susp_mask;
    susp_mask |= reason;
    if (was == susp_mask) return;          /* already stopped for this (or another) reason */
    plat_log("snd: mixing stopped (reasons 0x%x)", susp_mask);
    cb_t0 = 0;                             /* a pause is not part of the delivery rate */
    if (stream) AAudioStream_requestPause(stream);
}
void snd_resume(int reason) {
    int was = susp_mask;
    susp_mask &= ~reason;
    if (was == susp_mask) return;
    plat_log("snd: mixing %s (reasons 0x%x)", susp_mask ? "still stopped" : "restored", susp_mask);
    if (susp_mask) return;                 /* another reason is still in force */
    cb_t0 = 0;                             /* the delivery rate is measured again from here */
    cb_frames = 0;
    if (stream) AAudioStream_requestStart(stream);
}
int snd_suspended(void) { return susp_mask; }

int snd_out_rate(void) { return stream ? out_rate : 0; }
int snd_active_voices(void) {
    int n = 0;
    for (int i = 0; i < MAX_VOICES; i++) n += vox[i].on ? 1 : 0;
    return n;
}
int snd_underruns(void) { return underruns; }
int snd_steals(void) { return steals; }

/* source frames per output frame, 16.16: the sample's rate times the requested pitch over the device
 * rate.  Recomputed whenever the device rate turns out to be something else than it claimed. */
static void voice_step(voice_t *o) {
    if (!o->s) { o->step = 0x10000; return; }
    double pitch = o->freq > 1000 ? (double)o->freq / 22050.0 : 1.0;   /* the AICA FREQ argument, in Hz */
    o->step = (uint32_t)((double)o->s->rate * pitch / (double)out_rate * 65536.0);
    if (o->step == 0) o->step = 0x10000;
}

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
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < MAX_VOICES; i++) if (vox[i].on && vox[i].s) voice_gain(&vox[i]);
    pthread_mutex_unlock(&mix_lock);
}

/* ------------------------------------------------------------------ sound effects */
void snd_init_fp(void) { snd_init_state(); }

int snd_load(const char *name) {
    for (int i = 0; i < nsmp; i++)
        if (smp[i].used && !strcmp(smp[i].name, name)) return snd_handle_of(&smp[i]);   /* already resident */
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
    snd_new_gen(&smp[slot]);
    return snd_handle_of(&smp[slot]);
}

void snd_unload(int h) {
    snd_t *s = snd_at(h);
    if (!s) return;                                 /* a handle of a sample that is already gone: nothing to do */
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < MAX_VOICES; i++)
        if (vox[i].on && vox[i].s == s) vox[i].on = 0;
    free(s->pcm);
    s->pcm = NULL;
    s->used = 0;
    s->gen = 0;
    pthread_mutex_unlock(&mix_lock);
}

int snd_mem_free_kb(void) {
    long used = 0;
    for (int i = 0; i < nsmp; i++)
        if (smp[i].used) used += (long)smp[i].frames * 2;
    long budget = 32L * 1024 * 1024;            /* the game budgets against ~2 MB of AICA RAM */
    long free_kb = (budget - used) / 1024;
    return free_kb > 0 ? (int)free_kb : 0;
}

/* A free voice, else the oldest one-shot.  Voices the game holds a handle on (snd_loop_start: Carol's
 * bike, the trolley, rain) are never stolen: stealing one meant the game's next snd_loop_update()
 * retuned and re-volumed whatever unrelated sound had taken the slot, which is heard as random extra
 * noises on top of the mix. */
static int voice_alloc(void) {
    int best = -1, best_serial = 0x7fffffff;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!vox[i].on) return i;
        if (vox[i].reserved) continue;
        if (vox[i].serial < best_serial) { best_serial = vox[i].serial; best = i; }   /* steal the oldest */
    }
    if (best >= 0) steals++;
    return best;
}

static void voice_start(int v, snd_t *s, int vol, int pan, int loop, int rate_hz) {
    if (v < 0 || !s) return;
    pthread_mutex_lock(&mix_lock);
    voice_t *o = &vox[v];
    o->s = s;
    o->pos = 0;
    o->freq = rate_hz;
    voice_step(o);
    o->vol = vol;
    o->pan = pan;
    o->loop = loop;
    o->reserved = loop ? 1 : 0;
    o->frozen = 0;                     /* a sound started while a pause is up is heard (the panel's beep) */
    o->on = 1;
    o->serial = ++voice_serial;
    voice_gain(o);
    char nm[48];
    snprintf(nm, sizeof nm, "%s", o->s->name);   /* copied here: the slot may be freed after the unlock */
    pthread_mutex_unlock(&mix_lock);
    /* the trace line outside the lock: logcat is not real-time and the audio callback waits on this mutex */
    if (trace) plat_log("snd: play %s (slot %d) on voice %d, vol %d pan %d%s", nm, (int)(s - smp), v, vol, pan,
                        loop ? " loop" : "");
}

void snd_play(int h, int vol, int pan) {
    snd_t *s = snd_at(h);
    if (!s || !stream) return;
    int v = voice_alloc();
    voice_start(v, s, vol < 0 ? 0 : vol > 255 ? 255 : vol, pan < 0 ? 128 : pan, 0, 0);
}

int snd_loop_start(int h, int vol, int freq) {
    snd_t *s = snd_at(h);
    if (!s || !stream) return -1;
    int v = voice_alloc();
    if (v < 0) return -1;
    voice_start(v, s, vol < 0 ? 0 : vol > 255 ? 255 : vol, 128, 1, freq);
    return v;
}

/* the name of the sample a handle names ("" when the handle is stale): the log says what a menu or a voice plays,
 * so a report of a wrong sound names the file instead of leaving us guessing */
int snd_sample_name(int h, char *dst, int cap) {
    if (!dst || cap <= 0) return 0;
    dst[0] = 0;
    snd_t *s = snd_at(h);
    if (!s) return 0;
    int n = 0;
    for (; n < cap - 1 && s->name[n]; n++) dst[n] = s->name[n];
    dst[n] = 0;
    return n;
}

void snd_loop_set(int ch, int vol, int freq) {
    if (ch < 0 || ch >= MAX_VOICES || !vox[ch].on) return;
    pthread_mutex_lock(&mix_lock);
    vox[ch].vol = vol < 0 ? 0 : vol > 255 ? 255 : vol;
    if (freq > 1000) { vox[ch].freq = freq; voice_step(&vox[ch]); }
    voice_gain(&vox[ch]);
    pthread_mutex_unlock(&mix_lock);
}

void snd_loop_end(int ch) {
    pthread_mutex_lock(&mix_lock);
    if (ch >= 0 && ch < MAX_VOICES) { vox[ch].on = 0; vox[ch].reserved = 0; }
    pthread_mutex_unlock(&mix_lock);
}

/* Media::pause_samples(): a pause holds every sample where it is -- the ambient loops of a story scene
 * come back where they stopped. The port used to call snd_stop_all() when the pause panel opened, which
 * KILLED them: after one pause the wind, the rain and the dialogue voice of that scene were gone for
 * good, which reads as "half of the audio goes away when you pause". A sound started while the pause is
 * up (the panel's own confirm beep) still plays: it is not frozen. */
void snd_pause_samples(int on) {
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!vox[i].on) continue;
        vox[i].frozen = on ? 1 : 0;
    }
    pthread_mutex_unlock(&mix_lock);
}

void snd_stop_all(void) {
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < MAX_VOICES; i++) { vox[i].on = 0; vox[i].frozen = 0; vox[i].reserved = 0; }
    pthread_mutex_unlock(&mix_lock);
}

void snd_free_all(void) {
    snd_stop_all();
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < nsmp; i++) {
        if (!smp[i].used) continue;
        free(smp[i].pcm);
        smp[i].pcm = NULL;
        smp[i].used = 0;
        smp[i].gen = 0;                 /* every handle that names this sample is stale from here on */
    }
    pthread_mutex_unlock(&mix_lock);
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
static volatile int mus_gen;            /* read without the lock by the callback (see ring_owned) */
static int mus_taken;                  /* buffers abandoned because another track took the ring */
static char mus_req[64];
static int mus_req_kind;                 /* 0 idle, 1 play, 2 play once, 3 stop */
static char mus_name[64];
static int mus_vol = 255;                /* the game's own option, 0..255 */
static int mus_paused, mus_once, mus_ended, mus_serial, mus_stop_flag;
static int underruns_logged;           /* the last underrun count in the log (see snd_recover) */
static int mus_hist[2], mus_step_size[2] = { 127, 127 };
static int mus_pcm_stream;              /* .pcm (s16le 22.05 kHz stereo) instead of .adp */
static int mus_rate = MUS_RATE;         /* source frames per second of the current track */
static double mus_phase;

int music_serial(void) { return mus_serial; }
int music_active(void) { return mus_f && !mus_paused && !mus_ended; }

/* the current track, for the log and the crash report (copied without the lock and without trusting
 * the terminator: a report must never be the thing that crashes) */
int snd_music_name(char *dst, int cap) {
    if (!dst || cap <= 0) return 0;
    int n = 0;
    for (; n < cap - 1 && n < (int)sizeof mus_name && mus_name[n]; n++) dst[n] = mus_name[n];
    dst[n] = 0;
    return n;
}

static int ring_used(void) {
    int used = mus_head - mus_tail;
    if (used < 0) used += MUS_RING;
    return used;
}

/* The ring belongs to the track that was requested when it was read.  A request (play, play once, stop)
 * zeroes it and starts another stream there, so the read position of a buffer that was being mixed at that
 * moment names a place in the OLD ring: publishing it back used to report ring_used() of about half a
 * second of audio the worker had not written -- the previous song's leftover frames (the ring is a plain
 * array, the old samples are still in it), played over the start of the new one, and then a gap while the
 * worker waited for a "full" ring to drain.  That is the report "the music glitches / plays the wrong thing
 * when a stage or a scene starts": every track change had a chance to do it.  Two uses, both under the
 * rule: the callback stops reading the ring in the middle of a buffer, and it publishes its position only
 * into the ring it read. */
static int ring_owned(int gen) { return mus_gen == gen; }

static void ring_publish(int gen, int tail) {
    pthread_mutex_lock(&mus_lock);
    if (ring_owned(gen)) {                    /* the same track: hand the read position over */
        mus_tail = tail;
        pthread_cond_broadcast(&mus_cond);
    } else {
        mus_taken++;
    }
    pthread_mutex_unlock(&mus_lock);
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
            int kind = mus_req_kind;
            char req[64];
            snprintf(req, sizeof req, "%s", mus_req);
            mus_req_kind = 0;
            if (mus_f) { fclose(mus_f); mus_f = NULL; }
            mus_gen++;                       /* a read that is already in flight is stale from here on */
            FILE *nf = NULL;
            if (kind != 3) {
                char path[600];
                snprintf(path, sizeof path, "%s/%s", plat_data_root(), req);
                pthread_mutex_unlock(&mus_lock);      /* opening a file never holds the mixer's lock */
                nf = fopen(path, "rb");
                pthread_mutex_lock(&mus_lock);
                if (mus_req_kind) {                   /* a newer request came in while the file opened */
                    if (nf) fclose(nf);
                    /* drop this one and let the loop take the newer one.  The lock must be released
                     * first: the top of the loop takes it again, and a second lock of a mutex this
                     * thread already owns is a deadlock (the audio callback would wait for ever) */
                    pthread_mutex_unlock(&mus_lock);
                    continue;
                }
                if (nf) {
                    snprintf(mus_name, sizeof mus_name, "%s", req);
                    mus_pcm_stream = strstr(req, ".pcm") != NULL;   /* the DC port played both */
                    mus_rate = mus_pcm_stream ? 22050 : MUS_RATE;
                    plat_log("snd: music %s, %d Hz stereo -> %d Hz", req, mus_rate, out_rate);
                } else {
                    plat_log("music missing %s", path);
                    mus_name[0] = 0;
                }
            } else {
                mus_name[0] = 0;
            }
            mus_f = nf;
            mus_head = mus_tail = 0;
            mus_hist[0] = mus_hist[1] = 0;
            mus_step_size[0] = mus_step_size[1] = 127;
            mus_phase = 0;
            mus_ended = 0;
            mus_once = kind == 2;
            mus_paused = 0;
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
        if (!f) { pthread_mutex_lock(&mus_lock); continue; }   /* only this thread opens or closes it */
        size_t got = fread(buf, 1, sizeof buf, f);
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
            aica_decode_stereo(buf, frames, pcm, mus_hist, mus_step_size);
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
    stream_close();
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
/* A hard clamp tears: the music alone sits at ~80% of full scale, so any voice or effect on top of it clipped, and a
 * clipped mix is heard as a broken, "wrong" soundtrack.  The DC's AICA could clip too, but its music was mixed with
 * more headroom; this keeps everything below the knee untouched and bends the peaks instead of cutting them. */
static inline int32_t snd_soft(int32_t x) {
    const int32_t T = 24000, R = 32767 - T;
    int32_t a = x < 0 ? -x : x;
    if (a <= T) return x;
    int64_t d = a - T;
    int32_t y = T + (int32_t)((int64_t)R * d / ((int64_t)R + d));   /* approaches 32767 as d grows */
    return x < 0 ? -y : y;
}

static void mix(int16_t *dst, int frames) {
    pthread_mutex_lock(&mix_lock);
    pthread_mutex_lock(&mus_lock);
    int music_on = mus_f && !mus_paused;
    int tail = mus_tail;                           /* one look at the ring, with its rate */
    int avail = ring_used();
    int gen = mus_gen;
    int src_rate = mus_rate;
    pthread_mutex_unlock(&mus_lock);
    /* source frames per output frame: 0.667 for a 32 kHz stream on a 48 kHz device.  It used to be the
     * inverse (1.5), so the ring was drained 2.25x faster than the worker filled it: the music ran
     * fast, the ring starved, the worker refilled it and off it went again. */
    const double src_step = (double)(src_rate > 1000 ? src_rate : MUS_RATE) / (double)out_rate;
    const int32_t mg = mus_vol * master_music / 100;               /* 0..255 */
    int consumed = 0;
    int starve = 0;

    if (music_on && avail < 8) starve = 1;

    for (int i = 0; i < frames; i++) {
        int32_t l = 0, r = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            voice_t *o = &vox[v];
            if (!o->on) continue;
            if (o->frozen) continue;                       /* paused: silent, not advanced */
            if (!o->s || !o->s->pcm) { o->on = 0; continue; }
            int idx = (int)(o->pos >> 16);
            if (idx >= o->s->frames) {
                if (o->loop) { o->pos = 0; idx = 0; }
                else { o->on = 0; continue; }
            }
            int32_t s = o->s->pcm[idx];
            int nxt = idx + 1;
            if (nxt < o->s->frames) {                 /* linear between the two source frames: the
                                                       * hardware's DAC smoothed them, a bare nearest
                                                       * read sounds harsh at 22 kHz into 48 kHz */
                int32_t f = (int32_t)((o->pos >> 8) & 0xff);
                s += (int32_t)(((int64_t)(o->s->pcm[nxt] - s) * f) >> 8);
            }
            l += s * o->gl >> 8;
            r += s * o->gr >> 8;
            o->pos += (int64_t)o->step;
        }
        if (music_on && !ring_owned(gen)) {             /* another track took the ring mid-buffer: stop
                                                         * reading it, this buffer's music ends here */
            music_on = 0;
            mus_taken++;
        }
        if (music_on) {
            if (avail >= 4) {
                /* 32 kHz -> the device's rate is a real resample, and a straight line between two source frames leaves
                 * everything above 16 kHz folded back in: the music then sounds gritty and slightly out of tune on a
                 * good speaker.  Catmull-Rom through four frames is as cheap and much cleaner. */
                int i0 = (tail + MUS_RING - 1) % MUS_RING, i1 = tail;
                int i2 = (tail + 1) % MUS_RING, i3 = (tail + 2) % MUS_RING;
                const double t = mus_phase, t2 = t * t, t3 = t2 * t;
                double w0 = -0.5 * t3 + t2 - 0.5 * t;
                double w1 = 1.5 * t3 - 2.5 * t2 + 1.0;
                double w2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
                double w3 = 0.5 * t3 - 0.5 * t2;
                int32_t fl = (int32_t)(mus_ring[i0 * 2 + 0] * w0 + mus_ring[i1 * 2 + 0] * w1 +
                                       mus_ring[i2 * 2 + 0] * w2 + mus_ring[i3 * 2 + 0] * w3);
                int32_t fr = (int32_t)(mus_ring[i0 * 2 + 1] * w0 + mus_ring[i1 * 2 + 1] * w1 +
                                       mus_ring[i2 * 2 + 1] * w2 + mus_ring[i3 * 2 + 1] * w3);
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
        dst[i * 2 + 0] = (int16_t)snd_soft(l);
        dst[i * 2 + 1] = (int16_t)snd_soft(r);
    }
    if (consumed) ring_publish(gen, tail);
    if (starve && music_on) underruns++;
    pthread_mutex_unlock(&mix_lock);
}

/* ------------------------------------------------------------------ the game thread's audio upkeep
 * Called once per iteration of the main loop (launcher and game alike):
 *   - a stream the device took away (route change, other app grabbing the output) is rebuilt; AAudio
 *     only reports that through the error callback, and a dead stream is silent for good otherwise;
 *   - the delivery rate is measured once over four seconds.  Every rate in this file comes from
 *     AAudioStream_getSampleRate(), and a phone that delivers another rate than it reports would make
 *     the music drift (fast for a while, then the ring starves) -- the tempo and the voices follow the
 *     measured rate instead of the claimed one.
 */
void snd_recover(void) {
    /* A ring that ran dry is the moment the music is silent while the worker refills it -- a hitch, the
     * report "the music stutters".  Silence about it cannot be told apart from a wrong tempo, a missing
     * track or a device that underruns, so say it once and then per 100; the same for a buffer abandoned
     * because another track took the ring (music_taken is expected on every track change, rare enough
     * that the count stays small unless something keeps restarting the track). */
    if (underruns != underruns_logged && (underruns_logged == 0 || underruns - underruns_logged >= 100)) {
        char nm[64];
        snd_music_name(nm, sizeof nm);
        plat_log("snd: the music ring ran dry %d times (track %s, %d Hz out)", underruns,
                 nm[0] ? nm : "-", out_rate);
        underruns_logged = underruns;
    }
    if (mus_taken != mus_taken_logged) {
        char nm[64];
        snd_music_name(nm, sizeof nm);
        plat_log("snd: a track change took the ring %d times (track %s)", mus_taken, nm[0] ? nm : "-");
        mus_taken_logged = mus_taken;
    }
    if (stream_dead) {
        plat_log("snd: the output device disconnected, rebuilding the stream");
        stream_close();
        stream_dead = 0;
        rate_settled = 0;
        cb_t0 = 0;
        cb_frames = 0;
        snd_init_state();
        return;
    }
    if (rate_settled || !stream || susp_mask || !cb_t0) return;
    uint64_t dt = plat_time_us() - cb_t0;
    if (dt < 4000000ull) return;
    double meas = (double)cb_frames * 1000000.0 / (double)dt;
    cb_t0 = 0;
    cb_frames = 0;
    if (!(meas > 4000.0)) return;                  /* the window covered a stall: ask again */
    /* The delivered rate is trusted only when two windows in a row agree on it.  One window is measured
     * right after the stream starts, while the game is still loading a stage: a callback that ran late
     * makes it read low, and adopting that number resamples the music and every voice to a rate the device
     * does not run at -- the tempo drifts, the ring fills and empties at the wrong speed and the music
     * hitches for the rest of the session.  A phone that really delivers another rate says so twice. */
    double off = meas / (double)out_rate - 1.0;
    if (fabs(off) <= 0.02) {                       /* the stream reports the truth: nothing to do */
        rate_settled = 1;
        plat_log("snd: %.0f frames/s delivered, the stream reports %d Hz", meas, out_rate);
        return;
    }
    if (rate_windows == 0 || fabs(meas / rate_last - 1.0) > 0.01) {
        rate_last = meas;
        rate_windows = 1;
        plat_log("snd: %.0f frames/s delivered, the stream reports %d Hz (measuring again)", meas, out_rate);
        return;
    }
    rate_windows = 0;
    if (fabs(off) > 0.35) {                        /* not a rate this stream could be: a bad window */
        plat_log("snd: %.0f frames/s delivered is not believable for a %d Hz stream", meas, out_rate);
        return;
    }
    rate_settled = 1;
    out_rate = (int)(meas + 0.5);
    plat_log("snd: the device does not run at the rate it reports: resampling for %d Hz", out_rate);
    pthread_mutex_lock(&mix_lock);
    for (int i = 0; i < MAX_VOICES; i++) if (vox[i].on) voice_step(&vox[i]);
    pthread_mutex_unlock(&mix_lock);
}
