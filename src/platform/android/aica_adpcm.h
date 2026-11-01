/* AICA (YMZ280B) 4-bit ADPCM: the step function the Dreamcast's sound chip decodes with, and the two
 * stream layouts the game's data uses.  Shared by the Android mixer and by tools/android/check_audio.c,
 * which pins the layouts without a device (one byte is one *stereo frame*, two nibbles are two *mono
 * samples*: getting that wrong makes the music play at twice its tempo and the ring starve every few
 * seconds, which sounds like "fast, then silence, then something odd").
 *
 * The arithmetic is the hardware's (third_party/wav2adpcm encodes against it, tools/audio/adpcm_check.py
 * validates with the Flycast model): no leak, low nibble first in mono. */
#ifndef FP_AICA_ADPCM_H
#define FP_AICA_ADPCM_H

#include <stdint.h>

static const int aica_step_table[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };

/* one 4-bit sample; history and step_size carry the decoder state of that channel */
static inline int16_t aica_nibble(int nib, int *history, int *step_size) {
    int sign = nib & 8, delta = nib & 7;
    int diff = ((1 + (delta << 1)) * *step_size) >> 3;
    int newval = *history;
    int nstep = (aica_step_table[delta] * *step_size) >> 8;
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

/* mono: bytes * 2 samples, low nibble first (wav2adpcm packs sample 0 into the low nibble, and the
 * AICA decodes it that way; reading the high nibble first swapped every sample pair, which is the
 * harsh, crackling version of the effects and the voices) */
static inline void aica_decode_mono(const uint8_t *in, int bytes, int16_t *out) {
    int hist = 0, sz = 127;
    for (int i = 0; i < bytes * 2; i++) {
        int nib = (i & 1) ? (in[i >> 1] >> 4) & 15 : in[i >> 1] & 15;
        out[i] = aica_nibble(nib, &hist, &sz);
    }
}

/* stereo: byte-interleaved (high nibble left, low nibble right), so one byte is one frame */
static inline void aica_decode_stereo(const uint8_t *in, int bytes, int16_t *out, int *hist, int *step) {
    for (int i = 0; i < bytes; i++) {
        out[i * 2 + 0] = aica_nibble((in[i] >> 4) & 15, &hist[0], &step[0]);
        out[i * 2 + 1] = aica_nibble(in[i] & 15, &hist[1], &step[1]);
    }
}

#endif
