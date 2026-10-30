#include <cassert>
#include <cstdio>
#include <sstream>
#include "catalog.h"  // compile with -I src
int main() {
  // search: first two results, one without feedUrl must be skipped
  const char* search = R"({"resultCount":3,"results":[
    {"wrapperType":"track","collectionName":"Show A","artistName":"Host A","collectionId":111,"feedUrl":"https://a.example/feed.xml","genres":["x"]},
    {"collectionName":"No Feed","collectionId":222},
    {"collectionName":"Show C","artistName":"Host C","collectionId":333,"feedUrl":"https://c.example/rss"}]})";
  std::vector<ShowEntry> s;
  size_t n = parseSearchJson(std::string(search), s, 20);
  printf("search n=%zu\n", n);
  for (auto& e : s) printf("  %s | %s | %llu | %s\n", e.name.c_str(), e.artist.c_str(), (unsigned long long)e.id, e.feedUrl.c_str());
  assert(n == 2 && s[0].feedUrl == "https://a.example/feed.xml" && s[1].id == 333);

  // charts keep rank order
  const char* charts = R"({"feed":{"title":"Top","results":[
    {"artistName":"Big Host","id":"1001","name":"First Show","kind":"podcast","url":"https://x"},
    {"artistName":"Other","id":"1002","name":"Second Show"}]}})";
  std::vector<ShowEntry> c;
  n = parseChartsJson(std::string(charts), c, 25);
  printf("charts n=%zu first=%s id=%llu\n", n, c[0].name.c_str(), (unsigned long long)c[0].id);

  // lookup
  std::string f = parseLookupFeedUrl(std::string(R"({"resultCount":1,"results":[{"feedUrl":"https://z/feed"}]})"));
  printf("lookup=%s\n", f.c_str());
  assert(f == "https://z/feed");
  // malformed input must not crash
  std::vector<ShowEntry> bad;
  assert(parseSearchJson(std::string("<html>oops"), bad, 5) == 0);
  printf("ALL OK\n");
}
