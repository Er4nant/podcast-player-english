# -*- coding: utf-8 -*-
"""PlatformIO 预构建脚本：读 channels.txt，生成 src/channels.h（内嵌兜底频道）。

频道维护方式（无需改 C++ 代码）：
  1. 编辑仓库根目录 channels.txt，每行一个频道：频道名|RSS地址
  2. 正式使用时把同样格式放到 SD 卡根目录（免烧录，优先级更高）
  3. 重新编译即生效

封面是运行时按频道名自动生成的文字徽章（渐变 + 大字），无图片资源。
生成失败（txt 格式错）会直接抛异常终止构建，避免烧进坏数据。
"""
import os

# 定位项目根：SCons exec 执行预脚本时 __file__ 未必存在，
# 优先用 PlatformIO 注入的 env 与 $PROJECT_DIR；手动运行则回退 __file__
try:
    Import("env")  # noqa: F821  SCons 内置
    ROOT_DIR = env.subst("$PROJECT_DIR")
except NameError:
    try:
        ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        raise SystemExit("gen_channels: 无法定位项目根目录")

TXT_PATH = os.path.join(ROOT_DIR, "channels.txt")
OUT_PATH = os.path.join(ROOT_DIR, "src", "channels.h")


def parse_channels():
    """解析 channels.txt -> [(name, rss), ...]"""
    if not os.path.exists(TXT_PATH):
        raise SystemExit("gen_channels: 缺少 %s" % TXT_PATH)
    with open(TXT_PATH, encoding="utf-8-sig") as f:
        raw = f.read()
    chans = []
    for lineno, line in enumerate(raw.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 2 or not parts[0] or not parts[1].startswith("http"):
            raise SystemExit(
                "gen_channels: channels.txt 第 %d 行格式错误（应为 频道名|RSS地址）: %r"
                % (lineno, line))
        chans.append((parts[0], parts[1]))
    if not chans:
        raise SystemExit("gen_channels: channels.txt 里没有任何频道")
    return chans


def main():
    chans = parse_channels()
    lines = []
    lines.append("// 由 tools/gen_channels.py 自动生成（数据源：channels.txt）")
    lines.append("// 手改无效，下次编译会被覆盖；加频道请编辑 channels.txt")
    lines.append("#pragma once")
    lines.append("")
    lines.append("// 频道定义：名字 / RSS 地址（封面为运行时按名字生成的文字徽章）")
    lines.append("struct ChannelDef {")
    lines.append("  const char* name;")
    lines.append("  const char* rss;")
    lines.append("};")
    lines.append("")
    lines.append("static const ChannelDef CHANNELS[] = {")
    for name, rss in chans:
        lines.append('    {"%s", "%s"},' % (name, rss))
    lines.append("};")
    lines.append("")
    lines.append("static const int CHANNEL_COUNT =")
    lines.append("    sizeof(CHANNELS) / sizeof(CHANNELS[0]);")
    lines.append("")
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("gen_channels: %d channels -> %s" % (len(chans), OUT_PATH))


if __name__ == "__main__":
    main()
