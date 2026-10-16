// =====================================================================
// Cardputer ADV 播客 RSS 在线播放器（v3 美化版）
//
// 硬件：M5Stack Cardputer ADV（ESP32-S3，8MB Flash，无 PSRAM）
// 屏幕：ST7789，原生 135x240 + rotation 1 -> 画布 240x135（M5GFX 0.2.33）
// 音频：板载 ES8311 Codec，I2C 总线 SDA=8 / SCL=9（M5Unified 自动初始化），
//       I2S_NUM_1，BCLK=41 / WS=43 / DOUT=42，无 MCLK 线
//       （ES8311 寄存器 0x01=0xB5 配置 MCLK=BCLK）。
//       外放与 3.5mm 耳机共用 HP 驱动输出路径（寄存器 0x13=0x10）。
//
// 音频后端：ESP32-audioI2S 3.0.12（HTTPS insecure / 重定向 / M4A-AAC 内置）。
// 配网：tzapu/WiFiManager v2.0.17 —— 设备开热点 PodcastPlayer，手机连上
// 自动弹出配置页（192.168.4.1）选网输密码，凭据存 NVS，无硬编码 WiFi。
// 订阅源（小宇宙）全部节目是 .m4a，ESP8266Audio 无法处理 MP4 容器（v1 教训）。
//
// UI（v3.4 黑白配色：纯黑底 + 白字灰阶层次，仅封面为彩色）：
//   - 列表页：状态栏 + 6 行节目列表，Enter 播放并进入播放页
//   - 播放页：左侧 100x100 内嵌封面（cover.h，gen_cover.py 生成），
//     右侧节目名/两行标题/白色进度条/当前与总时长；进度每秒局部刷新
//   - 按键：';'/w 与 '.'/s 移动光标（列表页）或返回列表（播放页）
//     Enter 播放/暂停/续播，'+'/'=' 与 '-'/'_' 音量，'`' 退出到 M5Launcher
//   - 播放完毕（EOF）自动置为"播放完毕"并刷新屏幕
//
// 已知限制（MVP 取舍）：暂停用 pauseResume()，连接存活期间位置精确；
// 暂停时间过长 CDN 可能断连，此时按 Enter 重新从头播放。
//
// 关键实现备忘：
//   - ES8311 初始化在 connecttohost 成功后执行（I2S 时钟已运行），
//     镜像 M5Unified 的 speaker post-start 回调时机。
//   - M5Launcher 启动的应用重启后仍停留在本应用（otadata），
//     必须提供返回 Launcher 的按键。
// =====================================================================

#include <M5Unified.h>
#include <M5Cardputer.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>  // 频道选择持久化

#include <WiFiManager.h>  // tzapu/WiFiManager：热点 + captive portal 配网

#include "Audio.h"  // ESP32-audioI2S 3.0.12

#include "channels.h"  // 编译期内嵌兜底频道表（tools/gen_channels.py 生成）

#include <SD.h>      // SD 卡：channels.txt（免烧录换频道）
#include <SPI.h>

#include <tinyxml2.h>

#include <esp_ota_ops.h>

#include <vector>
#include <string>

// ========================== 可修改配置区 ==========================
// WiFi：WiFiManager captive portal 配网（无硬编码凭据，适合开源）——
// 首次开机设备开热点 PodcastPlayer，手机连上自动弹出配置页选网输密码；
// 凭据存 NVS，之后开机自动连接。列表页按 0 重新配网。
//
// 频道：在仓库根目录 channels.txt 维护（每行 频道名|RSS地址|封面|裁剪宽），
// 编译时由 tools/gen_channels.py 自动生成 src/channels.h，不用改 C++ 代码。
// 列表页按 Tab 打开频道切换页（Esc 键键盘库不上报，用 Tab 代替）。
static const char* AP_NAME = "PodcastPlayer";  // 配网热点名

// Cardputer ADV 板载音频引脚（与 M5Unified 0.2.26 对 CardputerADV 的
// speaker 配置一致：I2S_NUM_1，无 MCLK）
static constexpr int PIN_I2S_BCLK = 41;
static constexpr int PIN_I2S_WS   = 43;
static constexpr int PIN_I2S_DOUT = 42;

static constexpr uint8_t ES8311_ADDR = 0x18;  // 备用地址 0x19

static constexpr size_t   MAX_EPISODES   = 60;        // 最多解析集数
static constexpr size_t   MAX_ITEM_BYTES = 48 * 1024; // 单条 item 上限
static constexpr size_t   MAX_FEED_BYTES = 2u * 1024 * 1024;  // 订阅源读取上限
static constexpr int      VOLUME_MAX     = 16;
static constexpr uint8_t  VOLUME_DEFAULT = 12;
// =================================================================

// ------------------------ 配色（黑白灰，仅封面彩色） ------------------------
static constexpr uint16_t C_BG      = 0x0000;  // 纯黑底
static constexpr uint16_t C_ACCENT  = 0xFFFF;  // 强调=白（标题/进度条/光标/封面框）
static constexpr uint16_t C_BAR_BG  = 0x2104;  // 进度条底=暗灰
static constexpr uint16_t C_SELECT  = 0x2104;  // 列表选中行底=暗灰
static constexpr uint16_t C_TEXT    = 0xFFFF;  // 正文白
static constexpr uint16_t C_DIM     = 0x7BEF;  // 次要灰
static constexpr uint16_t C_WARN    = 0xFFFF;  // 提示=白（黑白主题）

// --------------------------- 全局状态 ---------------------------
struct Episode {
  std::string title;  // 节目标题（已清理空白字符）
  std::string url;    // 音频直链（<enclosure url="...">，本源为 .m4a）
};
static std::vector<Episode> episodes;

enum View { VIEW_LIST, VIEW_PLAYER, VIEW_CHANNEL };
static View view      = VIEW_LIST;  // 当前页面
static int  chanIdx    = 0;         // 当前频道下标（NVS 持久化）
static int  chanCursor = 0;         // 频道页光标
static int  chanTop    = 0;         // 频道页滚动窗口顶部

