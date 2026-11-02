#!/usr/bin/env python3
"""
Adventure mode story data (session 25): tables/story/ -> <out>/story/ + the story rooms <out>/st<json>/.

usage: import_story.py <out data dir> [--rooms-only | --no-rooms] [-j N]
  FP_FRAMES / FP_IMAGES: the frame tables and the extracted images (the builder sets them; default the lead's tree)

Inputs (all committed, read from the Linux 1.21.5 program once: tables/README.md):
  tables/story/scenes.json   the scene inventory: id, the original's debug name, room (json frame), g4, trigger, next / skip
                             steps, characters, status ("ported" = a script exists in tables/story/scripts)
  tables/story/scripts/<id>.scn   the ported scenes' scripts (tools/story_rv2scn.py from RV traces; format src/game/story.h)
  tables/story/dialog.json   every DialogData object's paragraphs (tools/re/dump_dialog.py)
  tables/story/rows.json     the sprite rows each script draws (tools/import_frame.py story_rows puts them in st<json>_ens)
Outputs:
  story/story.txt            the scene table the game reads (only the ported scenes; src/game/story.c)
  story/inline.txt           the in-stage scenes (next 'back': played over the loaded stage, no room; session 25b)
  story/<id>.scn             the scripts
  story/captions.txt         the scene captions ("Three days later," ...): '<str_ symbol> <text>' lines
  story/dlg_<handle>.txt     the paragraphs of each DialogData object a script uses, one per line (Latin-1, as the original)
  st<room>/                  every story room a ported scene plays in, built like a level (tools/build_level.py): room = the
                             json frame, or a split room of a large frame (scenes.json 'frame': frame 83 -> st83<list>)
Voices (story/vo/<voice>.wav) are converted by tools/audio/convert_audio.py story (the builder's voice step).
Host-only tool (FP_HOST_ENV world): nothing here runs on the Dreamcast.
"""
import concurrent.futures, json, os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
TABLES = os.path.join(ROOT, 'tables', 'story')

def scenes():
    return json.load(open(os.path.join(TABLES, 'scenes.json'), encoding='utf-8'))['scenes']

def ported(sc):
    return [s for s in sc if os.path.isfile(os.path.join(TABLES, 'scripts', s['id'] + '.scn'))]

def step(s):
    return s if s else 'menu'

def sids():
    """tables/story/sids.json: the permanent scene numbers (seen flags, saved story position; append-only)"""
    return json.load(open(os.path.join(TABLES, 'sids.json'), encoding='utf-8'))['sid']

def sid_for(s, sid):
    """a scene's number: its own, or (a character variant: scenes.json 'sid_of', session 25b) its base scene's. A variant is the
    same story scene staged for another character (same json frame and g4, story.c chr_variant): it shares the seen flag and
    the story position, so the 64 numbers are not spent on variants"""
    if s['id'] in sid: return sid[s['id']]
    if s.get('sid_of') in sid: return sid[s['sid_of']]
    sys.exit('import_story: scene %s has no number in tables/story/sids.json (append the next free one, or name its base '
             'scene in sid_of when it is a character variant)' % s['id'])

def inline(s): return s.get('next') == 'back'      # an in-stage scene (session 25b: RM5's Adventure dialogue): story/inline.txt, no room

def write_table(out, sc, which='story'):
    rows = ['# Adventure story scenes (tools/import_story.py from tables/story/scenes.json): id room g4 next sid= [skip=] [stage=] [panel=] [trig=] [chr=]']
    if which == 'inline': rows[0] = '# in-stage scenes (played over the stage that is loaded, story.c story_inline_begin): same columns; room = the stage\'s json frame'
    sid = sids()
    for s in ported(sc):
        if inline(s) != (which == 'inline'): continue
        r = '%s %d %d %s sid=%d' % (s['id'], s['room'], s['g4'], step(s.get('next')), sid_for(s, sid))
        if s.get('skip') and s['skip'] != s.get('next'): r += ' skip=%s' % s['skip']
        if s.get('stage'): r += ' stage=%s' % s['stage']
        r += ' panel=%d' % s.get('panel', 1)
        for t in s.get('triggers', []): r += ' trig=%s' % t
        if s.get('chr', 7) != 7: r += ' chr=%d' % s['chr']
        rows.append(r)
    with open(os.path.join(out, 'story', which + '.txt'), 'w', encoding='utf-8', newline='\n') as f: f.write('\n'.join(rows) + '\n')
    return len(rows) - 1

