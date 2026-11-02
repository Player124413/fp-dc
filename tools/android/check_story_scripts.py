#!/usr/bin/env python3
"""story scripts: a scene must not open behind an opaque cover that never lifts.

rm5_spade (RV b28_g0) carried exactly one cover write, `0 fa 255`: the trace caught the room's act fade at full
black, so the whole cutscene played with its picture hidden (music and voices only, i.e. a black screen). Every
other scene opens uncovered or lifts its cover within the fade's 32 frames (the RV cover ramp, 8 alpha a frame).

This follows cutscene.c's `csx_load` over every script and fails when the cover ending up fully opaque has no
later command that lowers it - a scene that can never show its picture. A scene that fades OUT to black at its
end is fine: nothing lowers the cover there either, but the cover was not opaque at the scene's first frame.

The parse is a checker's, not the player: only the cover state and the op set are modelled.
"""
import glob
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SCRIPTS = os.path.join(ROOT, 'tables', 'story', 'scripts')
# cutscene.c's csx_load: the ops it knows (a numbered line with any other name is dropped there)
OPS = {'p', 'm', 'l', 'i', 'v', 'a', 'fa', 'fw', 'c', 'say', 'close', 's', 'f', 'chsel', 'capx', 'subx', 'mv', 'u', 'o',
       'rga', 'rgd', 'rgp', 'rgj', 'sub', 'pv', 'cap', 'rigpart', 'seg', 'end'}


def walk(path):
    """([(seg, t, kind, v) for the cover in play order], the cover at the scene's first frame, error text)"""
    cover, first, seg = [], None, -1
    for ln in open(path, encoding='utf-8', errors='replace'):
        ln = ln.strip()
        if not ln or ln.startswith('#'):
            continue
        t = ln.split()
        if not t[0][0].isdigit():                       # the header: scene / room / dialog / actor / next / end / seg
            if t[0] == 'seg':
                seg += 1
            continue
        if t[0][0] == '-':                              # csx_load rejects a negative frame
            return ops, cover, first, 'negative frame %r' % t[0]
        op = t[1] if len(t) > 1 else ''
        if op not in OPS:
            return cover, first, 'unknown op %r in %r' % (op, ln)
        if op in ('fa', 'f'):
            v = int(t[2]) if len(t) > 2 else 0
            cover.append((max(seg, 0), int(t[0]), 'set' if op == 'fa' else 'ramp', v))
            if first is None:
                first = min(255, max(0, v)) if op == 'fa' else (255 if v > 0 else 0)
    return cover, first, None


def main():
    bad, note, n = [], [], 0
    for path in sorted(glob.glob(os.path.join(SCRIPTS, '*.scn'))):
        n += 1
        name = os.path.basename(path)
        cover, first, err = walk(path)
        if err:
            bad.append('%s: %s' % (name, err))
            continue
        if not cover or first is None or first < 250:
            continue                                     # opens with a visible picture
        lift = next(((sg, t) for sg, t, kind, v in cover
                     if (kind == 'set' and v < 250) or (kind == 'ramp' and v > 0)), None)
        if lift is None:
            bad.append('%s: the cover is opaque at the first frame and nothing lowers it '
                       '(the scene stays a black screen)' % name)
        elif lift[0] > 0 or lift[1] > 120:
            note.append('%s: the picture only comes in at segment %d frame %d' % (name, lift[0], lift[1]))
    for w in note:
        print('  note: %s' % w)
    if bad:
        for b in bad:
            print('  FAILED %s' % b)
        return 1
    print('  story scripts: %d scenes open with a visible picture (an opaque cover always has a lift)' % n)
    return 0


if __name__ == '__main__':
    sys.exit(main())