// ------------------------- 运行时频道表 -------------------------
// 优先级：SD 卡 /channels.txt（免烧录改频道）> NVS 缓存 > 编译期内嵌。
// 封面无图片资源：按频道名自动生成文字徽章（见 drawCoverArt）
struct ChannelEntry {
  std::string name;
  std::string rss;
};
static std::vector<ChannelEntry> channels;
static bool sdReady = false;

static SPIClass sdSPI(HSPI);
// SD 引脚（Cardputer 布局；LCD 走 34~37 独立总线，互不冲突）
constexpr int PIN_SD_SCK = 40, PIN_SD_MISO = 39, PIN_SD_MOSI = 14,
              PIN_SD_CS = 12;

// ---------------- 频道文字封面（自动生成，无图片资源） ----------------
// 每个频道按名字哈希取一套配色：渐变底 + 白色角标 + 大号频道名
static inline uint16_t RGB888(uint8_t r, uint8_t g, uint8_t b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

struct CoverScheme {
  uint8_t  r1, g1, b1;  // 渐变顶
  uint8_t  r2, g2, b2;  // 渐变底
  uint16_t text;        // 文字/角标颜色
};

static const CoverScheme kCoverSchemes[] = {
    { 96,  52, 176,   38,  14,  82, 0xFFFF},  // 紫（霓达原色系）
    {  0, 128, 138,    0,  48,  72, 0xFFFF},  // 青
    {178,  44,  64,   88,   8,  30, 0xFFFF},  // 绛红
    {206, 116,  24,  122,  48,   8, 0xFFFF},  // 橙
    { 28, 126,  96,    8,  58,  46, 0xFFFF},  // 松绿
    { 52,  72, 190,   14,  22,  92, 0xFFFF},  // 靛蓝
};
static const int kSchemeCount =
    sizeof(kCoverSchemes) / sizeof(kCoverSchemes[0]);

// FNV-1a 哈希：同名频道永远同配色
static uint32_t nameHash(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h ^ (h >> 16);
}

// 频道列表页的小色块（取渐变顶色）
static uint16_t coverTopColor(const std::string& name) {
  const CoverScheme& sc = kCoverSchemes[nameHash(name) % kSchemeCount];
  return RGB888(sc.r1, sc.g1, sc.b1);
}

// 文字封面：垂直渐变底 + 内圈角标 + 居中大字频道名 + 底部装饰线
static void drawCoverArt(int x, int y, int w, int h,
                         const std::string& name) {
  auto& d = M5Cardputer.Display;
  const CoverScheme& sc = kCoverSchemes[nameHash(name) % kSchemeCount];

  // 渐变底（逐行插值）
  for (int i = 0; i < h; ++i) {
    int t = (h > 1) ? i * 255 / (h - 1) : 0;
    uint8_t r = sc.r1 + (sc.r2 - sc.r1) * t / 255;
    uint8_t g = sc.g1 + (sc.g2 - sc.g1) * t / 255;
    uint8_t b = sc.b1 + (sc.b2 - sc.b1) * t / 255;
    d.drawFastHLine(x, y + i, w, RGB888(r, g, b));
  }

  // 四角白色 L 形角标（徽章感）
  const int arm = 7, off = 5;
  uint16_t ln = sc.text;
  d.drawFastHLine(x + off, y + off, arm, ln);
  d.drawFastVLine(x + off, y + off, arm, ln);
  d.drawFastHLine(x + w - off - arm, y + off, arm, ln);
  d.drawFastVLine(x + w - off - 1, y + off, arm, ln);
  d.drawFastHLine(x + off, y + h - off - 1, arm, ln);
  d.drawFastVLine(x + off, y + h - off - arm, arm, ln);
  d.drawFastHLine(x + w - off - arm, y + h - off - 1, arm, ln);
  d.drawFastVLine(x + w - off - 1, y + h - off - arm, arm, ln);

  // 按频道名哈希取色已完成；下面排文字：先试 2 倍大字（32px/字），
  // 放不下（超过 3 行或超出行上限）退回 1 倍（16px/字）
  auto countChars = [](const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
      if ((c & 0xC0) != 0x80) ++n;  // UTF-8 续字节不计
    return n;
  };
  int nch = countChars(name);
  int size = 1, cpl = (w - 20) / 16, maxLines = (h - 16) / 16;
  int lines = (nch + cpl - 1) / cpl;
  if (lines <= 3) {  // 有机会用大字
    int cpl2 = (w - 20) / 32;
    int mL2  = (h - 16) / 32;
    int ln2  = (nch + cpl2 - 1) / cpl2;
    if (ln2 >= 1 && ln2 <= mL2 && ln2 <= 3) {
      size = 2;
      cpl = cpl2;
      lines = ln2;
      maxLines = mL2;
    }
  }
  if (lines > maxLines) lines = maxLines;  // 超长名字截断
  if (lines < 1) lines = 1;

  // 居中排版绘制（按 UTF-8 codepoint 边界切行）
  d.setTextDatum(m5gfx::textdatum_t::top_center);
  d.setTextColor(sc.text);  // 透明底，渐变透出
  d.setTextSize(size);
  int lh     = 16 * size;
  int yStart = y + (h - lines * lh) / 2;
  std::string rem = name;
  for (int i = 0; i < lines && !rem.empty(); ++i) {
    int cnt = 0, cut = (int)rem.size();
    for (size_t p = 0; p < rem.size(); ++p) {
      if ((rem[p] & 0xC0) != 0x80 && ++cnt > cpl) {
        cut = (int)p;
        break;
      }
    }
    std::string seg = rem.substr(0, cut);
    d.drawString(seg.c_str(), x + w / 2, yStart + i * lh);
    rem.erase(0, cut);
  }

  // 文字块下方装饰短线（居中）
  int yEnd = yStart + lines * lh;
  if (yEnd + 8 <= y + h - off) {
    d.fillRect(x + w / 2 - 12, yEnd + 4, 24, 2, sc.text);
  }

  // 恢复默认文本状态（页面其余部分依赖 top_left + size 1）
  d.setTextDatum(m5gfx::textdatum_t::top_left);
  d.setTextSize(1);
}
static int  cursor    = 0;          // 当前选中行（列表页）
static int  scrollTop = 0;          // 列表窗口顶部的集数下标
static bool wifiOk    = false;
static bool uiDirty   = true;       // 需要整页重绘
static std::string statusMsg;       // 状态栏中间的提示文字（如"缓冲中..."）

static Audio audio(false, 3, I2S_NUM_1);  // 外部 DAC、双声道、I2S 端口 1
static bool    i2sConfigured = false;     // setPinout/音量 只做一次
static bool    es8311Ready   = false;     // ES8311 初始化成功标志
static bool    paused        = false;     // 用户暂停态

static struct {
  int     current = -1;                  // 当前加载的集数下标
  uint8_t volume  = VOLUME_DEFAULT;
} player;

static int listLines  = 6;    // 列表可见行数，setup 里按实际屏幕高度计算
static int listTop    = 20;   // 列表区起始 y
static int lineHeight = 19;   // 每行高度

static void drawUI();
static void drawPlayerPage(bool full);
static void drawChannelPage();   // 频道切换页
static void drawPortalScreen();  // 配网热点说明页
static bool openPortal();        // 重新打开配网热点（列表页按 0）

// --------------------- WiFi 配网（captive portal） ---------------------

// 配网说明页：手机连接设备热点后，DNS 劫持会自动弹出配置页；
// setAPCallback 在热点就绪时由 WiFiManager 调用
static void drawPortalScreen() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(C_BG);
  d.setTextDatum(m5gfx::textdatum_t::top_left);
  d.setFont(&fonts::efontCN_16);
  d.setTextColor(C_ACCENT, C_BG);
  d.drawString("WiFi 配置", 2, 4);
  d.setTextColor(C_TEXT, C_BG);
  d.drawString("1.手机WiFi连接热点", 2, 30);
  d.drawString("  PodcastPlayer", 2, 50);
  d.drawString("2.自动弹出配置页", 2, 70);
  d.drawString("  或访问 192.168.4.1", 2, 90);
  d.drawString("3.选择家中WiFi输密码", 2, 110);
}

