#!/usr/bin/env python3
"""Verify fp_untwiddle_offset() (src/renderer/twiddle.c) against tools/twiddle_disc.py.

The Dreamcast build ships pre-twiddled 8-bit atlases (FPTX fmt 2): the disc-side converter wrote them
with pvr_txr_load_ex(PVR_TXRLOAD_8BPP), and the Android renderer has to undo that swizzle before it
uploads the indices to GL.  Both directions must agree byte for byte, otherwise every fmt 2 atlas
would come out scrambled.

  python3 tools/android/check_twiddle.py            # every shape the builder produces
  python3 tools/android/check_twiddle.py --verbose

Needs gcc and ctypes (no NDK, no GL)."""
import ctypes
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import twiddle_disc   # noqa: E402

SHAPES = [(64, 64), (128, 64), (64, 128), (128, 128), (256, 128), (128, 256),
          (512, 256), (256, 512), (512, 512), (1024, 512), (512, 1024), (1024, 1024),
          (16, 16), (32, 8), (8, 32)]


def build(tmp):
    so = os.path.join(tmp, 'twiddle.so')
    src = os.path.join(ROOT, 'src', 'renderer', 'twiddle.c')
    subprocess.run(['gcc', '-shared', '-fPIC', '-O2', '-o', so, src], check=True)
    lib = ctypes.CDLL(so)
    lib.fp_untwiddle_offset.restype = ctypes.c_size_t
    lib.fp_untwiddle_offset.argtypes = [ctypes.c_int] * 4
    return lib


def check(lib, w, h):
    # perm(w, h) is the itemgetter that turns a linear picture into the disc's byte order:
    # applied to the identity it gives, for every output byte, the source texel
    order = twiddle_disc.perm(w, h)(list(range(w * h)))
    where = [0] * (w * h)
    for k, src in enumerate(order):
        where[src] = k
    bad = 0
    for y in range(h):
        for x in range(w):
            got = lib.fp_untwiddle_offset(x, y, w, h)
            want = where[y * w + x]
            if got != want:
                if bad < 5:
                    print('  %dx%d texel (%d,%d): C says %d, the disc layout says %d' % (w, h, x, y, got, want))
                bad += 1
    return bad


def main():
    verbose = '--verbose' in sys.argv
    with tempfile.TemporaryDirectory() as tmp:
        lib = build(tmp)
        total = 0
        for w, h in SHAPES:
            bad = check(lib, w, h)
            total += bad
            if verbose or bad:
                print('%4dx%-4d %s' % (w, h, 'ok' if not bad else '%d texels wrong' % bad))
        if total:
            print('FAILED: %d texels disagree with tools/twiddle_disc.py' % total)
            return 1
    print('twiddle: %d shapes match the disc layout' % len(SHAPES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
