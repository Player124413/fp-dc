#!/usr/bin/env python3
"""Pack the converted game data for the Android port.

The Android build does not bake the assets into the APK: the game reads them from the app's external
files folder, which the launcher fills from a zip you pick (IMPORT DATA).  This tool turns the same
`converted/` folder the Dreamcast build uses into that zip.

  python3 tools/android/make_android_data.py converted -o fpdc-data.zip

The zip root must be the data root (levels.txt at the top).  If the folder you pass contains a single
sub-folder with the data in it, that folder is used.

No Android device is needed here, and nothing is re-encoded: the port reads the same PAL8 / PAL4 /
ARGB1555 atlases, the AICA ADPCM sound effects and the ADPCM music the disc has.  ETC2 atlases are
optional (see README.md) and would be produced by a separate step; nothing here depends on them.
"""
import argparse
import os
import sys
import zipfile

REQUIRED = ['levels.txt']
HINT = {
    '.tex': 'textures', '.spr': 'sprite banks', '.lv2': 'levels', '.pal': 'palettes',
    '.wav': 'sound effects (AICA ADPCM)', '.adp': 'music (ADPCM 32 kHz stereo)',
    '.pcm': 'music (PCM16 22 kHz stereo)', '.dat': 'packs', '.txt': 'index files',
}


def find_root(path):
    if os.path.isfile(os.path.join(path, 'levels.txt')):
        return path
    kids = [d for d in os.listdir(path) if os.path.isdir(os.path.join(path, d))]
    if len(kids) == 1:
        sub = os.path.join(path, kids[0])
        if os.path.isfile(os.path.join(sub, 'levels.txt')):
            return sub
    return None


def walk(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            yield full, os.path.relpath(full, root).replace(os.sep, '/'), os.path.getsize(full)


def main():
    ap = argparse.ArgumentParser(description='build the Android data package (zip) from converted/')
    ap.add_argument('data', help='the converted data folder (the one with levels.txt)')
    ap.add_argument('-o', '--out', default='fpdc-data.zip', help='output zip (default: fpdc-data.zip)')
    ap.add_argument('--store', action='store_true', help='no compression (faster import, bigger file)')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    root = find_root(a.data)
    if not root:
        print('error: %s does not look like converted game data (no levels.txt)' % a.data, file=sys.stderr)
        return 2
    for req in REQUIRED:
        if not os.path.isfile(os.path.join(root, req)):
            print('error: %s is missing %s' % (root, req), file=sys.stderr)
            return 2

    files = list(walk(root))
    total = sum(n for _f, _r, n in files)
    by_kind = {}
    for _f, rel, n in files:
        kind = os.path.splitext(rel)[1].lower()
        # the sound effects live in sfx/, the music in bgm/, the ui atlases in ui/
        top = rel.split('/')[0] if '/' in rel else ''
        key = top if top in ('sfx', 'bgm', 'ui', 'gal', 'story') else HINT.get(kind, 'other')
        by_kind[key] = by_kind.get(key, 0) + n

    if not a.quiet:
        print('data: %s' % root)
        print('%d files, %.1f MB' % (len(files), total / 1048576.0))
        for k in sorted(by_kind, key=lambda k: -by_kind[k]):
            print('  %-28s %7.1f MB' % (k, by_kind[k] / 1048576.0))
    if by_kind.get('music (ADPCM 32 kHz stereo)', 0) == 0 and by_kind.get('bgm', 0) == 0:
        print('warning: no music in the package: the game will run silent between stages', file=sys.stderr)
    if by_kind.get('sfx', 0) == 0:
        print('warning: no sfx/ folder: the game will have no sound effects', file=sys.stderr)

    comp = zipfile.ZIP_STORED if a.store else zipfile.ZIP_DEFLATED
    tmp = a.out + '.part'
    with zipfile.ZipFile(tmp, 'w', comp, compresslevel=6 if not a.store else None) as z:
        for full, rel, _n in files:
            z.write(full, rel)
    os.replace(tmp, a.out)
    print('wrote %s: %.1f MB (%s)' % (a.out, os.path.getsize(a.out) / 1048576.0,
                                     'stored' if a.store else 'deflated'))
    print('copy it to the phone and use IMPORT DATA in the launcher')
    return 0


if __name__ == '__main__':
    sys.exit(main())