// 打开配网热点（阻塞）：WiFiManager 内部跑 webserver + DNS，直到连上
// 或超时。首次开机无凭据时由 setup 走 autoConnect（先试已存凭据），
// 之后按 0 走这里重新配网。返回是否已连上 WiFi。
static bool openPortal() {
  WiFiManager wm;
  wm.setConnectTimeout(10);        // 已存凭据的连接尝试等待
  wm.setConfigPortalTimeout(180);  // 无人操作 3 分钟自动关闭热点
  wm.setAPCallback([](WiFiManager*) { drawPortalScreen(); });
  bool ok = wm.startConfigPortal(AP_NAME);
  wifiOk = ok && (WiFi.status() == WL_CONNECTED);
  if (wifiOk) WiFi.setSleep(false);  // 在线串流需要稳定吞吐
  uiDirty = true;                    // 恢复主界面（loop 里重绘）
  return wifiOk;
}

// --------------------------- 工具函数 ---------------------------

// 清理 XML 文本：去掉换行/制表符，压缩连续空格，去掉首尾空白
static std::string cleanText(const char* raw) {
  std::string out;
  if (raw == nullptr) return out;
  out.reserve(strlen(raw));
  bool prevSpace = true;  // 顺便吞掉开头空格
  for (const char* p = raw; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (c == ' ') {
      if (prevSpace) continue;
      prevSpace = true;
    } else {
      prevSpace = false;
    }
    out.push_back((char)c);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// 单个 UTF-8 字符的字节序列长度与显示宽度（efontCN_16：全角 16px 半角 8px）
static void utf8CharAt(const std::string& s, size_t i, int& seq, int& cw) {
  unsigned char c = (unsigned char)s[i];
  if      (c >= 0xF0) { seq = 4; cw = 16; }
  else if (c >= 0xE0) { seq = 3; cw = 16; }
  else if (c >= 0xC0) { seq = 2; cw = 8;  }
  else                { seq = 1; cw = 8;  }
}

// 按显示宽度截断（结尾补 "..."，不切进多字节字符中间）
static std::string fitText(const std::string& s, int max_px) {
  const int ellipsis_px = 3 * 8;
  int w = 0;
  size_t i = 0;
  while (i < s.size()) {
    int seq, cw;
    utf8CharAt(s, i, seq, cw);
    if (w + cw > max_px - ellipsis_px) {
      size_t cut = i;
      while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
      return s.substr(0, cut) + "...";
    }
    w += cw;
    i += seq;
  }
  return s;
}

// 按显示宽度折行（最多 max_lines 行，末行超长补 "..."）
static std::vector<std::string> wrapText(const std::string& s, int max_px,
                                         int max_lines) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i < s.size() && (int)lines.size() < max_lines) {
    std::string line;
    int w = 0;
    bool overflow = false;
    while (i < s.size()) {
      int seq, cw;
      utf8CharAt(s, i, seq, cw);
      if (w + cw > max_px) {
        // 当前行还有后续内容 -> 记为折行
        if ((int)lines.size() == max_lines - 1) overflow = true;
        break;
      }
      line += s.substr(i, seq);
      w += cw;
      i += seq;
    }
    if (overflow) {
      // 最后一行装不下剩余内容：退一个字符补省略号
      while (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) {
        line.pop_back();
      }
      if (!line.empty()) line.pop_back();
      // 退字符后重新按省略号宽度裁
      int lw = 0;
      for (size_t k = 0; k < line.size();) {
        int seq, cw;
        utf8CharAt(line, k, seq, cw);
        lw += cw;
        k += seq;
      }
      while (lw > max_px - 24 && !line.empty()) {
        char lc = line.back();
        line.pop_back();
        lw -= ((lc & 0x80) == 0) ? 8 : 0;
        // 多字节字符尾字节被逐个剥掉，宽度按剥到字符头为止修正
        if (!line.empty() && ((unsigned char)line.back() & 0xC0) == 0x80) {
          continue;
        }
        break;
      }
      line += "...";
    }
    lines.push_back(line);
  }
  return lines;
}

