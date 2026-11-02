#!/usr/bin/env python3
"""
Developer tool (session 25, Adventure mode): an RV trace of an original story scene (tools/orig/trace_story.py, run by
tools/orig/story_rv.sh with the reader STORY_READ) -> the port's scene script, committed in tables/story/scripts/<id>.scn.
End users never run this: the builder only copies the committed scripts (tools/import_story.py).

usage: story_rv2scn.py <trace.log> <scene id> <json frame> <dialog handle|-> <first frame> <end frame> <out.scn>
                       [--actors REGEX] [--drop REGEX] [--next KIND:ARG] [--title TEXT] [--from F]

The trace covers one visit of a story room: frames [first, end) of the log (the room's 'F' event to the next one).
Everything in the script is RV (read from the running original), turned into segments:

  segment 0 starts at <first>; each reader press 'A <P>' (J pressed with the dialog arrow shown) ends a segment, and the
  next one starts at P + 1, the frame the original reads the press (InputsP1, one frame late). Inside a segment the times
  are frames since its start. The port's player runs a segment's entries as they come; at its end it waits for the
  player's A (with the arrow shown) instead of the reader's 30 frames. A segment cut short by an early press applies the
  rest of its entries at once (positions / images snap to the original's state at the press).

The format (one entry per line; '#' comments) is documented in src/game/story.h ("Scene script").
"""
import json, os, re, sys, argparse

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# never actors: the DialogBox (its own entries), backdrops ('pv'), stage cards (the stage's own), fades ('f'), the player's
# hidden skins and helpers
DROP = re.compile(r'^(dialog|lipsync|levelparallax|levelcard|levelcardfade|fade|fadeedge|player_|playerskin(spade|torque)|'
                  r'playerreflect|playerdig|playerskintorquearm|whitefade|mouseobject|globalinputdata|ini_|highlighter|'
                  r'controlbox|savedata|mapdata|recorddata|guipause|crystal_447|sarassteam|windowcontrol|stagelist|'
                  r'masterlevel|playerassist|enemylimiter|sign_|link_|separator|objectcrystal_317|monitor|yellowspring|redspring|'
                  r'gimmickbridge|objectbike|thunderspark|effectswatersplash|ringsparkle|playerskincarolbike|playerskinmilla|'
                  r'effectfallingleaf)')   # ambient falling leaves (random sine paths: 36k entries a scene) are left out (INFERRED)

def load_dialog(handle):
    d = json.load(open(os.path.join(ROOT, 'tables', 'story', 'dialog.json'), encoding='utf-8'))
    return d['dialog'].get(str(handle), [])

# ---- actor positions (session 26): constant-speed runs as 'm', jumps as 'p', and paths that change by at most 40 px a
# frame for a while (a wobbling chain segment, a bobbing arm, a flying tail: thousands of 'p' / 'm' entries) as one 'mv'
# entry: two characters a frame, chr(79 + dx) chr(79 + dy). Lossless: the same positions frame by frame.
MV_MIN = 12                                      # position changes a path needs
def enc_pos(n, pos, put, seg_of):
    """pos: [(f, x, y)], the first one already written as 'p'"""
    i, N = 1, len(pos)
    def plain(lo, hi):                           # the states lo..hi-1 the old way ('m' runs of one delta, else 'p')
        run = None
        for k in range(lo, hi):
            f, x, y = pos[k]; pf, px, py = pos[k - 1]
            dx, dy = x - px, y - py
            if f == pf + 1 and run and run[1] == dx and run[2] == dy and run[0] + run[3] == f and seg_of(run[0]) == seg_of(f): run[3] += 1; continue
            if run: put(run[0], 1, 'm %d %d %d %d' % (n, run[1], run[2], run[3])); run = None
            if f == pf + 1: run = [f, dx, dy, 1]
            else: put(f, 0, 'p %d %d %d' % (n, x, y))
        if run: put(run[0], 1, 'm %d %d %d %d' % (n, run[1], run[2], run[3]))
    lo = 1
    while i < N:
        j = i                                   # a path from state i - 1: steps of at most 40 px, gaps of at most 8 frames, one segment
        sg = seg_of(pos[i - 1][0])
        while j < N and pos[j][0] - pos[j - 1][0] <= 8 and abs(pos[j][1] - pos[j - 1][1]) <= 40 and abs(pos[j][2] - pos[j - 1][2]) <= 40 \
                and seg_of(pos[j][0]) == sg and pos[j][0] - pos[i - 1][0] <= 30000:
            j += 1
        if j - i >= MV_MIN:
            plain(lo, i)
            f0 = pos[i - 1][0]; ch = []; k = i
            for f in range(f0 + 1, pos[j - 1][0] + 1):
                if pos[k][0] == f: dx, dy = pos[k][1] - pos[k - 1][1], pos[k][2] - pos[k - 1][2]; k += 1
                else: dx = dy = 0
                ch.append(chr(79 + dx) + chr(79 + dy))
            put(f0 + 1, 1, 'mv %d %d %s' % (n, len(ch), ''.join(ch)))
            lo = i = j
        else:
            i = max(j, i + 1)
    plain(lo, N)

