"""构建后自动把应用固件复制为 dist/PodcastPlayer.bin（供 M5Launcher 使用）。

M5Launcher 只接受单个应用镜像（firmware.bin），不做 0x0 合并 bin。
"""
Import("env")  # noqa: F405  SCons global provided by PIO

import os
import shutil


def _copy_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    out_dir = os.path.join(project_dir, "dist")
    os.makedirs(out_dir, exist_ok=True)
    src = os.path.join(build_dir, "firmware.bin")
    dst = os.path.join(out_dir, "PodcastPlayer.bin")
    shutil.copyfile(src, dst)
    print("[deploy] %s -> %s" % (src, dst))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _copy_firmware)