// 秒 -> "mm:ss" 或 "h:mm:ss"
static std::string fmtTime(uint32_t sec) {
  if (sec == 0) return "--:--";
  char buf[16];
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%u:%02u:%02u",
             (unsigned)h, (unsigned)m, (unsigned)s);
  } else {
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)m, (unsigned)s);
  }
  return buf;
}

// --------------------------- ES8311 ---------------------------

// 经由 M5Unified 的内部 I2C 总线（CardputerADV：SDA=8 / SCL=9，M5.begin
// 已自动初始化）写 ES8311 寄存器
static bool es8311Write(uint8_t addr, uint8_t reg, uint8_t val) {
  return M5Cardputer.In_I2C.writeRegister8(addr, reg, val, 100000);
}

// ES8311 初始化。寄存器序列逐字照抄 M5Unified 0.2.26 的
// CardputerADV speaker 回调（src/M5Unified.inl）。
// 必须在 I2S 时钟运行后调用（M5Unified 即在 speaker post-start 回调里
// 做这套写入），因此在 connecttohost 成功后执行。
static bool initEs8311() {
  uint8_t addr = ES8311_ADDR;
  if (!es8311Write(addr, 0x00, 0x80)) {       // 先写 RESET 探测地址
    addr = 0x19;
    if (!es8311Write(addr, 0x00, 0x80)) {
      Serial.println("[audio] ES8311 not found (0x18/0x19)");
      return false;
    }
  }
  static const uint8_t seq[][2] = {
      {0x00, 0x80},  // 0x00 RESET   / CSM POWER ON
      {0x01, 0xB5},  // 0x01 CLOCK_MANAGER / MCLK=BCLK（无需独立 MCLK 线）
      {0x02, 0x18},  // 0x02 CLOCK_MANAGER / MULT_PRE=3
      {0x0D, 0x01},  // 0x0D SYSTEM   / 上电模拟电路
      {0x12, 0x00},  // 0x12 SYSTEM   / 打开 DAC
      {0x13, 0x10},  // 0x13 SYSTEM   / 使能 HP 驱动输出（外放+耳机共用）
      {0x32, 0xBF},  // 0x32 DAC      / 硬件音量 ±0dB，软件层再做 setVolume
      {0x37, 0x08},  // 0x37 DAC      / 旁路 DAC 均衡器
  };
  bool ok = true;
  for (auto& r : seq) ok = es8311Write(addr, r[0], r[1]) && ok;
  Serial.printf("[audio] ES8311 init @0x%02X %s\n", addr, ok ? "ok" : "FAIL");
  return ok;
}

// --------------------------- 播放控制 ---------------------------

static void startPlayback(int idx) {
  if (audio.isRunning()) audio.stopSong();
  paused = false;
  player.current = idx;

  // connecttohost 会同步阻塞数秒（DNS+TLS+GET+m4a 头解析），先画提示
  statusMsg = "缓冲中...";
  uiDirty = true;
  drawUI();
  uiDirty = false;

  if (!i2sConfigured) {
    // 无 MCLK 引脚（ES8311 已配置 MCLK=BCLK）
    audio.setPinout(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
    audio.setVolumeSteps(VOLUME_MAX);
    audio.setVolume(player.volume);
    audio.setConnectionTimeout(8000, 15000);
    i2sConfigured = true;
  }

  bool ok = audio.connecttohost(episodes[idx].url.c_str());
  if (ok && !es8311Ready) {
    // I2S 时钟已在运行，此刻初始化 ES8311 与 M5Unified 时机一致
    es8311Ready = initEs8311();
    if (!es8311Ready) Serial.println("[audio] ES8311 init failed, no sound");
  }
  statusMsg = ok ? "" : "连接失败";
  view = VIEW_PLAYER;   // 进入播放页
  uiDirty = true;
}

// --------------------------- RSS 拉取 ---------------------------

// 用 TinyXML2 解析单条 <item>...</item>，提取标题与 enclosure 直链
static void parseEpisodeXml(const std::string& xml, std::vector<Episode>& out) {
  tinyxml2::XMLDocument doc;
  if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) return;
  const tinyxml2::XMLElement* item = doc.RootElement();
  if (item == nullptr) return;
  const tinyxml2::XMLElement* titleEl = item->FirstChildElement("title");
  const tinyxml2::XMLElement* encEl   = item->FirstChildElement("enclosure");
  if (titleEl == nullptr || encEl == nullptr) return;
  const char* t = titleEl->GetText();
  const char* u = encEl->Attribute("url");
  if (t == nullptr || u == nullptr) return;
  Episode ep;
  ep.title = cleanText(t);
  ep.url   = cleanText(u);
  if (!ep.title.empty() && !ep.url.empty()) out.push_back(ep);
}