# ---- session 26: the holo dragon rig (--rig). RE (PL6 13342-13344, 13390; src/game/pl_boss.c, spec_pl_bosses): every part keeps its
# placed offset from the neck (B, C); body scales (Scale2-4) ride the wave by their index from the tail (a = n - 1 - k); the head
# parts turn about the neck point by the tilt (upper jaw B - H, lower B + H); the hit spots sit on their scales (dy -24 / -18).
# RV r81_g0l: json 82's dragon follows it (the traced parts within 1 px of the prediction when the neck is still).
RIG_BODY = ('bossholodragonscale2', 'bossholodragonscale3', 'bossholodragonscale4')
RIG_HEAD = {'bossholodragonbreath': 5, 'bossholodragontoptooth': 3, 'bossholodragonbottomtooth': 4, 'bossholodragonscale': 3,
            'bossholodragonear': 2, 'bossholodragoneye': 2, 'bossholodragonbrow': 2}
RIG_SPOT = {'bossholodragonhitspot1': 0, 'bossholodragonhitspot2': 1, 'bossholodragonhitspot3': 2}
def rig_parts(a, fr, obj, actors, neck):
    """{addr: (kind, B, C, a)}: kind 1 body, 2 head (tilt B), 3 upper jaw (B - H), 4 lower jaw (B + H), 5 breath (no turn),
    6 hit spot (B = the scale's actor number, C = dy)"""
    import math
    pl = [o for o in fr['objects'] if o['cls'].startswith('bossholodragon')]
    nk = next((o for o in pl if o['cls'] == 'bossholodragonneck'), None)
    if not nk: return {}
    f0 = min(neck); al = neck[f0]
    A, D, E, F, H, J = (al.get(k, 0.0) for k in 'ADEFHJ'); s = -J
    sn = lambda d: math.sin(math.radians(d)); cs = lambda d: math.cos(math.radians(d))
    q = F * 0.25; Bt = sn(A + 100) * q - min(q, 20) - H; nx, ny = int(D), int(sn(A + 10) * F + E)
    def pred(cls, B, C, k_):
        if cls in RIG_BODY: return int(s * B + D), int(sn(15 * k_ + A - 135) * F + int(E + C))
        th = Bt - H if RIG_HEAD[cls] == 3 else Bt + H if RIG_HEAD[cls] == 4 else Bt
        return int((sn(th) * C + B * cs(th)) * s + nx), int(cs(th) * C - sn(th) * B + ny)
    out = {}; num = {addr: n for n, (_, addr) in enumerate(actors)}
    scales = {}                                  # (scale class, placement k) -> actor number
    for cls in list(RIG_BODY) + list(RIG_HEAD):
        ps = [o for o in pl if o['cls'] == cls]
        cand = [(addr, o_) for addr, o_ in obj.items() if o_['cls'].rsplit('_', 1)[0] == cls and addr in num]
        for k, o in enumerate(ps):
            B, C, k_ = o['x'] - nk['x'], o['y'] - nk['y'], len(ps) - 1 - k
            px, py = pred(cls, B, C, k_)
            best = None
            for addr, o_ in cand:
                if addr in out: continue
                fs = [f for f in sorted(o_['st']) if f >= f0][:1] or sorted(o_['st'])[-1:]
                x, y = o_['st'][fs[0]][:2]
                d = abs(x - px) + abs(y - py)
                if best is None or d < best[0]: best = (d, addr)
            if best and best[0] <= 4:
                out[best[1]] = (1 if cls in RIG_BODY else RIG_HEAD[cls], B, C, k_)
                if cls in RIG_BODY: scales[(cls, k)] = num[best[1]]
    for cls, g in RIG_SPOT.items():               # 13390: spot k of group g on scale k of Scale2 / 3 / 4 (the last one past the count)
        sc = RIG_BODY[g]; nsc = len([o for o in pl if o['cls'] == sc])
        cand = sorted((addr for addr, o_ in obj.items() if o_['cls'].rsplit('_', 1)[0] == cls and addr in num),
                      key=lambda ad: min(obj[ad]['st']))
        ps = [o for o in pl if o['cls'] == cls]
        for addr in cand:                         # the spot's placement: the scale it sits on, by its traced position at its first state
            f_ = min(obj[addr]['st']); x, y = obj[addr]['st'][f_][:2]
            best = None
            for k in range(len(ps)):
                ka = scales.get((sc, min(k, nsc - 1)))
                if ka is None: continue
                sa = actors[ka][1]; ff = [f for f in sorted(obj[sa]['st']) if f <= f_][-1:] or sorted(obj[sa]['st'])[:1]
                sx, sy = obj[sa]['st'][ff[0]][:2]
                d = abs(x - sx) + abs(y - (sy - (18 if g == 2 else 24)))
                if best is None or d < best[0]: best = (d, ka)
            if best and best[0] <= 8: out[addr] = (6, best[1], -18 if g == 2 else -24, 0)
    return out

