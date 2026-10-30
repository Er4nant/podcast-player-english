# Pre-build patch: enlarge the ESP32-audioI2S decoder task stack from 3300 bytes to 8192.
# The AAC(m4a) decode call chain is deep; 3300 bytes overflows the stack after a few minutes
# of playback and causes a hard crash.
# libdeps installs a separate library copy per build environment; this script idempotently
# patches every installed copy.
# (On a fresh dependency install this script runs before the dependencies are downloaded,
#   so the patch does not take effect that time — just build once more; libdeps is in place
#   and the patch is applied.)
import glob
import io
import os

MARK = "// [PodcastPlayer patch] decoder task stack 3300 -> 8192"


def patch_file(path):
    with io.open(path, "r", encoding="utf-8") as f:
        src = f.read()
    if MARK in src:
        return False  # already patched
    target = '"PeriodicTask", 3300,'
    if target not in src:
        print("[patch_audio_stack] WARN: target not found in %s" % path)
        return False
    src = src.replace(
        target,
        MARK + '\n    xTaskCreate(&Audio::taskWrapper, "PeriodicTask", 8192,',
        1,
    )
    with io.open(path, "w", encoding="utf-8") as f:
        f.write(src)
    return True


def main():
    base = os.path.join(".pio", "libdeps")
    hits = glob.glob(
        os.path.join(base, "*", "ESP32-audioI2S", "src", "Audio.cpp")
    )
    if not hits:
        print("[patch_audio_stack] no ESP32-audioI2S copies found yet")
        return
    for p in hits:
        changed = patch_file(p)
        print("[patch_audio_stack] %s: %s" % (p, "patched" if changed else "ok/skip"))


if __name__ == "__main__":
    main()