def dialog_handles(sc):
    hs = set()
    for s in ported(sc):
        for l in open(os.path.join(TABLES, 'scripts', s['id'] + '.scn'), encoding='utf-8'):
            if l.startswith('dialog ') and l.split()[1] != '-': hs.add(l.split()[1])
    return sorted(hs, key=int)

def main():
    a = sys.argv[1:]
    if not a: sys.exit(__doc__)
    out = os.path.abspath(a[0]); jobs = int(a[a.index('-j') + 1]) if '-j' in a else 2
    sc = scenes()
    os.makedirs(os.path.join(out, 'story'), exist_ok=True)
    if '--rooms-only' not in a:
        n = write_table(out, sc); n += write_table(out, sc, 'inline')
        for s in ported(sc):
            shutil.copyfile(os.path.join(TABLES, 'scripts', s['id'] + '.scn'), os.path.join(out, 'story', s['id'] + '.scn'))
        # D-136: the scripts travel inside the data pack, so a script fix only reaches a device with a re-imported pack.
        # stamp.txt names the tree the pack was built from (commit, scene count, a hash of every script); src/game/story.c
        # logs it and each scene's own crc, which is what tells "old pack" from "new bug".
        import hashlib
        h = hashlib.sha1()
        for s in sorted(ported(sc), key=lambda s: s['id']):
            h.update(open(os.path.join(TABLES, 'scripts', s['id'] + '.scn'), 'rb').read())
        try:
            com = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, capture_output=True, text=True).stdout.strip() or '?'
        except OSError:
            com = '?'
        with open(os.path.join(out, 'story', 'stamp.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('build %s, %d scenes, scripts %s\n' % (com, len(ported(sc)), h.hexdigest()[:12]))
        D = json.load(open(os.path.join(TABLES, 'dialog.json'), encoding='utf-8'))['dialog']
        for h in dialog_handles(sc):
            with open(os.path.join(out, 'story', 'dlg_%s.txt' % h), 'w', encoding='latin-1', newline='\n') as f:
                for spk, anim, vo, text in D[h]:
                    f.write('%02d %02d %s %s\n' % (spk, anim, vo, text.replace('\n', ' ')))
        caps = json.load(open(os.path.join(TABLES, 'dialog.json'), encoding='utf-8')).get('captions', {})
        with open(os.path.join(out, 'story', 'captions.txt'), 'w', encoding='latin-1', newline='\n') as f:
            for k in sorted(caps): f.write('%s %s\n' % (k, caps[k]))
            subs = json.load(open(os.path.join(TABLES, 'dialog.json'), encoding='utf-8')).get('subtitles', {})
            for h in sorted(subs, key=int):                   # session 26: SubtitleData paragraphs, keys sub<handle>_<k> ('sub' entries)
                for k, t in enumerate(subs[h]): f.write('sub%s_%d %s\n' % (h, k, t.replace('\n', ' ')))
        print('story: %d scenes of %d ported, %d dialogue objects' % (n, len(sc), len(dialog_handles(sc))))
    if '--no-rooms' not in a:
        # a room is st<room> built from json frame <room>, or from 'frame' for a split room (one per scene of a large frame:
        # frame 83's scenes are st83<list>, cropped to their views by tools/import_frame.py; docs/ADVENTURE.md); in-stage scenes
        # have none
        rooms = sorted({(s['room'], s.get('frame', s['room'])) for s in ported(sc) if not inline(s)})
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
        def one(rf):
            subprocess.run([sys.executable, '-B', os.path.join(ROOT, 'tools', 'build_level.py'), str(rf[1]), 'st%d' % rf[0], out],
                           check=True, env=env, stdout=subprocess.DEVNULL)
            return rf[0]
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            for j in ex.map(one, rooms): print('story room st%d built' % j)
        # build_level adds every built dir to levels.txt (the developer select): the story rooms are no stages
        lt = os.path.join(out, 'levels.txt')
        if os.path.isfile(lt):
            keep = [l for l in open(lt, encoding='utf-8').read().split('\n') if l and not (l.startswith('st') and l[2:].isdigit())]
            with open(lt, 'w', encoding='utf-8', newline='\n') as f: f.write(''.join(l + '\n' for l in keep))

if __name__ == '__main__':
    main()
