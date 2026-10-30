Host-side parser tests (no hardware needed).

    git clone --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson.git /tmp/aj
    g++ -std=c++17 -Wall -I src -I /tmp/aj/src tests/host/test_catalog.cpp -o /tmp/test_catalog && /tmp/test_catalog
