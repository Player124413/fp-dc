/* The Android audio path without a device: stream lifetime, tempo, and the ADPCM layouts.
 *
 * Reported from a phone: "the sound is very fast, then silence, then something odd again".
 *   - engine_start() runs on every PLAY and snd_init_state() opened a *new* AAudio stream each time,
 *     leaking the previous one, which kept calling the same mixer: two (three, four) consumers of one
 *     ring means the music runs at two (three, four) times its tempo and the ring starves in between.
 *   - the tempo itself is a rate chain: the stream's rate, the source rate (music 32 kHz, effects
 *     22050 Hz), and the ADPCM layouts (one byte = one stereo frame, two samples in mono). A wrong
 *     link anywhere sounds exactly like the report.
 *
 * This test links the real snd_android.c (its statics are reachable from the same translation unit)
 * against a fake AAudio that counts streams, so all of that is pinned where no phone is needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "../../src/platform/android/snd_android.c"

/* ------------------------------------------------------------------ the fake AAudio + platform */
struct AAudioStream { int rate, running; };
struct AAudioStreamBuilder { int rate, ch; };

static int open_calls, close_calls, start_calls, stop_calls;
static struct AAudioStream fake_stream;
static struct AAudioStreamBuilder fake_builder;

static int fails;
static void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("  FAIL ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fails++;
}

aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b) { *b = &fake_builder; return AAUDIO_OK; }
void AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { (void)b; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t v) { b->ch = (int)v; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, int32_t v) { b->rate = (int)v; }
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *ud) { (void)b; (void)cb; (void)ud; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *ud) { (void)b; (void)cb; (void)ud; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **out) {
    open_calls++;
    fake_stream.rate = b->rate ? b->rate : 48000;      /* AAUDIO_UNSPECIFIED: the device's own rate */
    fake_stream.running = 0;
    *out = &fake_stream;
    return AAUDIO_OK;
}
int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->rate; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s) { (void)s; return 192; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) { start_calls++; s->running = 1; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) { s->running = 0; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) { stop_calls++; s->running = 0; return AAUDIO_OK; }
aaudio_result_t AAudioStream_close(AAudioStream *s) { (void)s; close_calls++; return AAUDIO_OK; }

void plat_log(const char *fmt, ...) { (void)fmt; }                    /* keep the test's output clean */
void *plat_load_file(const char *name, size_t *size) { (void)name; if (size) *size = 0; return NULL; }
const char *plat_data_root(void) { return "/nonexistent"; }
const char *plat_files_root(void) { return "/nonexistent"; }
uint64_t plat_time_us(void) { static uint64_t t; return t += 1000; }

/* ------------------------------------------------------------------ helpers */
static void expect_near(int got, int want, int tol, const char *what) {
    if (got < want - tol || got > want + tol) fail("%s: %d (want %d +-%d)", what, got, want, tol);
}

static void check_stream_lifetime(void) {
    open_calls = close_calls = start_calls = 0;
    snd_init_fp();
    snd_init_fp();                                   /* the second PLAY of the session */
    snd_init_fp();
    if (open_calls != 1) fail("three init calls opened %d streams (one mixer may only run once)", open_calls);
    if (start_calls != 1) fail("%d requestStart calls", start_calls);
    if (close_calls != 0) fail("%d close calls before shutdown", close_calls);
    snd_stop_mixer();
    if (close_calls != 1) fail("shutdown closed %d streams", close_calls);
    snd_init_fp();                                   /* and it can be brought back up */
    if (open_calls != 2) fail("reopening after shutdown: %d streams opened", open_calls);
    if (stream == NULL) fail("no stream after reopen");
}

/* fill the ring with a ramp whose index is the source frame number, so the mixer's position through the
 * stream is readable in the output */
static void ring_fill(void) {
    for (int i = 0; i < MUS_RING; i++) {
        int16_t v = (int16_t)(i - 16000);             /* ramps through zero, unique per frame */
        mus_ring[i * 2 + 0] = v;
        mus_ring[i * 2 + 1] = v;
    }
    mus_head = MUS_RING - 1;                          /* nearly a full second of source */
    mus_tail = 0;
}

static void check_music_tempo(void) {
    int16_t out[4800 * 2];
    static FILE *dummy;
    if (!dummy) dummy = tmpfile();
    mus_f = dummy;
    mus_paused = 0;
    mus_vol = 255;
    master_music = 100;
    voice_t *v;
    for (int i = 0; i < MAX_VOICES; i++) { v = &vox[i]; v->on = 0; }

    /* 0.1 s of output at 48 kHz must consume 0.1 s of source: 3200 frames at 32 kHz ... */
    mus_rate = 32000;
    mus_phase = 0;
    ring_fill();
    mix(out, 4800);
    expect_near(mus_tail, 3200, 2, "32 kHz music, 4800 output frames consumed");

    /* ... 2205 frames at 22.05 kHz, and the same tempo in both cases */
    mus_rate = 22050;
    mus_phase = 0;
    ring_fill();
    mix(out, 4800);
    expect_near(mus_tail, 2205, 2, "22.05 kHz music, 4800 output frames consumed");

    /* a starved ring must output silence, not speed up */
    mus_rate = 32000;
    mus_phase = 0;
    mus_tail = mus_head = 0;
    memset(out, 0x7f, sizeof out);
    mix(out, 4800);
    for (int i = 0; i < 4800 * 2; i++)
        if (out[i] != 0) { fail("starved ring: sample %d is %d, not silence", i, out[i]); break; }
    mus_f = NULL;
}