// 流式拉取 RSS：滑动窗口只缓冲单条 <item>，其余内容即读即弃。
// 返回是否至少解析出一集。
static bool fetchEpisodes(std::vector<Episode>& eps) {
  WiFiClientSecure client;
  client.setInsecure();  // 自用：跳过 HTTPS 证书校验
  HTTPClient http;
  if (!http.begin(client, channels[chanIdx].rss.c_str())) return false;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("CardputerPodcast/1.0");
  http.setConnectTimeout(10000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[rss] HTTP GET code=%d\n", code);
    http.end();
    return false;
  }
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return false;
  }
  stream->setTimeout(8000);

  std::string pending;   // 不在 item 内时的小缓冲（用于跨块匹配 "<item"）
  std::string item;      // 当前 item 的累积缓冲
  bool inItem = false;
  size_t total = 0;
  uint8_t buf[1024];

  while (eps.size() < MAX_EPISODES && total < MAX_FEED_BYTES) {
    int n = (int)stream->readBytes(buf, sizeof(buf));
    if (n <= 0) break;  // 流结束
    total += (size_t)n;

    if (!inItem) {
      pending.append((const char*)buf, (size_t)n);
      size_t pos = pending.find("<item");
      if (pos != std::string::npos) {
        inItem = true;
        item = pending.substr(pos);
        pending.clear();
      } else if (pending.size() > 8) {
        // 只保留尾部 8 字节，足够匹配跨块的 "<item"
        pending.erase(0, pending.size() - 8);
      }
    } else {
      item.append((const char*)buf, (size_t)n);
      size_t pos = item.find("</item>");
      if (pos != std::string::npos) {
        // 完整的一条 item：交给 TinyXML2 解析
        parseEpisodeXml(item.substr(0, pos + 7), eps);
        // 剩余内容回到 pending，继续找下一条 <item>
        pending = item.substr(pos + 7);
        if (pending.size() > 8) pending.erase(0, pending.size() - 8);
        item.clear();
        inItem = false;
      } else if (item.size() > MAX_ITEM_BYTES) {
        // 异常超长条目，整条跳过
        Serial.println("[rss] item too large, skipped");
        item.clear();
        pending.clear();
        inItem = false;
      }
    }
  }
  http.end();
  Serial.printf("[rss] %u episodes, %u bytes\n",
                (unsigned)eps.size(), (unsigned)total);
  return !eps.empty();
}

// 带屏幕提示的拉取入口（首次启动与列表为空时按 Enter 重试共用）
static void fetchWithProgress() {
  statusMsg = "拉取:" + channels[chanIdx].name;
  uiDirty = true;
  drawUI();
  uiDirty = false;

  episodes.clear();
  fetchEpisodes(episodes);
  cursor = 0;
  scrollTop = 0;
  statusMsg = episodes.empty() ? "拉取失败，Enter重试" : "";
  uiDirty = true;
}

// 切换频道：停播、清列表、NVS 记忆、重拉 RSS（选同一频道则只回列表页）
static void switchChannel(int idx) {
  if (idx < 0 || idx >= (int)channels.size()) return;
  if (idx != chanIdx) {
    if (audio.isRunning()) {
      audio.stopSong();
      while (audio.isRunning()) delay(10);  // 等解码线程停干净再动频道数据
    }
    paused         = false;
    player.current = -1;
    chanIdx        = idx;
    Preferences p;
    p.begin("chan", false);
    p.putUChar("idx", (uint8_t)chanIdx);
    p.end();
    // 封面为文字徽章，切换频道零开销
    statusMsg = "已切换: " + channels[chanIdx].name;
  }
  view    = VIEW_LIST;
  uiDirty = true;
  if (wifiOk) {
    fetchWithProgress();
  } else {
    episodes.clear();
    cursor    = 0;
    scrollTop = 0;
    statusMsg = "未连WiFi，按0配网";
  }
}

// --------------------------- 绘制 ---------------------------

static void syncScroll() {
  if (cursor < scrollTop) scrollTop = cursor;
  if (cursor >= scrollTop + listLines) scrollTop = cursor - listLines + 1;
}

// 状态栏：WiFi | 状态提示（自动限宽） | 音量
static void drawStatusBar() {
  auto& d = M5Cardputer.Display;
  d.fillRect(0, 0, d.width(), listTop - 2, C_BG);

  d.setTextColor(wifiOk ? C_ACCENT : C_DIM, C_BG);
  d.drawString(wifiOk ? "WiFi:OK" : "WiFi:NG", 2, 1);

  // 状态文字：可用宽度 58..188
  const char* st = nullptr;
  uint16_t stColor = C_TEXT;
  if (!statusMsg.empty()) {
    st = statusMsg.c_str();
    stColor = C_WARN;
  } else if (audio.isRunning() && !paused) {
    st = "播放中";
    stColor = C_ACCENT;
  } else if (player.current >= 0) {
    st = "暂停";
  } else {
    st = "停止";
  }
  std::string shown = fitText(st, 128);
  int w = d.textWidth(shown.c_str());
  d.setTextColor(stColor, C_BG);
  d.drawString(shown.c_str(), 58 + (128 - w) / 2, 1);

  char vol[16];
  snprintf(vol, sizeof(vol), "Vol:%d", player.volume);
  d.setTextColor(C_DIM, C_BG);
  d.drawString(vol, d.width() - d.textWidth(vol) - 2, 1);
}

// 列表页
static void drawListPage() {
  auto& d = M5Cardputer.Display;
  // 先清整个列表区（状态栏以下的全部），否则光标移动/滚动后旧帧文字
  // 和旧选中条残留在行间缝隙里，看起来"透出下面一层"
  d.fillRect(0, listTop - 2, d.width(), d.height() - listTop + 2, C_BG);
  if (episodes.empty()) {
    d.setTextColor(C_DIM, C_BG);
    d.drawString("节目列表为空", 2, listTop);
    d.drawString("Enter重试拉取", 2, listTop + lineHeight);
  } else {
    for (int row = 0; row < listLines; ++row) {
      int idx = scrollTop + row;
      if (idx >= (int)episodes.size()) break;
      int y = listTop + row * lineHeight;
      bool selected  = (idx == cursor);
      bool isPlaying = (idx == player.current);
      if (selected) {
        d.fillRect(0, y, d.width(), lineHeight, C_SELECT);
      }
      char prefix[10];
      snprintf(prefix, sizeof(prefix), "%2d%s", idx + 1, isPlaying ? ">" : " ");
      std::string line = std::string(prefix) + " " + episodes[idx].title;
      std::string shown = fitText(line, d.width() - 6);
      if (selected) {
        d.setTextColor(C_TEXT, C_SELECT);
      } else if (isPlaying) {
        d.setTextColor(C_ACCENT, C_BG);
      } else {
        d.setTextColor(C_DIM, C_BG);
      }
      d.drawString(shown.c_str(), 2, y + 1);
    }
  }
}

