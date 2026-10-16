"""构建后把应用固件复制到 dist/（文件名按编译环境区分）。

M5Launcher 只接受单个应用镜像（firmware.bin），不做 0x0 合并 bin。
"""
Import("env")  # noqa: F405  SCons global provided by PIO

import os
import shutil

# 编译环境 -> dist 输出文件名
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
