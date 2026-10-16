// 由 tools/gen_channels.py 自动生成（数据源：channels.txt）
// 手改无效，下次编译会被覆盖；加频道请编辑 channels.txt
#pragma once

// 频道定义：名字 / RSS 地址（封面为运行时按名字生成的文字徽章）
struct ChannelDef {
  const char* name;
  const char* rss;
};

static const ChannelDef CHANNELS[] = {
    {"霓达播客", "https://feed.xyzfm.space/xlgejk3wbj8q"},
};

static const int CHANNEL_COUNT =
    sizeof(CHANNELS) / sizeof(CHANNELS[0]);