def rig_entries(neck, put, a):
    """the neck's alterables as rig entries: rga <A x 10> <G x 100> (the phase, + G a frame mod 360), rgd <D> <I x 10> (the anchor
    x, + I a frame), rgp <E> <F> <H x 10>, rgj <J> <L> (the facing, the hologram's transparency: 255 invisible); written when
    the traced value leaves the per-frame progression"""
    pa = pd = pp = pj = None
    for f in sorted(neck):
        if f < a.first or f >= a.end: continue
        al = neck[f]; g = lambda k: al.get(k, 0.0)
        A, G, D, I = g('A'), g('G'), g('D'), g('I')
        if pa is None or abs(((pa[0] + pa[1]) % 360) - A) > 0.05 or G != pa[1]: put(f, 3, 'rga %d %d' % (round(A * 10), round(G * 100)))
        if pd is None or abs(pd[0] + pd[1] - D) > 0.05 or I != pd[1]: put(f, 3, 'rgd %d %d' % (round(D), round(I * 10)))
        P = (round(g('E')), round(g('F')), round(g('H') * 10)); Jl = (round(g('J')), round(g('L')))
        if P != pp: put(f, 3, 'rgp %d %d %d' % P)
        if Jl != pj: put(f, 3, 'rgj %d %d' % Jl)
        pa, pd, pp, pj = (A, G), (D, I), P, Jl

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log'); ap.add_argument('sid'); ap.add_argument('json', type=int); ap.add_argument('dialog')
    ap.add_argument('first', type=int); ap.add_argument('end', type=int); ap.add_argument('out')
    ap.add_argument('--actors', default=r'.', help='classes that may be actors (default: every traced class but the --drop ones)')
    ap.add_argument('--drop', default=r'^$', help='more classes left out (besides DROP: the DialogBox, backdrops, cards, fades, helpers)')
    ap.add_argument('--covers', action='store_true', help='STORY_BLEND trace: actor alpha (a) and the screen covers (fa) from the blend alpha')
    ap.add_argument('--split-reuse', action='store_true', help='groups A / B (session 25b): an address reused by another class is another '
                    'actor (as --by-class), a visible state flagged 0x2 still counts as a sighting, no actor is dropped for being '
                    'destroyed at the room start, parallax pieces are hidden by their own D only')
    ap.add_argument('--keep', default=r'^$', help='classes DROP would leave out that are actors here (e.g. playerskinmilla: Milla as an NPC)')
    ap.add_argument('--next', default='route')
    ap.add_argument('--title', default='')
    ap.add_argument('--chsel', default='', help='OPEN,PRESS: the in-scene character select opens at frame OPEN, the choice at PRESS')
    ap.add_argument('--rows', default='', help='extra rows class_handle:anim:dir,... (rows the trace did not show)')
    ap.add_argument('--caption', action='append', default=[], help='F0,F1,key|key: caption rows shown from F0 to F1 (RV screenshots)')
    ap.add_argument('--flash', default='', help='F,HOLD,N: white from frame F, HOLD frames, then a fade from white of N frames (RV screenshots; timing INFERRED)')
    ap.add_argument('--fadeout', default='', help='F,N: a fade to black of N frames from frame F (INFERRED: the wipe)')
    ap.add_argument('--rig', action='store_true', help='session 26: the holo dragon (BossHoloDragon*, its neck traced): its body / head parts and hit '
                    'spots follow the PL6 rig (RE 13342-13344 / 13390) from the neck\'s traced alterables (rig entries) instead of per-frame positions')
    ap.add_argument('--sub', action='append', default=[], help='session 26: F0,F1,KEY,Y: subtitle KEY (sub<handle>_<paragraph>) in the box at Y from F0 to F1 (RV SubtitleText_3449 visibility / position, the paragraph RE)')
    ap.add_argument('--mv', action='store_true', help='session 26: positions as lossless \'mv\' paths (two characters a frame) where they move often, '
                    '\'m\' runs / \'p\' elsewhere (without it: \'m\' / \'p\' only, as the earlier groups were converted)')
    ap.add_argument('--dialogbox', default='dialogbox_', help='the DialogBox class (prefix): rooms 15 / 16 also hold a hidden DialogBox_525 '
                    'helper, so a scene there passes dialogbox_551 (session 26)')
    ap.add_argument('--by-class', action='store_true', help='an object address freed and reused by another class is another actor '
                    '(without it the later objects\' states are merged into the first class, as arc 1 was converted)')
    ap.add_argument('--carry', action='store_true', help='a scene chained inside the trace that starts no music: the track the '
                    'original still plays (the last one before the window) starts with it (the port stops the music between scenes)')
    ap.add_argument('--room', type=int, default=0, help='a split story room (one room per scene of a large frame: st<room>, built '
                    'from <json frame> cropped to the scene\'s camera views; docs/ADVENTURE.md): rows.json gets json <room>, frame <json>, view')
    a = ap.parse_args()
    lines = load_dialog(a.dialog) if a.dialog != '-' else []
    INK = json.load(open(os.path.join(ROOT, 'tables', 'story', 'dialog.json'), encoding='utf-8')).get('ink', {})
    fr = json.load(open(os.path.join(ROOT, 'tables', 'frames', 'frame_%d.json' % a.json)))
    layer_of = {}
    zpos = {}          # draw order inside a layer: the frame's object order (Fusion draws later instances on top)
    for k_, o in enumerate(fr['objects']):
        layer_of.setdefault('%s_%d' % (o['cls'], o['id']), o.get('layer', 2)); zpos.setdefault('%s_%d' % (o['cls'], o['id']), k_)
    act_re, drop_re, keep_re = re.compile(a.actors), re.compile(a.drop), re.compile(a.keep)

    # ---- parse the window
    obj = {}           # object key (address/class/first frame) -> {'cls', 'st': {f: state}}
    par = {}           # parallax class -> {'st': {f: vis}, 'alt'}
    box = []           # (f, vis, E, F, C, img frame)
    snd, cams, presses, gone = [], [], [], {}
    par_addr = {}      # a parallax piece's address -> its class (a destroyed piece is hidden: 'pv h 0')
    neck = {}          # session 26: the holo dragon's neck, its alterables per frame (--rig)
    bgm_before = None  # the last music track started before the window (--carry)
    fade_a = {}        # LevelCardFade2 address -> {frame: alpha} (traces with the blend byte 'a=', tools/orig/trace_story.py)
    fade_d = {}        # address -> the frames it was destroyed (an address is reused by later instances)
    cur = {}           # an address's object key (address/class): the original reuses a destroyed object's memory. With --by-class
                       # (or --split-reuse) another class there is another actor (RV r76_g2_l: sparkle3_399 ->
                       # playerskincaroltail_448 at frame 1); the same class goes on as one actor. Without them (arc 1's scripts)
                       # the address keeps its first class. gone: object key -> destroyed at (in the window)
    reuse = a.by_class or a.split_reuse
    gone_raw = {}      # address -> destroyed at (whatever lived there: the parallax pieces' rule, arc 3)
    for l in open(a.log, encoding='latin1'):
        t = l.split()
        if not t or len(t) < 2 or not t[1].lstrip('-').isdigit(): continue
        f = int(t[1])
        if t[0] == 'D':
            if a.first <= f < a.end: gone[cur.get(t[2], t[2])] = f; gone_raw[t[2]] = f; fade_d.setdefault(t[2], []).append(f)
            continue
        if t[0] == 'O' and (cur.get(t[2], '/').split('/')[1] != t[3] if reuse else t[2] not in cur): cur[t[2]] = '%s/%s' % (t[2], t[3])
        if f < a.first or f >= a.end:
            if t[0] == 'O' and f < a.first and a.first == 0: pass   # (a trace that starts in the room: its first states)
            elif t[0] == 'C' and f < a.first: pass                  # the view before the window
            elif t[0] == 'S' and f < a.first and '/BGM/' in l.replace('\\', '/'): bgm_before = t[3]; continue   # (--carry)
            else: continue
        if t[0] == 'O':
            addr, cls = cur[t[2]], t[3]
            x, y, aid, afr, dr, fl = int(t[4]), int(t[5]), int(t[6]), int(t[7]), int(t[8]), int(t[9], 16)
            ia, ib, ic = int(t[10]), int(t[11]), int(t[12])
            alt = dict(kv.split('=') for kv in t[13:])
            if cls.startswith(a.dialogbox):
                box.append((f, fl & 1, int(float(alt.get('E', 0))), int(float(alt.get('F', 0))), int(float(alt.get('C', 0))), ic))
                continue
            if fl >= 0x10000: continue                  # not an object (old traces: a non-list member)
            if cls.startswith(('levelcardfade2', 'levelcardfade_')) and 'a' in alt:   # the black covers (0x2: being destroyed)
                fade_a.setdefault(t[2], {})[max(f, a.first)] = int(alt['a']) if (fl & 3) == 1 else 0
            if cls.startswith(('levelparallax', 'parallax')):   # backdrop pieces: visibility per scene ('pv'), scroll from D / E / F (RV)
                pp = par.setdefault(cls, {'st': {}, 'alt': alt}); par_addr[addr if a.split_reuse else t[2]] = cls
                pp['st'][max(f, a.first)] = fl & 1
                continue
            if a.rig and cls.startswith('bossholodragonneck_'): neck[max(f, a.first)] = {k_: float(v_) for k_, v_ in alt.items() if k_ != 'a'}
            o = obj.setdefault(addr, {'cls': cls, 'st': {}})
            o['st'][max(f, a.first)] = (x, y, ia, ib, ic, fl & 1, int((fl & 3) == 1), int(alt.get('a', 255)))
            # [6]: drawable (0x2: being destroyed); [7]: the blend alpha (STORY_BLEND traces)
        elif t[0] == 'S': snd.append((f, t[2], ' '.join(t[3:])))
        elif t[0] == 'C': cams.append((f, int(t[2]), int(t[3])))
        elif t[0] == 'A': presses.append(f)
    # segment starts
    chsel = [int(v) for v in a.chsel.split(',')] if a.chsel else None
    if chsel: presses = sorted(set(presses + [chsel[1]]))
    segs = [a.first] + [p + 1 for p in presses if a.first <= p < a.end - 1]
    def seg_of(f):
        k = 0
        while k + 1 < len(segs) and segs[k + 1] <= f: k += 1
        return k
    ev = [[] for _ in segs]                    # per segment: (t, order, text)
    def put(f, order, txt):
        k = seg_of(f); ev[k].append((f - segs[k], order, txt))

    # ---- actors: visible at least once in the window with a known image
    # the view per frame (an actor must be seen: a story room holds the props of all its scenes)
    camf = sorted((f, x, y) for f, x, y in cams)
    def view(f):
        v = (0, 0)
        for cf, x, y in camf:
            if cf > f: break
            v = (x, y)
        return v
    actors = []
    for addr, o in obj.items():
        c = o['cls']
        if drop_re.search(c) or (DROP.match(c) and not keep_re.search(c)) or not act_re.match(c.rsplit('_', 1)[0] + '_'): continue
        if not a.split_reuse and addr in gone and gone[addr] <= a.first + 2 and max(o['st']) <= a.first + 2: continue   # destroyed by the room's start events
        if re.match(r'^(levelcardfade|fadeedge|fade_)', c): continue          # fades: 'f' entries below
        fixed = layer_of.get(c, 2) >= 6
        seen = []
        fs_ = sorted(o['st'])
        for k_, f in enumerate(fs_):
            st_ = o['st'][f]
            if not st_[5 if a.split_reuse else 6] or st_[2] < 0: continue   # (a state flagged 0x2 is not a sighting; --split-reuse,
            # groups A / B as converted in session 25b: a visible one is: Spade's afterimages effectsspadeimage_456 carry 0x2)
            f2 = fs_[k_ + 1] if k_ + 1 < len(fs_) else a.end
            vs = [(0, 0)] if fixed else [view(f)] + [(x, y) for cf, x, y in camf if f < cf < f2]
            if any(vx - 48 < st_[0] < vx + 426 + 48 and vy - 48 < st_[1] < vy + 240 + 48 for vx, vy in vs): seen.append(f)
        if seen: actors.append((min(seen), addr))
    if len(actors) > 256:
        from collections import Counter
        sys.exit('%d actors (the player holds 256): narrow --actors / --drop: %s' % (len(actors), Counter(obj[a_]['cls'] for _, a_ in actors)))
    # actor number = draw order: by the frame's object order, runtime-created classes after the placed ones (creation
    # order); several instances of a class by first sighting (RV: comet2_4004 under the additive comet_4005)
    actors.sort(key=lambda fa: (zpos.get(obj[fa[1]]['cls'], 100000), fa[0], fa[1]))
    rows = set()
    used = {}          # (class, anim, dir) -> the frame indices the script shows (--covers: the room build keeps only those
                       # of a still anim, e.g. the skins' pose library anim 40: 108 frames, a scene shows a few)
    for r in filter(None, a.rows.split(',')):
        c_, an_, d_ = r.split(':'); rows.add((c_, int(an_), int(d_)))
    decl = []
    rigof = rig_parts(a, fr, obj, actors, neck) if a.rig and neck else {}
    for n, (_, addr) in enumerate(actors):
        o = obj[addr]; c = o['cls']; h = int(c.rsplit('_', 1)[1])
        lay = 2 if c.startswith(('playerskin', 'player_')) else layer_of.get(c, 2)   # the player's templates sit on layer 5 off-map
        ik = INK.get(c, {})                       # RE: create_<class> set_shader 0x20 = additive / blend colour alpha
        decl.append('actor %d %s %d%s' % (n, c, lay, ' add' if ik.get('shader') == 32 else ' a%d' % ik['alpha'] if 'alpha' in ik else ''))
        if addr in rigof: decl.append('rigpart %d %d %d %d %d' % ((n,) + rigof[addr]))
        pos = []                                     # --mv: the position changes (f, x, y) -> enc_pos
        prev = None; run = None                      # run: [f0, dx, dy, count]
        def flush_run():
            if run and run[3]: put(run[0], 1, 'm %d %d %d %d' % (n, run[1], run[2], run[3]))
        alpha0 = ik.get('alpha', 255)              # the declared ink; --covers (blend traces): the traced alpha wins ('a')
        for f in sorted(o['st']):
            x, y, ia, ib, ic, vis = o['st'][f][:6]; al = o['st'][f][7]
            if prev is None:
                put(f, 0, 'p %d %d %d' % (n, x, y)); put(f, 2, 'v %d %d' % (n, vis)); pos.append((f, x, y))
                if ia >= 0: put(f, 2, 'i %d %d %d %d' % (n, ia, ib, ic)); rows.add((c, ia, ib)); used.setdefault((c, ia, ib), set()).add(ic)
                if a.covers and al != alpha0: put(f, 2, 'a %d %d' % (n, al))
                prev = (f, x, y, ia, ib, ic, vis, al); continue
            pf, px, py, pia, pib, pic, pvis, pal = prev
            if a.covers and al != pal: put(f, 2, 'a %d %d' % (n, al))
            dx, dy = x - px, y - py
            if (dx or dy) and a.mv:                        # (--mv: enc_pos below; a rig part's position follows the rig)
                if addr not in rigof: pos.append((f, x, y))
            elif (dx or dy) and not a.covers:              # (--covers: the positions are fitted below, 'l' / 'p')
                if f == pf + 1 and run and run[1] == dx and run[2] == dy and run[0] + run[3] == f and seg_of(run[0]) == seg_of(f):
                    run[3] += 1
                elif f == pf + 1:
                    flush_run(); run = [f, dx, dy, 1]
                else:
                    flush_run(); run = None; put(f, 0, 'p %d %d %d' % (n, x, y))
            if (ia, ib, ic) != (pia, pib, pic) and ia >= 0:
                put(f, 2, 'i %d %d %d %d' % (n, ia, ib, ic)); rows.add((c, ia, ib)); used.setdefault((c, ia, ib), set()).add(ic)
            if vis != pvis: put(f, 2, 'v %d %d' % (n, vis))
            prev = (f, x, y, ia, ib, ic, vis, al)
        flush_run()
        if a.mv: enc_pos(n, pos, put, seg_of)
        if a.covers:                                   # piecewise-linear positions: 'l n x y k' reaches (x, y) in k frames, each
            fs_ = sorted(o['st']); f0 = fs_[0]         # frame within 1 px of the trace (RV positions are integers): drifting
            f1 = min(a.end, gone.get(addr, a.end))     # backdrops / bobbing ships cost one entry instead of one a frame
            P = []; k_ = 0
            for f in range(f0, f1):
                while k_ + 1 < len(fs_) and fs_[k_ + 1] <= f: k_ += 1
                P.append(o['st'][fs_[k_]][:2])
            i = 0
            while i < len(P) - 1:
                if P[i + 1] == P[i]:
                    i += 1; continue
                j = i + 1; best = i + 1; xi, yi = P[i]
                lo, hi = [-1e9, -1e9], [1e9, 1e9]       # the slopes (x, y) every frame so far allows (within 1 px)
                while j < len(P) and j - i <= 4000 and seg_of(f0 + j) == seg_of(f0 + i + 1):
                    N = j - i; sx, sy = (P[j][0] - xi) / N, (P[j][1] - yi) / N
                    if lo[0] <= sx <= hi[0] and lo[1] <= sy <= hi[1]: best = j   # ending here fits every frame before it
                    for ax, v0 in ((0, xi), (1, yi)):  # this frame as an inner one of a longer move
                        lo[ax] = max(lo[ax], (P[j][ax] - v0 - 1) / N); hi[ax] = min(hi[ax], (P[j][ax] - v0 + 1) / N)
                    if lo[0] > hi[0] or lo[1] > hi[1]: break
                    j += 1
                N = best - i
                if N == 1: put(f0 + best, 0, 'p %d %d %d' % (n, P[best][0], P[best][1]))
                else: put(f0 + i + 1, 1, 'l %d %d %d %d' % (n, P[best][0], P[best][1], N))
                i = best
        if addr in gone and (not a.by_class or gone[addr] >= max(o['st'])): put(gone[addr], 2, 'v %d 0' % n)
    if rigof: rig_entries(neck, put, a)
    # ---- fades: LevelCardFade2 (the black cover). Traces with its alpha: every fade (RV). Older traces: visible at the window's
    # start = a fade from black until it hides (RV visibility; the original's alpha ramp is not traced: linear, INFERRED)
    if fade_a and not a.covers:   # RV: the cover's alpha per frame (the black LevelCardFade2 instances, the most opaque one) -> linear
        cover = []     # fades over the same frames ('f -n' to black, 'f n' from black: ramps between <= 55 and >= 200; partial dims left out)
        for addr, ser in fade_a.items():
            for f in fade_d.get(addr, []): ser.setdefault(f, 0)      # destroyed: no cover until the address shows again
        for f in range(a.first, a.end):
            v = 0
            for addr, ser in fade_a.items():
                ks = [k for k in ser if k <= f]
                if ks: v = max(v, ser[max(ks)])
            cover.append(v)
        if cover and cover[0] >= 200: put(a.first, 3, 'f -1')
        k = 0
        while k < len(cover) - 1:
            if cover[k + 1] == cover[k]: k += 1; continue
            up = cover[k + 1] > cover[k]; j = k
            while True:                        # (a ramp may hold one value for up to 2 frames: B 64 -> 65 in c83_g4)
                while j < len(cover) - 1 and cover[j + 1] != cover[j] and (cover[j + 1] > cover[j]) == up: j += 1
                h = next((d for d in (2, 3) if j + d < len(cover) and cover[j + d] != cover[j]), 0)
                if h and all(cover[j + e] == cover[j] for e in range(1, h)) and (cover[j + h] > cover[j]) == up: j += h - 1; continue
                break
            if up and cover[k] <= 55 and cover[j] >= 200: put(a.first + k, 3, 'f %d' % -(j - k))
            elif not up and cover[k] >= 200 and cover[j] <= 55: put(a.first + k, 3, 'f %d' % (j - k))
            k = j
    for addr, o in ([] if a.covers else obj.items()):   # (--covers: 'fa' entries below)
        if fade_a or not o['cls'].startswith('levelcardfade2'): continue
        st = sorted(o['st'].items())
        if st and st[0][1][5]:
            off = next((f for f, s_ in st if not s_[5]), None)
            if off is not None and off - a.first < 120: put(a.first, 3, 'f %d' % (off - a.first))
    # ---- covers (--covers, STORY_BLEND traces): the LevelCardFade objects are black screen covers created at the view's
    # top-left (or at 0, 0 on the fixed layer 7) with a blend alpha ramp (RV b15_g11_l: 8 a frame, 32 frames). The covers
    # that sit on the view combine (1 - product of (1 - alpha)); 'fa' entries follow the result frame by frame.
    if a.covers:
        COV = re.compile(r'^(levelcardfade|whitefade|fade_)')
        cov = [(addr, o) for addr, o in obj.items() if COV.match(o['cls'])]
        fr_ = sorted({f for _, o in cov for f in o['st']} | {f for f in (gone.get(ad) for ad, _ in cov) if f is not None} |
                     {f for f, x, y in cams if a.first <= f < a.end} | {a.first})
        last = None; lastw = -999; fa_out = []
        for f in fr_:
            vx, vy = view(f); blk = wht = 1.0; wx = -999
            for addr, o in cov:
                ks = [k for k in o['st'] if k <= f]
                if not ks or (addr in gone and gone[addr] <= f): continue
                x, y, _, _, _, vis, _, al = o['st'][max(ks)]
                if vis and abs(y) <= 2 and -600 < x < -2 and al == 255: wx = max(wx, x)   # the black wipe sliding in (RV b76_g4_m 1807)
                if not vis or not ((abs(x) <= 2 and abs(y) <= 2) or (abs(x - vx) <= 2 and abs(y - vy) <= 2)): continue
                if o['cls'].startswith('whitefade'): wht *= 1 - al / 255.
                else: blk *= 1 - al / 255.
            cur_ = (round(255 * (1 - wht)), 1) if wht < 1 else (round(255 * (1 - blk)), 0)
            if cur_ != last: fa_out.append((f, 'fa %d%s' % (cur_[0], ' 1' if cur_[1] else ''))); last = cur_
            if wx != lastw: put(f, 3, 'fw %d' % wx); lastw = wx
        # A black cover that never lifts for the whole window cannot be what the original shows (the scene would be
        # invisible): that single write is the room's act fade caught at full black (rm5_spade b28_g0), so fade in.
        if len(fa_out) == 1 and fa_out[0][1] == 'fa 255': fa_out = [(fa_out[0][0], 'f 32')]
        for f_, s_ in fa_out: put(f_, 3, s_)
    for c in a.caption:
        f0, f1, keys = c.split(',', 2)
        for r, k in enumerate(keys.split('|')): put(int(f0), 3, 'cap %d %s' % (r, k))
        if int(f1) < a.end: put(int(f1), 3, 'capx')
    for c in a.sub:
        f0, f1, key, y = c.split(',')
        put(int(f0), 3, 'sub %s %s' % (key, y))
        if int(f1) < a.end: put(int(f1), 3, 'subx')
    if a.flash:
        ff, hh, nn = [int(v) for v in a.flash.split(',')]; put(ff, 3, 'f -1 1'); put(ff + hh, 3, 'f %d 1' % nn)
    if a.fadeout:
        ff, nn = [int(v) for v in a.fadeout.split(',')]; put(ff, 3, 'f %d' % -nn)
    if chsel:
        put(chsel[0], 6, 'chsel')
        k0 = seg_of(chsel[0])                                 # the idle wait inside the select: nothing of it is kept
        ev[k0] = [e for e in ev[k0] if e[0] <= chsel[0] - segs[k0]]
    # ---- parallax pieces: shown / hidden per scene (the room's other scenes use other sets)
    pinfo = {}
    g_ = gone if a.split_reuse else gone_raw   # (--split-reuse: by object key, so a D of the address's previous object is not the piece's)
    for addr, c in par_addr.items():           # destroyed at the room's start (RV c81_g2: 3223 / 3224 / 3225 / 3227 at frame 1)
        if addr in g_: par[c]['st'][max(g_[addr], a.first)] = 0
    for c, pp in par.items():
        h = int(c.rsplit('_', 1)[1]); prev = None
        for f in sorted(pp['st']):
            if pp['st'][f] != prev: put(f, 3, 'pv %d %d' % (h, pp['st'][f])); prev = pp['st'][f]
        al = pp['alt']
        pinfo[str(h)] = [float(al.get('D', 0)), float(al.get('E', 0)), float(al.get('F', 0))]
    # ---- camera
    cam0 = None
    for f, x, y in cams:
        if f < a.first: cam0 = (x, y); continue
        put(f, 3, 'c %d %d' % (x, y))
    if cam0 and not any(f == a.first for f, x, y in cams): put(a.first, 3, 'c %d %d' % cam0)   # (not over a view set at the start)
    # ---- dialogue: a line starts when the box shows with C 1 (18606 / 18607: F 1 -> 2, three characters)
    voiced = set(); said = []
    pv, pe = 0, -1
    for f, vis, E, F, C, img in box:
        if f < a.first: pv, pe = vis, E; continue
        if vis and F == 2 and C == 1 and (not pv or E != pe or True):
            if not said or said[-1][1] != E or said[-1][0] < f - 2:
                put(f, 4, 'say %d %d' % (E, img)); said.append((f, E))
                if 0 <= E < len(lines): voiced.add((lines[E][2], f))
        if pv and not vis: put(f, 4, 'close')
        pv, pe = vis, E
    vids = {v for v, _ in voiced}
    if a.carry:                                # the music the original still plays at the window's start (a chained scene that
        own = [f for f, k, v in snd if f == a.first and k == 'str' and '/BGM/' in v.replace('\\', '/')]   # starts no track)
        if bgm_before and not own: put(a.first, 5, 'u bgm/%s.adp' % bgm_before.replace('\\', '/').split('/')[-1][:-4])
    for f, kind, val in snd:
        if f < a.first: continue
        m = re.match(r'(\S+) ch(-?\d+) loop(\d+)$', val)
        what = m.group(1) if m else val
        if kind == 'id':
            if chsel and chsel[1] < f <= chsel[1] + 3 and what in ('63', '220', '221', '186'): continue   # the choice's own sounds (story.c)
            put(f, 5, 's %s' % what)
        else:
            b = what.replace('\\', '/').split('/')[-1]
            if '/BGM/' in what.replace('\\', '/'): put(f, 5, 'u bgm/%s.adp' % b[:-4])
            elif b[:-4] in vids: continue                       # the line's own voice (the player plays it with 'say')
            else: put(f, 5, 'o %s' % b[:-4])                    # a voice outside the dialogue
    out = ['# scene %s: RV, tools/story_rv2scn.py %s frames %d-%d (json frame %d)' % (a.sid, os.path.basename(a.log), a.first, a.end, a.json),
           'scene %s' % a.sid, 'room st%d' % (a.room or a.json), 'dialog %s' % a.dialog]
    if a.title: out.append('title %s' % a.title)
    out += decl
    for k, e in enumerate(ev):
        out.append('seg %d' % k)
        out += ['%d %s' % (t, txt) for t, o_, txt in sorted(e, key=lambda r: (r[0], r[1]))]
    out.append('end %d' % (a.end - segs[-1]))
    out.append('next %s' % a.next)
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f: f.write('\n'.join(out) + '\n')
    rp = os.path.join(ROOT, 'tables', 'story', 'rows.json')
    R = json.load(open(rp)) if os.path.exists(rp) else {}
    R[a.sid] = {'json': a.json, 'rows': sorted([c, ia, ib] for c, ia, ib in rows), 'par': pinfo}
    if a.room:                                # the views the scene shows (RV camera, 426 x 240 each): the split room's crop
        vs = [(x, y) for f, x, y in cams if a.first <= f < a.end]
        if cam0 and not any(f == a.first for f, x, y in cams): vs.insert(0, cam0)   # the view at the start (unless set right there)
        boxes = []                             # one box per camera run (a cut: a jump of more than 300 px starts another)
        for k, (x, y) in enumerate(vs):
            if k and abs(x - vs[k - 1][0]) <= 300 and abs(y - vs[k - 1][1]) <= 300:
                b = boxes[-1]; b[0], b[1], b[2], b[3] = min(b[0], x), min(b[1], y), max(b[2], x + 426), max(b[3], y + 240)
            else: boxes.append([x, y, x + 426, y + 240])
        R[a.sid].update({'json': a.room, 'frame': a.json, 'view': boxes})
    elif a.covers:                             # session 25b: the scene's view box (camera top-lefts + the screen): the room build
        vs = [(x, y) for f, x, y in cams if a.first <= f < a.end] + ([cam0] if cam0 else [])   # keeps only the pieces near it
        if vs: R[a.sid]['vbox'] = [min(x for x, y in vs), min(y for x, y in vs), max(x for x, y in vs) + 426, max(y for x, y in vs) + 240]
    with open(rp, 'w', encoding='utf-8', newline='\n') as f: json.dump(R, f, indent=0, sort_keys=True); f.write('\n')
    print('%s: %d actors, %d segments, %d lines said, %d rows, %d entries' % (a.sid, len(actors), len(segs), len(said), len(rows), sum(len(e) for e in ev)))

if __name__ == '__main__':
    main()
