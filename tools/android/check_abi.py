#!/usr/bin/env python3
"""The APK must carry a 32-bit library next to the 64-bit one.

Nothing here needs the NDK: it reads the four files that decide it and fails when one of them stops
agreeing with the others -- an abiFilters line that lost armeabi-v7a (gradle then simply does not build
it, and an old 32-bit tablet answers "app not installed"), a CI step that no longer checks the ABIs it
ships, a log that cannot say whether the build that crashed was the 32-bit one, or a Java side without
showMessage (the only way to explain a black window on a device that has no ES 3.0).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANDROID = ROOT / "android"
GRADLE = ANDROID / "app" / "build.gradle"
WORKFLOW = ROOT / ".github" / "workflows" / "android.yml"
PLAT = ROOT / "src" / "platform" / "android" / "plat_android.c"
ACTIVITY = ANDROID / "app" / "src" / "main" / "java" / "com" / "fpdc" / "port" / "FpActivity.java"
MANIFEST = ANDROID / "app" / "src" / "main" / "AndroidManifest.xml"

REQUIRED = ["arm64-v8a", "armeabi-v7a"]     # every APK: 64-bit phones first, 32-bit ones too
fails = 0


def bad(msg):
    global fails
    fails += 1
    print(f"  ABI CHECK FAILED: {msg}")


def read(path):
    if not path.is_file():
        bad(f"{path.relative_to(ROOT)} is missing")
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def main():
    gradle = read(GRADLE)
    m = re.search(r"abiFilters\s+([^\n]+)", gradle)
    if not m:
        bad("android/app/build.gradle has no abiFilters")
        abis = []
    else:
        abis = re.findall(r"'([a-z0-9_-]+)'", m.group(1))
    for abi in REQUIRED:
        if abi not in abis:
            bad(f"android/app/build.gradle does not build {abi} (it builds: {', '.join(abis) or 'nothing'})")
    if "arm64-v8a" not in abis:
        bad("the 64-bit ABI is the one every phone since 2019 uses")
    for abi in abis:
        if abi not in ("arm64-v8a", "armeabi-v7a", "x86_64", "x86"):
            bad(f"unknown ABI '{abi}' in abiFilters")

    workflow = read(WORKFLOW)
    for abi in REQUIRED:
        if abi not in workflow:
            bad(f".github/workflows/android.yml no longer checks that {abi} went into the APK")

    plat = read(PLAT)
    if "__arm__" not in plat or "armeabi-v7a" not in plat:
        bad("plat_android.c cannot name a 32-bit build: the log and ABOUT must say which ABI ran")
    if "sizeof(void *)" not in plat.replace("sizeof(void*)", "sizeof(void *)"):
        bad("plat_android.c does not report the pointer size of the build")

    activity = read(ACTIVITY)
    if "showMessage" not in activity:
        bad("FpActivity.java has no showMessage: a device without ES 3.0 gets a silent black window")

    manifest = read(MANIFEST)
    if "0x00030000" not in manifest:
        bad("the manifest no longer declares OpenGL ES 3.0 as required")

    if fails:
        return 1
    print(f"  abi: {', '.join(abis)} built, gated in CI, named in the log and ABOUT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
