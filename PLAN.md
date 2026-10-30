# Search + Charts plan (Cardputer ADV)

Baseline: unmodified upstream builds clean with `pio run -e cardputer-adv-podcast` (branch feature/search-charts).

Resumable steps (each ends with a build + commit):
1. [ ] Add JSON parsing (ArduinoJson or hand-rolled) and HTTPS fetch helper for Apple APIs.
2. [ ] Search: keyboard input page -> `https://itunes.apple.com/search?term=...&media=podcast&country=au&limit=20`
       -> list of shows (name, feedUrl). Selecting a show loads its RSS via the existing fetchEpisodes().
3. [ ] Charts: `https://rss.marketingtools.apple.com/api/v2/au/podcasts/top/25/podcasts.json`
       -> ranked list (rank = array position). Selecting a show -> feedUrl via itunes lookup (`id=`).
4. [ ] Tab/menu wiring: new Search and Charts views alongside existing list/player/channel views.
5. [ ] Hardware test on device (serial log via tools/serial_dump.py): heap during JSON parse, no crash.

Constraints:
- ADV has no PSRAM; keep JSON responses streamed/bounded (cap bytes like MAX_FEED_BYTES).
- Storefront default: au (configurable constant).
- Charts/search are Apple's public endpoints (no key). Feed URLs are whatever the publisher serves.