// 进度条 + 时间（局部刷新用）
static void drawProgressArea() {
  auto& d = M5Cardputer.Display;
  constexpr int PX = 116, PW = 120;
  d.fillRect(PX, 80, PW, 26, C_BG);

  uint32_t dur = audio.getAudioFileDuration();
  uint32_t cur = audio.getAudioCurrentTime();
  if (dur == 0) { cur = 0; }
  if (cur > dur && dur > 0) cur = dur;

  // 进度条
  d.fillRect(PX, 82, PW, 6, C_BAR_BG);
  if (dur > 0) {
    int fill = (int)((uint64_t)PW * cur / dur);
    if (fill > 0) d.fillRect(PX, 82, fill, 6, C_ACCENT);
  }
  // 播放中给条尾一个亮点，暂停时无
  if (audio.isRunning() && !paused && dur > 0) {
    int fill = (int)((uint64_t)PW * cur / dur);
    d.fillRect(PX + fill - 2, 81, 3, 8, C_TEXT);
  }

  // 时间
  char tbuf[48];
  snprintf(tbuf, sizeof(tbuf), "%s / %s",
           fmtTime(cur).c_str(), fmtTime(dur).c_str());
  d.setTextColor(C_DIM, C_BG);
  d.drawString(tbuf, PX, 92);
}

// 播放页：封面 + 节目名 + 标题 + 进度（full=false 只刷进度区）
static void drawPlayerPage(bool full) {
  auto& d = M5Cardputer.Display;
  if (full) {
    d.fillScreen(C_BG);

    // 文字封面徽章（按频道名哈希配色：渐变底 + 角标 + 大字）
    drawCoverArt(6, 26, 100, 100, channels[chanIdx].name);

    // 节目名（白色）+ 装饰短线
    d.setTextColor(C_ACCENT, C_BG);
    d.drawString(channels[chanIdx].name.c_str(), 116, 26);
    d.fillRect(116, 43, 40, 2, C_ACCENT);

    // 标题：最多两行
    if (player.current >= 0 && player.current < (int)episodes.size()) {
      auto lines = wrapText(episodes[player.current].title, 120, 2);
      d.setTextColor(C_TEXT, C_BG);
      for (size_t i = 0; i < lines.size(); ++i) {
        d.drawString(lines[i].c_str(), 116, 48 + (int)i * 17);
      }
    }
    // 底部操作提示
    d.setTextColor(C_DIM, C_BG);
    d.drawString("w/s:返回列表", 116, 116);
  }
  drawProgressArea();
}

// 频道切换页：光标选频道，* 标记当前频道，Enter 确认切换
static void drawChannelPage() {
  auto& d = M5Cardputer.Display;
  d.fillScreen(C_BG);
  d.setTextColor(C_ACCENT, C_BG);
  d.drawString("选择频道", 2, 1);

  int visible = listLines - 2;  // 标题、底部提示各占一行
  if (chanCursor < chanTop) chanTop = chanCursor;
  if (chanCursor >= chanTop + visible) chanTop = chanCursor - visible + 1;
  int maxTop = (int)channels.size() > visible
                   ? (int)channels.size() - visible
                   : 0;
  if (chanTop > maxTop) chanTop = maxTop;

  for (int row = 0; row < visible; ++row) {
    int  idx   = chanTop + row;
    if (idx >= (int)channels.size()) break;
    int  y     = listTop + row * lineHeight;
    bool sel   = (idx == chanCursor);
    bool isCur = (idx == chanIdx);
    if (sel) d.fillRect(0, y, d.width(), lineHeight, C_SELECT);
    // 频道专属色块（与文字封面同色，方便按颜色认频道）
    d.fillRect(2, y + 4, 10, 10, coverTopColor(channels[idx].name));
    std::string line = std::string(sel ? "> " : "   ") + channels[idx].name +
                       (isCur ? " *" : "");
    d.setTextColor(sel ? C_TEXT : (isCur ? C_ACCENT : C_DIM),
                   sel ? C_SELECT : C_BG);
    d.drawString(fitText(line, d.width() - 6).c_str(), 2, y + 1);
  }
  d.setTextColor(C_DIM, C_BG);
  d.drawString("w/s选择 Enter切换 Tab返回", 2, d.height() - 17);
}

static void drawUI() {
  drawStatusBar();
  if (view == VIEW_LIST) {
    drawListPage();
  } else if (view == VIEW_CHANNEL) {
    drawChannelPage();
  } else {
    drawPlayerPage(true);
  }
}

// --------------------------- 按键 ---------------------------

// 退出到 M5Launcher：Launcher 启动的应用重启后仍停留在本应用（otadata），
// 必须显式把启动分区切回 Launcher 再重启
static void returnToLauncher() {
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part != nullptr) {
    esp_ota_set_boot_partition(part);
  }
  esp_restart();
}