static void check_voice_tempo(void) {
    int16_t out[48000 * 2];
    static int16_t pcm[22050];
    for (int i = 0; i < 22050; i++) pcm[i] = (int16_t)((i * 97) & 0x7fff);   /* any signal */
    for (int i = 0; i < MAX_VOICES; i++) vox[i].on = 0;
    for (int i = 0; i < MAX_SND; i++) smp[i].used = 0;
    nsmp = 1;
    smp[0].pcm = pcm;
    smp[0].frames = 22050;
    smp[0].rate = 22050;                              /* a 1 s effect, as the converter writes it */
    smp[0].used = 1;
    snprintf(smp[0].name, sizeof smp[0].name, "test");
    mus_f = NULL;

    voice_start(0, 0, 255, 128, 0, 0);
    if (!vox[0].on) { fail("the voice did not start"); return; }
    mix(out, 24000);                                  /* half a second */
    expect_near((int)(vox[0].pos >> 16), 11025, 40, "a 22050 Hz voice after half a second");
    mix(out, 24000);                                  /* the second half */
    mix(out, 64);                                     /* the frame that runs off the end turns it off */
    if (vox[0].on) fail("a 1 s voice is still playing after 1 s of output (pos %d of %d)", vox[0].pos >> 16, 22050);
    vox[0].on = 0;
}

/* a looping channel the game holds a handle on must survive a burst of effects: stealing it made the
 * game's next snd_loop_update() retune an unrelated sound (heard as random extra noises) */
static void check_loop_voices_are_not_stolen(void) {
    static int16_t loop_pcm[4096], shot_pcm[512];
    for (int i = 0; i < 4096; i++) loop_pcm[i] = (int16_t)(i * 31);
    for (int i = 0; i < 512; i++) shot_pcm[i] = (int16_t)(i * 61);
    for (int i = 0; i < MAX_SND; i++) smp[i].used = 0;
    for (int i = 0; i < MAX_VOICES; i++) { vox[i].on = 0; vox[i].reserved = 0; }
    nsmp = 2;
    smp[0].pcm = loop_pcm; smp[0].frames = 4096; smp[0].rate = 22050; smp[0].used = 1;
    snprintf(smp[0].name, sizeof smp[0].name, "loop");
    smp[1].pcm = shot_pcm; smp[1].frames = 512; smp[1].rate = 22050; smp[1].used = 1;
    snprintf(smp[1].name, sizeof smp[1].name, "shot");
    mus_f = NULL;

    int ch = snd_loop_start(0, 255, 0);
    if (ch < 0) { fail("the looping channel did not start"); return; }
    for (int k = 0; k < MAX_VOICES * 3; k++) snd_play(1, 255, 128);       /* more effects than voices */
    if (!vox[ch].on) fail("a burst of effects stole the game's looping channel");
    if (vox[ch].s != &smp[0]) fail("the looping channel now plays a different sample");
    if (!vox[ch].reserved) fail("the loop lost its reservation");
    snd_loop_end(ch);
    if (vox[ch].reserved) fail("snd_loop_end left the voice reserved");
    snd_stop_all();
    for (int i = 0; i < MAX_VOICES; i++) if (vox[i].reserved) fail("voice %d still reserved after stop_all", i);
}

static void check_adpcm_layouts(void) {
    int16_t out[16];
    int hist[2] = { 0, 0 }, step[2] = { 127, 127 };
    uint8_t b = 0x78;                                 /* left nibble 7 (a positive step), right 8 (negative) */
    aica_decode_stereo(&b, 1, out, hist, step);
    if (!(out[0] > 100)) fail("stereo: the high nibble is not the left channel (%d)", out[0]);
    if (!(out[1] < 0)) fail("stereo: the low nibble is not the right channel (%d)", out[1]);

    /* one byte is one frame: 100 bytes -> 100 stereo frames, never 200 */
    uint8_t buf[100];
    memset(buf, 0x11, sizeof buf);
    int16_t stereo[200];
    hist[0] = hist[1] = 0; step[0] = step[1] = 127;
    aica_decode_stereo(buf, 100, stereo, hist, step);
    if (stereo[199] == 0 && stereo[100] == 0) fail("stereo: 100 bytes produced fewer than 100 frames");

    /* mono: two samples per byte, low nibble first */
    int16_t mono[2];
    b = 0x87;                                         /* low nibble 7, then high nibble 8 */
    aica_decode_mono(&b, 1, mono);
    if (!(mono[0] > 100)) fail("mono: the low nibble is not the first sample (%d)", mono[0]);
    if (!(mono[1] < mono[0])) fail("mono: the high nibble is not the second sample (%d, %d)", mono[0], mono[1]);

    /* a full second of 32 kHz stereo ADPCM is 32000 bytes = 32000 frames */
    static uint8_t sec[32000];
    static int16_t secpcm[32000 * 2];
    memset(sec, 0x00, sizeof sec);
    hist[0] = hist[1] = 0; step[0] = step[1] = 127;
    aica_decode_stereo(sec, 32000, secpcm, hist, step);
    if (secpcm[31999 * 2] == 0 && secpcm[32000 * 2 - 1] == 0) fail("a second of ADPCM did not fill 32000 frames");
}

int main(void) {
    printf("audio checks\n");
    check_stream_lifetime();
    check_music_tempo();
    check_voice_tempo();
    check_loop_voices_are_not_stolen();
    check_adpcm_layouts();
    printf("audio %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
