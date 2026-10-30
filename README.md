# PodcastPlayer

M5Stack handheld podcast player (ESP32-S3): connects to WiFi automatically on boot, pulls the episode list from RSS, streams m4a/mp3, monochrome screen UI + auto-generated text cover art. **Supports two hardware targets**:

- **Cardputer ADV**: full keyboard input, swap channels from the SD card without reflashing
- **StickS3**: two-button gesture input, edit RSS directly on the config web page (swap stations without reflashing)

![Hardware](https://img.shields.io/badge/hardware-Cardputer%20ADV%20%7C%20StickS3-black) ![Framework](https://img.shields.io/badge/PlatformIO-Arduino--ESP32-orange)

## Features

- **WiFiManager zero-config networking**: on first boot the device opens a hotspot `PodcastPlayer`; connect from your phone and a captive portal at `192.168.4.1` opens to pick WiFi and enter the password. After that it reconnects automatically on every boot (credentials stored in NVS). Press `0` on the list page to reconfigure at any time
- **Multiple channels**: drop a `channels.txt` in the SD card root to add or remove channels — **switch channels with zero reflashing**; press Tab to open the channel page and switch, your selection is remembered automatically
- **Text cover art**: no image assets needed; the cover is generated from the channel name (a gradient badge with a dedicated color scheme + large channel name), and channels with the same name always get the same color
- **Online streaming**: audioI2S backend, supports M4A/AAC/MP3/WAV/FLAC, HTTPS without certificates, redirects followed automatically
- **Playback page**: cover art + show name + two-line title + progress bar + elapsed/total time, partially refreshed every second
- **SD card fault-tolerance chain**: SD config → NVS cache → compile-time built-in; removing the card, a bad card, or a malformed txt all fall back gracefully and never brick the device

## Hardware

| Item | Cardputer ADV | StickS3 |
|---|---|---|
| SoC | ESP32-S3 (no PSRAM) | ESP32-S3 (8MB OPI PSRAM) |
| Audio | ES8311, I2S_NUM_1 (BCLK 41 / WS 43 / DOUT 42), speaker and 3.5mm output both active | ES8311, I2S_NUM_0 (BCLK 17 / WS 15 / DOUT 14 / MCLK 18), amplifier enabled by M5PM1 |
| Display | 240x135 (auto-adapted by M5GFX) | 135x240 native + rotation 1 → 240x135 |
| Input | Full keyboard | A button (G11) / B button (G12) gestures |
| Channels | SD card `channels.txt`, swap stations without reflashing | Edit RSS on the config web page (stored in NVS), swap stations without reflashing |
| PSRAM | None (12KB input ring) | Yes (ample audio buffering) |
| CPU | 240MHz | 160MHz (reduced heat, plenty of headroom for AAC decoding) |

## Buttons (Cardputer ADV)

| Key | List page | Playback page | Channel page |
|---|---|---|---|
| `w` / `;` | Cursor up | Back to list | Up |
| `s` / `.` | Cursor down | Back to list | Down |
| `Enter` | Play / open playback page / resume | Pause · resume · replay | Switch channel |
| `+` / `-` | Volume ± | Volume ± | — |
| `0` | WiFi setup | — | Rescan |
| `Tab` | Channel page | — | Back to list |
| `` ` `` | Return to M5Launcher | Same | Same |

## Buttons (StickS3)

| Gesture | List page | Playback page |
|---|---|---|
| A single click | Cursor down | Back to list |
| A long press | Cursor up (auto-repeat) | Volume + (auto-repeat) |
| B single click | Play | Pause/resume |
| B long press 1.5s | WiFi setup hotspot | Volume − (auto-repeat) |

## Channel Configuration

**Cardputer ADV**: put `channels.txt` in the SD card root (UTF-8, lines starting with `#` are comments):

```
# Format: Channel Name|RSS URL
My Podcast|https://example.com/feed.xml
```

Cover art is generated automatically, no images needed. It also works without an SD card: the `channels.txt` in the repo is embedded at compile time as the fallback channel list.

**StickS3**: long-press the B button on the list page for 1.5s to open the setup hotspot; connect your phone to `PodcastPlayer` and edit "Channel Name" and "Podcast RSS URL" directly on the config page that pops up. Saving pulls the new channel automatically (stored in the device NVS, survives reboots). Clearing the RSS field and saving restores the firmware's built-in channels.

## Build

```bash
cd podcast_player
pio run -e cardputer-adv-podcast          # Cardputer ADV
pio run -e m5stick-s3-podcast             # StickS3
pio run -e cardputer-adv-podcast -t monitor   # serial log (115200)
```

Build artifacts are copied to `dist/` automatically: `PodcastPlayer.bin` for Cardputer, `PodcastPlayer_StickS3.bin` for StickS3. To flash: copy the bin to an SD card and pick it in M5Launcher (a single-app image that leaves the bootloader/partition table untouched); on Cardputer press `` ` `` to return to the Launcher at any time.

> Windows with a non-ASCII username: `tools/pio_windows_compat.py` handles the SCons response-file encoding issue and is already wired into the pre script, no extra steps needed.
>
> Decoder task stack patch: `tools/patch_audio_stack.py` bumps the audioI2S decoder task stack from 3300 to 8192 before building (the AAC decode call chain is deep and the default stack overflows); applies automatically to both environments.

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | 1.1.1 | Keyboard/board definitions |
| [M5GFX](https://github.com/m5stack/M5GFX) / M5Unified | 0.2.33 / 0.2.26 | Display driver |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | 3.0.12 | Audio streaming decoder |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | 2.0.17 | captive portal setup |
| [TinyXML2](https://github.com/leethomason/tinyxml2) | 9.0.0 | RSS parsing |

## Directory Structure

```
podcast_player/
├── platformio.ini        # Two environments: cardputer-adv-podcast / m5stick-s3-podcast
├── channels.txt          # Fallback channel list (embedded at compile time; Cardputer uses the SD card copy in production)
├── src/
│   ├── main.cpp          # All application logic (PODCAST_TARGET_STICKS3 distinguishes board differences)
│   └── channels.h        # auto-generated by gen_channels.py, do not edit by hand
└── tools/
    ├── gen_channels.py       # pre-build: channels.txt -> channels.h
    ├── patch_audio_stack.py  # pre-build: audioI2S decoder task stack 3300 -> 8192
    ├── deploy_dist.py        # post-build: firmware.bin -> dist/ (named per environment)
    ├── serial_dump.py        # debug: pyserial serial monitor to disk
    └── pio_windows_compat.py
```

## License

MIT
