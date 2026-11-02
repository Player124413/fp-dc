#!/usr/bin/env python3
"""story scripts: a scene must not open behind an opaque cover that never lifts.

rm5_spade (RV b28_g0) carried exactly one cover write, `0 fa 255`: the trace caught the room's act fade at full
black, so the whole cutscene played with its picture hidden (music and voices only, i.e. a black screen). Every
other scene opens uncovered or lifts its cover within the fade's 32 frames (the RV cover ramp, 8 alpha a frame).

This follows cutscene.c's `csx_load` over every script and fails when the cover ending up fully opaque has no
later command that lowers it - a scene that can never show its picture. A scene that fades OUT to black at its
end is fine: nothing lowers the cover there either, but the cover was not opaque at the scene's first frame.

Second pass, informational: the sprite rows the scripts ask for but the committed animation table cannot build
(tools/import_frame.py story_rows skips them with "story: missing ..."), so the actor is not drawn while such a
row is active (cutscene.c: `row < 0` is skipped). These are a one-time RE dump's holes in cutscene-only effect
classes, not a regression: reported, never fatal -- `--rows` lists them.

The parse is a checker's, not the player: only the cover state, the op set and a row lookup are modelled.
"""
import glob
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SCRIPTS = os.path.join(ROOT, 'tables', 'story', 'scripts')
ROWS = os.path.join(ROOT, 'tables', 'story', 'rows.json')
ANIMS = os.path.join(ROOT, 'tables', 'frames', 'anims.json')
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


def buildable(anims, key, anim, d):
    """import_frame.py story_rows: an[cls]['anims'][anim][dir]'s frames (a direction 16+ row may ride the mirror alias of
    its direction-0+ base). The alias also needs the pixels to match, which is not checked here: permissive."""
    a = anims.get(key) or {}
    al = a.get('anims') or []
    for d_ in (d, d - 16 if d >= 16 else None):
        if d_ is None or anim >= len(al) or not isinstance(al[anim], dict):
            continue
        e = al[anim].get(str(d_))
        if isinstance(e, dict) and e.get('frames'):
            return True
    return False


def rows_audit(verbose):
    if not (os.path.isfile(ROWS) and os.path.isfile(ANIMS)):
        return
    rows = json.load(open(ROWS, encoding='utf-8'))
    anims = json.load(open(ANIMS, encoding='utf-8'))
    total = miss = 0
    per = []
    for name in sorted(rows):
        bad = [(k, a, d) for k, a, d in rows[name].get('rows', []) if not buildable(anims, k, a, d)]
        total += len(rows[name].get('rows', []))
        miss += len(bad)
        if bad:
            per.append((name, bad))
    print('  story rows: %d of %d rows build (the animation table has no entry for the rest: those sprites are not drawn)'
          % (total - miss, total))
    for name, bad in per:
        if verbose:
            for k, a, d in bad:
                print('    %s: %s anim %d dir %d' % (name, k, a, d))
        else:
            print('    %-22s %d row(s)' % (name, len(bad)))


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
    rows_audit('--rows' in sys.argv)
    return 0


if __name__ == '__main__':
    sys.exit(main())
