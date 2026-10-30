/* The Dreamcast texture swizzle, inverted: where the texel (x, y) of a w x h 8-bit image sits in the
 * pre-twiddled block order (inverse of tools/twiddle_disc.py's perm()).
 *
 * The layout is the PowerVR "square twiddle" of KOS's pvr_txr_load_ex(..., PVR_TXRLOAD_8BPP): the
 * picture is cut into min(w, h)^2 blocks; inside a block the x index and the y index of an even row
 * pair are bit-interleaved (x in the even bits, y / 2 in the odd bits), and the two bytes of a
 * column step are adjacent, the odd row one byte after the even one. */
#include <stdint.h>
#include "twiddle.h"

static uint32_t twid10(uint32_t v) {
    uint32_t r = 0;
    for (int b = 0; b < 10; b++) r |= ((v >> b) & 1u) << (2 * b);
    return r;
}

size_t fp_untwiddle_offset(int x, int y, int w, int h) {
    int mn = w < h ? w : h, mask = mn - 1;
    uint32_t t = twid10((uint32_t)(x & mask)) | (twid10((uint32_t)((y & mask) >> 1)) << 1);
    return (size_t)(2 * t) + (size_t)((x / mn) + (y / mn)) * (size_t)(mn * mn) + (size_t)(y & 1);
}
