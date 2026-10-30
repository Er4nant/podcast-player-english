// Show catalog for search and charts, backed by Apple's public JSON endpoints (no API key).
//   search: https://itunes.apple.com/search?term=...&media=podcast&country=au&limit=N
//   charts: https://rss.marketingtools.apple.com/api/v2/au/podcasts/top/N/podcasts.json
//   lookup: https://itunes.apple.com/lookup?id=...   (returns feedUrl for a chart entry)
// Parsing is streamed through ArduinoJson with a filter, so only the fields we keep
// are held in memory (the device has 320 KB SRAM and no PSRAM).
#pragma once

#include <ArduinoJson.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

struct ShowEntry {
  std::string name;     // display name (collectionName / name)
  std::string artist;   // artistName (may be empty)
  uint64_t    id = 0;   // Apple collectionId / id (used for lookup)
  std::string feedUrl;  // RSS URL (only set by search; charts fill it via lookup)
};

// Keep only the fields we use: this bounds the parser's memory.
static inline void catalogFilterSearch(JsonDocument& f) {
  f["results"][0]["collectionName"] = true;
  f["results"][0]["artistName"] = true;
  f["results"][0]["collectionId"] = true;
  f["results"][0]["feedUrl"] = true;
}

static inline void catalogFilterCharts(JsonDocument& f) {
  f["feed"]["results"][0]["name"] = true;
  f["feed"]["results"][0]["artistName"] = true;
  f["feed"]["results"][0]["id"] = true;
}

static inline void catalogFilterLookup(JsonDocument& f) {
  f["results"][0]["feedUrl"] = true;
  f["resultCount"] = true;
}

// IDs arrive as numbers (iTunes search) or numeric strings (Apple charts feed).
static inline uint64_t catalogU64(JsonVariantConst v) {
  if (v.is<uint64_t>()) return v.as<uint64_t>();
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return s ? (uint64_t)strtoull(s, nullptr, 10) : 0;
  }
  return 0;
}

static inline std::string catalogStr(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? std::string(s) : std::string();
}

// Source: anything ArduinoJson can deserialize from (Stream&, const char*, std::istream&).
// Appends up to `max` shows that have a feedUrl. Returns the number appended.
template <typename TSource>
size_t parseSearchJson(TSource&& src, std::vector<ShowEntry>& out, size_t max) {
  JsonDocument filter;
  catalogFilterSearch(filter);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, src,
      DeserializationOption::Filter(filter));
  if (err) return 0;
  size_t before = out.size();
  for (JsonObjectConst r : doc["results"].as<JsonArrayConst>()) {
    if (out.size() - before >= max) break;
    std::string feed = catalogStr(r["feedUrl"]);
    if (feed.empty()) continue;  // shows without a feed can't be played
    ShowEntry e;
    e.name    = catalogStr(r["collectionName"]);
    e.artist  = catalogStr(r["artistName"]);
    e.id      = catalogU64(r["collectionId"]);
    e.feedUrl = feed;
    if (e.name.empty()) continue;
    out.push_back(e);
  }
  return out.size() - before;
}

// Charts keep the server's rank order (array position). Feed URL is filled later.
template <typename TSource>
size_t parseChartsJson(TSource&& src, std::vector<ShowEntry>& out, size_t max) {
  JsonDocument filter;
  catalogFilterCharts(filter);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, src,
      DeserializationOption::Filter(filter));
  if (err) return 0;
  size_t before = out.size();
  for (JsonObjectConst r : doc["feed"]["results"].as<JsonArrayConst>()) {
    if (out.size() - before >= max) break;
    ShowEntry e;
    e.name   = catalogStr(r["name"]);
    e.artist = catalogStr(r["artistName"]);
    e.id     = catalogU64(r["id"]);
    if (e.name.empty() || e.id == 0) continue;
    out.push_back(e);
  }
  return out.size() - before;
}

// Lookup response -> feedUrl (empty if none).
template <typename TSource>
std::string parseLookupFeedUrl(TSource&& src) {
  JsonDocument filter;
  catalogFilterLookup(filter);
  JsonDocument doc;
  if (deserializeJson(doc, src, DeserializationOption::Filter(filter))) return "";
  return catalogStr(doc["results"][0]["feedUrl"]);
}
