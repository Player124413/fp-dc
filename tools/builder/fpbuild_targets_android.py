"""Android target of the builder: `fpbuild.py android <Assets.dat>` -> fpdc-data.zip.

Same data dir as the Dreamcast build (`build_data()` in fpbuild.py), but no disc image, no
mkdcdisc, no KallistiOS: the phone reads the plain files, so the zip is what the launcher's
IMPORT DATA button takes.

    python3 tools/builder/fpbuild.py android "/path/Freedom Planet/Assets.dat"
    -> build/builder/fpdc-data.zip   (copy it to the phone, IMPORT DATA, PLAY)

Nothing here needs the game to be converted twice: if you already built a CDI, the same
`build/builder/data` folder can be re-zipped with tools/android/make_android_data.py.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def build_zip(a, tools, fb):
    out = getattr(a, 'zip', None) or os.path.join(os.path.dirname(a.out), 'fpdc-data.zip')
    out = os.path.abspath(out)
    packer = os.path.join(REPO, 'tools', 'android', 'make_android_data.py')
    fb.LOG.say('[android] packing the data dir into the Android package ...')
    r = subprocess.run([sys.executable, packer, a.out, '-o', out], cwd=REPO,
                       stdout=fb.LOG.f, stderr=subprocess.STDOUT, env=fb.child_env())
    if r.returncode:
        raise fb.Fail('packing the Android data package failed (log: %s)' % fb.LOG.path)
    fb.LOG.say('DONE: %s (%.0f MB). Copy it to the phone, then IMPORT DATA in the launcher.'
               % (out, os.path.getsize(out) / 1e6))


def ADD_ARGS(p):
    p.add_argument('--zip', default=None,
                   help='output package (default: build/builder/fpdc-data.zip)')


TARGETS = {'android': ('the Android data package (fpdc-data.zip)', False, build_zip)}
