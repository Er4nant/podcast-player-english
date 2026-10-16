# PodcastPlayer

M5Stack Cardputer ADV（ESP32-S3）掌上播客机：开机自动连网，从 RSS 拉取节目列表，串流播放 m4a/mp3，全键盘操作，黑白屏 UI + 自动生成的文字封面。

![硬件](https://img.shields.io/badge/hardware-Cardputer%20ADV-black) ![框架](https://img.shields.io/badge/PlatformIO-Arduino--ESP32-orange)

## 功能

- **WiFiManager 免配置组网**：首次开机设备自动开热点 `PodcastPlayer`，手机连接后弹出/访问 `192.168.4.1` 选 WiFi 输密码，之后每次开机自动直连（凭据存 NVS）。列表页按 `0` 随时重配
- **多频道**：SD 卡根目录放一个 `channels.txt` 即可增删频道，**换频道零烧录**；按 Tab 打开频道页切换，选择自动记忆
- **文字封面**：无需任何图片资源，封面按频道名自动生成（专属配色渐变徽章 + 大字频道名），同名频道永远同色
- **在线串流**：audioI2S 后端，支持 M4A/AAC/MP3/WAV/FLAC，HTTPS 免证书、重定向自动跟随
- **播放页**：封面 + 节目名 + 两行标题 + 进度条 + 已播/总时长，每秒局部刷新
- **SD 卡容错链**：SD 配置 → NVS 缓存 → 编译期内嵌，拔卡/坏卡/txt 写错都能自动降级，不会变砖

## 硬件

| 项 | 说明 |
|---|---|
| 设备 | M5Stack Cardputer ADV（ESP32-S3，无 PSRAM） |
| 音频 | ES8311 Codec + I2S_NUM_1（BCLK 41 / WS 43 / DOUT 42），外放与 3.5mm 同响 |
| 屏幕 | GC9A07 系列 SPI 屏，240x135 逻辑画布（M5GFX 自动适配） |
| 存储 | SD 卡（SCK 40 / MISO 39 / MOSI 14 / CS 12），可选，仅用于 channels.txt |

## 按键

| 键 | 列表页 | 播放页 | 频道页 |
|---|---|---|---|
| `w` / `;` | 光标上移 | 返回列表 | 上移 |
| `s` / `.` | 光标下移 | 返回列表 | 下移 |
| `Enter` | 播放/打开播放页/继续 | 暂停·续播·重播 | 切换频道 |
| `+` / `-` | 音量 ± | 音量 ± | — |
| `0` | WiFi 配网 | — | 重新扫描 |
| `Tab` | 频道页 | — | 返回列表 |
| `` ` `` | 退回 M5Launcher | 同左 | 同左 |

## 频道配置

SD 卡根目录放 `channels.txt`（UTF-8，`#` 开头为注释行）：

```
# 格式：频道名|RSS地址
XX播客|https://feed.xyzfm.space/xxxxxxxxxxxxxxxx
```

封面自动生成，不需要图片。不加 SD 卡也能用：仓库里的 `channels.txt` 会在编译时内嵌为兜底频道表。

## 编译

```bash
cd podcast_player
pio run -e cardputer-adv-podcast          # 编译
pio run -e cardputer-adv-podcast -t monitor   # 串口日志（115200）
```

产物自动复制到 `dist/PodcastPlayer.bin`。刷机：把 bin 拷进 SD 卡，用 M5Launcher 选择刷入（单应用镜像，不动 bootloader/分区表）；按 `` ` `` 随时返回 Launcher。

> Windows 中文用户名环境：`tools/pio_windows_compat.py` 已处理 SCons 响应文件编码问题，已挂到 pre 脚本，无需额外操作。

## 依赖

| 库 | 版本 | 用途 |
|---|---|---|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | 1.1.1 | 键盘/板级定义 |
| [M5GFX](https://github.com/m5stack/M5GFX) / M5Unified | 0.2.33 / 0.2.26 | 屏幕驱动 |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | 3.0.12 | 音频串流解码 |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | 2.0.17 | captive portal 配网 |
| [TinyXML2](https://github.com/leethomason/tinyxml2) | 9.0.0 | RSS 解析 |

## 目录结构

```
podcast_player/
├── platformio.ini        # cardputer-adv-podcast 环境（依赖全 GitHub tag 固定）
├── channels.txt          # 兜底频道表（编译时内嵌；正式用放 SD 卡）
├── src/
│   ├── main.cpp          # 全部应用逻辑
│   └── channels.h        # gen_channels.py 自动生成，勿手改
└── tools/
    ├── gen_channels.py   # 预构建：channels.txt -> channels.h
    ├── deploy_dist.py    # 后构建：firmware.bin -> dist/PodcastPlayer.bin
    └── pio_windows_compat.py
```

## License

MIT
