/* Byte offset of texel (x, y) inside a pre-twiddled 8-bit image (FPTX fmt 2, the layout KOS's
 * pvr_txr_load_ex(PVR_TXRLOAD_8BPP) expects and tools/twiddle_disc.py writes).  Kept in its own
 * translation unit so the host test (tools/android/check_twiddle.py) can verify it against the
 * Python without a GL context. */
#ifndef FP_TWIDDLE_H
#define FP_TWIDDLE_H

#include <stddef.h>

size_t fp_untwiddle_offset(int x, int y, int w, int h);

#endif
