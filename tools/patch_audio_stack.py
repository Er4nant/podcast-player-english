# 构建前置补丁：把 ESP32-audioI2S 解码任务栈从 3300 字节扩到 8192。
# AAC(m4a) 解码调用链很深，3300 字节会在播放数分钟后栈溢出导致硬崩溃。
# libdeps 按编译环境各装一份库，此脚本对已安装的所有副本幂等打补丁。
# （首次全新安装依赖时本脚本先于依赖下载运行，那一次补丁不会生效——
#   再跑一次构建即可，libdeps 已就位，补丁随之应用。）
import glob
import io
import os

MARK = "// [PodcastPlayer patch] decoder task stack 3300 -> 8192"


def patch_file(path):
    with io.open(path, "r", encoding="utf-8") as f:
        src = f.read()
    if MARK in src:
        return False  # 已打过
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