static void handleKeys() {
  auto& kb = M5Cardputer.Keyboard;
  bool up    = kb.isKeyPressed(';') || kb.isKeyPressed('w');
  bool down  = kb.isKeyPressed('.') || kb.isKeyPressed('s');
  bool volUp = kb.isKeyPressed('=') || kb.isKeyPressed('+');
  bool volDn = kb.isKeyPressed('-') || kb.isKeyPressed('_');
  bool back  = kb.isKeyPressed('`');
  bool enter = kb.keysState().enter;

  if (back) {
    returnToLauncher();  // 不返回
  }

  // ---- 上下：列表页移动光标；播放页返回列表 ----
  static bool     lastUp = false, lastDown = false;
  static uint32_t lastNavMs = 0;
  uint32_t now = millis();
  if (up != lastUp || down != lastDown) lastNavMs = 0;
  lastUp = up;
  lastDown = down;
  if (up || down) {
    if (view == VIEW_PLAYER) {
      if (now - lastNavMs >= 200) {
        view = VIEW_LIST;   // 播放页按上下键 -> 回列表
        lastNavMs = now;
        uiDirty = true;
      }
    } else if (view == VIEW_CHANNEL && now - lastNavMs >= 160) {
      // 频道页：光标在频道列表上移动
      if (up && chanCursor > 0) {
        --chanCursor;
        lastNavMs = now;
        uiDirty = true;
      }
      if (down && chanCursor < (int)channels.size() - 1) {
        ++chanCursor;
        lastNavMs = now;
        uiDirty = true;
      }
    } else if (now - lastNavMs >= 160) {
      if (up && cursor > 0) {
        --cursor;
        syncScroll();
        lastNavMs = now;
        uiDirty = true;
      }
      if (down && now - lastNavMs >= 160 &&
          cursor < (int)episodes.size() - 1) {
        ++cursor;
        syncScroll();
        lastNavMs = now;
        uiDirty = true;
      }
    }
  }

  // ---- 音量（播放中实时生效）----
  static uint32_t lastVolMs = 0;
  if (volUp && now - lastVolMs >= 150 && player.volume < VOLUME_MAX) {
    ++player.volume;
    lastVolMs = now;
    audio.setVolume(player.volume);
    uiDirty = true;
  }
  if (volDn && now - lastVolMs >= 150 && player.volume > 0) {
    --player.volume;
    lastVolMs = now;
    audio.setVolume(player.volume);
    uiDirty = true;
  }

  // ---- 0：列表页打开 WiFi 配网热点 ----
  static bool lastCfg = false;
  bool cfg = kb.isKeyPressed('0');
  if (cfg && !lastCfg && view == VIEW_LIST) {
    bool connected = openPortal();
    // 配网成功且还没有节目列表 -> 顺手拉取
    if (connected && episodes.empty()) fetchWithProgress();
  }
  lastCfg = cfg;

  // ---- Tab：频道切换页开关（Esc 键 M5Cardputer 键盘库不上报，用 Tab 代替）----
  static bool lastTab = false;
  bool tab = kb.isKeyPressed('\t');
  if (tab && !lastTab) {
    if (view == VIEW_CHANNEL) {
      view = VIEW_LIST;        // 再按一次 Tab 返回列表
    } else if (view == VIEW_LIST) {
      chanCursor = chanIdx;    // 打开时光标定位到当前频道
      view       = VIEW_CHANNEL;
    }
    uiDirty = true;
  }
  lastTab = tab;

  // ---- Enter（边沿触发，长按不连发）----
  // 频道页：Enter 确认切换；列表页：Enter 选中当前集 = 打开它的播放页
  // （未在播则开始播放）；播放页：Enter = 暂停 / 续播 / 重播
  static bool lastEnter = false;
  if (enter && !lastEnter) {
    if (view == VIEW_CHANNEL) {
      switchChannel(chanCursor);
    } else if (episodes.empty()) {
      fetchWithProgress();                    // 列表为空：重试
    } else if (view == VIEW_PLAYER) {
      if (audio.isRunning() && !paused) {
        // 正在播 -> 暂停
        audio.pauseResume();
        paused = true;
      } else if (paused) {
        // 暂停中 -> 续播；暂停太久 CDN 断连时 loop 里会转 EOF 提示
        audio.pauseResume();
        paused = false;
        audio.loop();                         // 立即试喂一次，探测连接死活
        if (!audio.isRunning()) statusMsg = "连接已断，Enter重播";
      } else {
        startPlayback(player.current >= 0 ? player.current : cursor);
      }                                      // 已播完/连接失败 -> 重新播放
      uiDirty = true;
    } else if (player.current == cursor) {
      // 列表页选中正在播/已暂停的集 -> 只进播放页，不改变播放状态
      view = VIEW_PLAYER;
      uiDirty = true;
    } else {
      startPlayback(cursor);                  // 播放新选中的节目
    }
  }
  lastEnter = enter;
}

// --------------------------- 主流程 ---------------------------

