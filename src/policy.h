#pragma once

#include <string>
#include <unordered_map>

#include "metrics.h"

// Tiers in ascending order of urgency.
enum class Tier { IDLE, NOMINAL, WARNING, EMERGENCY };

// The set of option changes to apply for a given tier.
// cf_options go to DB::SetOptions()    (ColumnFamily-level).
// db_options go to DB::SetDBOptions()  (DB-level, e.g. thread pool size).
struct Action {
    std::unordered_map<std::string, std::string> cf_options;
    std::unordered_map<std::string, std::string> db_options;
};

// Classify the current DB state into a control tier.
Tier ClassifyTier(const Metrics& m);

// Return the full set of option changes to apply for a given tier.
// Every tier returns a complete set of options so that settings always
// converge to the correct values regardless of prior state.
Action TierToAction(Tier tier);

// Human-readable tier name for logging.
const char* TierName(Tier tier);
