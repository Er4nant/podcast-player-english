"""Copy the application firmware to dist/ after the build (the filename varies by build environment).

M5Launcher only accepts a single application image (firmware.bin); it does not use a 0x0 merged bin.
"""
Import("env")  # noqa: F405  SCons global provided by PIO

import os
import shutil

# build environment -> dist output filename
OUTPUT_NAMES = {
    "cardputer-adv-podcast": "PodcastPlayer.bin",
    "m5stick-s3-podcast": "PodcastPlayer_StickS3.bin",
}


def _copy_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    out_dir = os.path.join(project_dir, "dist")
    os.makedirs(out_dir, exist_ok=True)
    name = OUTPUT_NAMES.get(env["PIOENV"], "PodcastPlayer_%s.bin" % env["PIOENV"])
    src = os.path.join(build_dir, "firmware.bin")
    dst = os.path.join(out_dir, name)
    shutil.copyfile(src, dst)
    print("[deploy] %s -> %s" % (src, dst))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_firmware)