void setup() {
  auto cfg = M5.config();
  // 板载扬声器/麦克风完全交给 ESP32-audioI2S（I2S_NUM_1）自己驱动；
  // M5Unified 只负责屏幕、板卡识别和内部 I2C 总线
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5Cardputer.begin(cfg);

  Serial.begin(115200);
  auto& d = M5Cardputer.Display;
  Serial.printf("[app] display %dx%d\n", (int)d.width(), (int)d.height());

  // 列表布局按实际屏幕高度计算（CardputerADV 画布 240x135）
  listTop = 20;
  lineHeight = 19;
  listLines = (d.height() - listTop) / lineHeight;
  if (listLines < 1) listLines = 1;

  d.setTextDatum(m5gfx::textdatum_t::top_left);
  d.setFont(&fonts::efontCN_16);
  d.fillScreen(C_BG);
  d.setTextColor(C_TEXT, C_BG);

  // ---- 频道装载：SD /channels.txt > NVS 缓存 > 编译期内嵌 ----
  d.drawString("读取频道...", 2, 1);
  {
    sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdReady = SD.begin(PIN_SD_CS, sdSPI, 25000000);
    Serial.printf("[sd] %s\n", sdReady ? "mounted" : "not found");
  }

  bool fromSD = false;
  if (sdReady) {
    File f = SD.open("/channels.txt");
    if (f) {
      std::string txt;
      txt.reserve(f.size());
      while (f.available()) txt.push_back((char)f.read());
      f.close();
      if (txt.size() >= 3 && (uint8_t)txt[0] == 0xEF &&
          (uint8_t)txt[1] == 0xBB && (uint8_t)txt[2] == 0xBF) {
        txt.erase(0, 3);  // 去 UTF-8 BOM
      }
      size_t pos = 0;
      while (pos < txt.size() && channels.size() < 16) {
        size_t eol = txt.find('\n', pos);
        std::string line = txt.substr(
            pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? txt.size() : eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t");
        line = line.substr(b, e - b + 1);
        if (line[0] == '#') continue;  // 注释行
        // 按 | 分段并去首尾空白
        std::vector<std::string> parts;
        size_t p2 = 0;
        while (true) {
          size_t bar = line.find('|', p2);
          std::string seg = line.substr(
              p2, bar == std::string::npos ? std::string::npos : bar - p2);
          size_t sb = seg.find_first_not_of(" \t");
          parts.push_back(sb == std::string::npos
                              ? ""
                              : seg.substr(sb,
                                           seg.find_last_not_of(" \t") - sb + 1));
          if (bar == std::string::npos) break;
          p2 = bar + 1;
        }
        if (parts.size() < 2 || parts[0].empty() ||
            parts[1].compare(0, 4, "http") != 0) {
          Serial.printf("[chan] skip bad line: %s\n", line.c_str());
          continue;
        }
        ChannelEntry ce;
        ce.name     = parts[0];
        ce.rss      = parts[1];
        channels.push_back(ce);
      }
      fromSD = !channels.empty();
      Serial.printf("[chan] SD: %d channels\n", (int)channels.size());
    } else {
      Serial.println("[chan] SD: no /channels.txt");
    }
  }

  if (fromSD) {
    // 成功解析 SD 配置 -> 缓存进 NVS，拔卡/坏卡时兜底
    // （blob2 = v2 布局：[u8 名长][名][u8 rss长][rss]，无封面字段）
    std::string blob;
    for (auto& c : channels) {
      blob.push_back((char)c.name.size());
      blob.append(c.name);
      blob.push_back((char)c.rss.size());
      blob.append(c.rss);
    }
    Preferences p;
    p.begin("chanlist", false);
    p.putBytes("blob2", blob.data(), blob.size());
    p.end();
  } else {
    // SD 没配好 -> 读上次缓存的频道列表
    Preferences p;
    p.begin("chanlist", true);
    size_t n = p.getBytesLength("blob2");
    if (n > 0) {
      std::vector<char> buf(n);
      p.getBytes("blob2", buf.data(), n);
      p.end();
      size_t cpos = 0;
      while (cpos + 2 <= buf.size() && channels.size() < 16) {
        size_t nl = (uint8_t)buf[cpos++];
        if (cpos + nl > buf.size()) break;
        ChannelEntry ce;
        ce.name.assign(buf.data() + cpos, nl);
        cpos += nl;
        if (cpos >= buf.size()) break;
        size_t rl = (uint8_t)buf[cpos++];
        if (cpos + rl > buf.size()) break;
        ce.rss.assign(buf.data() + cpos, rl);
        cpos += rl;
        channels.push_back(ce);
      }
      if (!channels.empty()) {
        Serial.printf("[chan] NVS cache: %d channels\n", (int)channels.size());
      }
    } else {
      p.end();
    }
  }

  if (channels.empty()) {
    // 最终兜底：编译期内嵌频道表（channels.txt 缺失也能用）
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
      ChannelEntry ce;
      ce.name = CHANNELS[i].name;
      ce.rss  = CHANNELS[i].rss;
      channels.push_back(ce);
    }
    Serial.println("[chan] using embedded channels");
  }

  // 上次选择的频道（NVS，越界回退到第 0 个）
  {
    Preferences p;
    p.begin("chan", true);
    chanIdx = p.getUChar("idx", 0);
    p.end();
    if (chanIdx < 0 || chanIdx >= (int)channels.size()) chanIdx = 0;
  }
  Serial.printf("[app] channel %d: %s\n", chanIdx,
                channels[chanIdx].name.c_str());

  d.fillScreen(C_BG);
  d.setTextColor(C_TEXT, C_BG);
  d.drawString("连接WiFi...", 2, 1);

  // 1) WiFi：WiFiManager 全托管 —— 先试 NVS 已存凭据，失败自动开热点
  //    "PodcastPlayer" + captive portal（手机连上自动弹出配置页）
  WiFiManager wm;
  wm.setConnectTimeout(10);          // 已存凭据的连接尝试等待
  wm.setConfigPortalTimeout(180);    // 无人操作 3 分钟自动关闭热点
  wm.setAPCallback([](WiFiManager*) { drawPortalScreen(); });
  wifiOk = wm.autoConnect(AP_NAME);
  if (wifiOk) WiFi.setSleep(false);  // 在线串流需要稳定吞吐
  Serial.printf("[app] wifi %s\n", wifiOk ? "connected" : "FAILED");

  // 2) RSS 节目列表（ES8311 延迟到首次播放时初始化，需 I2S 时钟先运行）
  d.fillScreen(C_BG);
  if (wifiOk) {
    fetchWithProgress();
  } else {
    d.setTextColor(C_WARN, C_BG);
    d.drawString("未连接WiFi", 2, 1);
    d.drawString("按0进入WiFi设置", 2, 21);
    d.setTextColor(C_TEXT, C_BG);
  }

  uiDirty = true;
}

void loop() {
  M5Cardputer.update();
  handleKeys();

  // 喂解码器（暂停时内部直接返回）。EOF 检测：
  // 上一轮还在运行、这一轮没了、且不是用户主动暂停 -> 播放完毕
  audio.loop();
  static bool wasRunning = false;
  bool running = audio.isRunning();
  if (wasRunning && !running && !paused && statusMsg.empty()) {
    statusMsg = "播放完毕";
    uiDirty = true;
  }
  wasRunning = running;

  // 播放页进度条每秒局部刷新（静止页面不整屏重绘）
  static uint32_t lastProgressMs = 0;
  if (view == VIEW_PLAYER && audio.isRunning() && !paused &&
      millis() - lastProgressMs >= 1000) {
    lastProgressMs = millis();
    drawPlayerPage(false);
  }

  // WiFi 断线自动重连（低频检查）
  static uint32_t lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 5000) {
    lastWifiCheck = millis();
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (!ok) WiFi.reconnect();
    if (ok != wifiOk) {
      wifiOk = ok;
      uiDirty = true;
    }
  }

  if (uiDirty) {
    drawUI();
    uiDirty = false;
  }
}
