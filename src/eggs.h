// Hidden terminal features. This committed file is the public stub: every hook is a no-op.
// The real implementation lives in src/eggs_private.h, which is gitignored and never committed.
// If that file is present, it is used instead of this stub.
#pragma once

#include <string>
#include <vector>

#if __has_include("eggs_private.h")
#include "eggs_private.h"
#else
static inline bool eggCommand(const std::string&, const std::vector<std::string>&, const std::string&) { return false; }
static inline bool eggWantsSecret() { return false; }
static inline void eggSecret(const std::string&) {}
static inline void eggTick(bool) {}
#endif
