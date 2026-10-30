#!/usr/bin/env python3
"""
Generates src/platform/android/ui_font.h: the tiny 5x7 bitmap font of the Android shell
(launcher, settings, control editor, touch labels).

The shell must draw text before any game data is available (the launcher also runs when the data
package has not been imported yet), so it cannot use the game's own font atlas: the glyphs are
compiled in.  One glyph is 5x7 pixels; ui_text() draws them as ui_rect() calls (GLES: one quad per
lit pixel, batched; software preview: one pixel each), which keeps both backends identical and
avoids a second texture path.  Lower-case letters map to the capitals (a 5x7 font has no room for
two cases, and every shell string is written in capitals).

usage: python3 tools/android/make_font.py            (rewrites the header; prints the glyph count)
       python3 tools/android/make_font.py --check    (exit 1 if the header is out of date)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'src', 'platform', 'android', 'ui_font.h')

# 35 characters per glyph: 7 rows of 5.  '#' = lit, '.' = clear.  Rows are separated by '/'.
GLYPHS = {
    ' ': '...../...../...../...../...../...../.....',
    '!': '..#../..#../..#../..#../..#../...../..#..',
    "'": '..#../..#../...../...../...../...../.....',
    '(': '...#./..#../..#../..#../..#../..#../...#.',
    ')': '.#.../..#../..#../..#../..#../..#../.#...',
    '*': '...../..#../#.#.#/.###./#.#.#/..#../.....',
    '+': '...../..#../..#../#####/..#../..#../.....',
    ',': '...../...../...../...../...../..#../.#...',
    '-': '...../...../...../#####/...../...../.....',
    '.': '...../...../...../...../...../...../..#..',
    '/': '....#/...#./...#./..#../.#.../.#.../#....',
    '0': '.###./#...#/#..##/#.#.#/##..#/#...#/.###.',
    '1': '..#../.##../..#../..#../..#../..#../.###.',
    '2': '.###./#...#/....#/...#./..#../.#.../#####',
    '3': '#####/...#./..##./....#/....#/#...#/.###.',
    '4': '...#./..##./.#.#./#..#./#####/...#./...#.',
    '5': '#####/#..../####./....#/....#/#...#/.###.',
    '6': '..##./.#.../#..../####./#...#/#...#/.###.',
    '7': '#####/....#/...#./..#../.#.../.#.../.#...',
    '8': '.###./#...#/#...#/.###./#...#/#...#/.###.',
    '9': '.###./#...#/#...#/.####/....#/...#./.##..',
    ':': '...../..#../...../...../...../..#../.....',
    '<': '...#./..#../.#.../#..../.#.../..#../...#.',
    '=': '...../...../#####/...../#####/...../.....',
    '>': '.#.../..#../...#./....#/...#./..#../.#...',
    '?': '.###./#...#/....#/...#./..#../...../..#..',
    '[': '.###./.#.../.#.../.#.../.#.../.#.../.###.',
    ']': '.###./...#./...#./...#./...#./...#./.###.',
    '_': '...../...../...../...../...../...../#####',
    '%': '##..#/##..#/...#./..#../.#.../#..##/#..##',
    '"': '.#.#./.#.#./...../...../...../...../.....',
    '#': '.#.#./#####/.#.#./#####/.#.#./...../.....',
    '$': '..#../.####/#.#../.###./..#.#/####./..#..',
    '&': '.##../#.#../.##../#.#.#/#..#./.##.#/.....',
    ';': '...../..#../...../...../..#../..#../.#...',
    '@': '.###./#...#/#.###/#.#.#/#.###/#..../.###.',
    '^': '..#../.#.#./#...#/...../...../...../.....',
    'A': '.###./#...#/#...#/#####/#...#/#...#/#...#',
    'B': '####./#...#/#...#/####./#...#/#...#/####.',
    'C': '.###./#...#/#..../#..../#..../#...#/.###.',
    'D': '####./#...#/#...#/#...#/#...#/#...#/####.',
    'E': '#####/#..../#..../####./#..../#..../#####',
    'F': '#####/#..../#..../####./#..../#..../#....',
    'G': '.###./#...#/#..../#.###/#...#/#...#/.###.',
    'H': '#...#/#...#/#...#/#####/#...#/#...#/#...#',
    'I': '.###./..#../..#../..#../..#../..#../.###.',
    'J': '..###/...#./...#./...#./...#./#..#./.##..',
    'K': '#...#/#..#./#.#../##.../#.#../#..#./#...#',
    'L': '#..../#..../#..../#..../#..../#..../#####',
    'M': '#...#/##.##/#.#.#/#...#/#...#/#...#/#...#',
    'N': '#...#/##..#/#.#.#/#..##/#...#/#...#/#...#',
    'O': '.###./#...#/#...#/#...#/#...#/#...#/.###.',
    'P': '####./#...#/#...#/####./#..../#..../#....',
    'Q': '.###./#...#/#...#/#...#/#.#.#/#..#./.##.#',
    'R': '####./#...#/#...#/####./#.#../#..#./#...#',
    'S': '.####/#..../#..../.###./....#/....#/####.',
    'T': '#####/..#../..#../..#../..#../..#../..#..',
    'U': '#...#/#...#/#...#/#...#/#...#/#...#/.###.',
    'V': '#...#/#...#/#...#/#...#/#...#/.#.#./..#..',
    'W': '#...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#',
    'X': '#...#/#...#/.#.#./..#../.#.#./#...#/#...#',
    'Y': '#...#/#...#/.#.#./..#../..#../..#../..#..',
    'Z': '#####/....#/...#./..#../.#.../#..../#####',
}

FIRST, LAST = 32, 126          # printable ASCII; unknown characters render as '?'
# Characters that share a glyph (a 5x7 font has no room for the rare ones).
ALIAS = {'"': "'", '`': "'", '\\': '/', '~': '-', '{': '(', '}': ')', '|': '!', '[': '[', ']': ']'}


def build():
    """rows[ch] = 7 bytes, bit 4 = leftmost pixel"""
    glyphs = {}
    for ch, art in GLYPHS.items():
        rows = art.split('/')
        if len(rows) != 7 or any(len(r) != 5 for r in rows):
            raise SystemExit('glyph %r: %d rows, widths %s' % (ch, len(rows), sorted({len(r) for r in rows})))
        if any(c not in '.#' for r in rows for c in r):
            raise SystemExit('glyph %r: only . and # allowed' % ch)
        glyphs[ord(ch)] = [sum(1 << (4 - i) for i, c in enumerate(r) if c == '#') for r in rows]
    missing = [chr(c) for c in range(FIRST, LAST + 1)
               if c not in glyphs and ord(chr(c).upper()) not in glyphs and c not in (ord(v) for v in ALIAS.values())
               and chr(c) not in ALIAS]
    if missing:
        raise SystemExit('no glyph for: %s' % ' '.join(missing))
    return glyphs


def emit(glyphs):
    out = []
    out.append('/* Generated by tools/android/make_font.py -- do not edit.\n'
               ' * 5x7 bitmap font of the Android shell (launcher, settings, control editor, touch labels):\n'
               ' * 95 printable ASCII characters, 7 rows of 5 bits each (bit 4 = leftmost pixel),\n'
               ' * lower case drawn as the capitals.  ui_text() (ui_gles.c / the preview) rasterizes it. */\n'
               '#ifndef FP_UI_FONT_H\n#define FP_UI_FONT_H\n\n'
               '#define UI_FONT_FIRST 32\n#define UI_FONT_LAST 126\n'
               '#define UI_FONT_W 5\n#define UI_FONT_H 7\n\n'
               'static const unsigned char ui_font_rows[UI_FONT_LAST - UI_FONT_FIRST + 1][UI_FONT_H] = {\n')
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        rows = (glyphs.get(code) or glyphs.get(ord(ch.upper()))
                or glyphs.get(ord(ALIAS.get(ch, '?'))) or glyphs[ord('?')])
        name = chr(code).replace('\\', '\\\\').replace("'", "\\'")
        out.append('    { %s },   /* %r */\n' % (', '.join('0x%02x' % r for r in rows), name))
    out.append('};\n\n#endif\n')
    return ''.join(out)


def main():
    text = emit(build())
    old = None
    if os.path.exists(OUT):
        with open(OUT, encoding='utf-8') as f:
            old = f.read()
    if '--check' in sys.argv:
        if old != text:
            print('ui_font.h is out of date: run tools/android/make_font.py')
            return 1
        print('ui_font.h is up to date (%d glyphs)' % (LAST - FIRST + 1))
        return 0
    if old != text:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, 'w', encoding='utf-8') as f:
            f.write(text)
        print('wrote %s (%d glyphs)' % (os.path.relpath(OUT), LAST - FIRST + 1))
    else:
        print('%s unchanged (%d glyphs)' % (os.path.relpath(OUT), LAST - FIRST + 1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
