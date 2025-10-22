/*==============================================================================
File: src/internal/csv_io.hpp

  Overview:
    Declares the columnar CSV frame representation and scaler artifact helpers
    shared across the dataset build pipeline.

  Key functionality:
    - Define identifiers for fold-specific scaler metadata and their payloads.
    - Provide the CsvIO interface for reading canonical time-series CSV files
      and persisting scaler parameters to disk.
    - Expose a timestamp parser that converts flexible input formats to
      nanoseconds.
==============================================================================*/
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

#include "rindle/types.hpp" // for Timestamp, Nanoseconds, etc.

namespace rivulet {

// --- Fold & Scaler identification (artifacts only; no math here) ---
struct FoldId {
    int index{};                   // walk-forward fold index
    std::int64_t train_start_ns{}; // inclusive
    std::int64_t train_end_ns{};   // inclusive
};

struct ScalerKey {
    FoldId fold;                      // fold that the scaler was fit on
    std::string ticker;               // per-ticker scaler
    std::string feature_set_version;  // hash or name for selected features
    std::string schema_hash;          // hash of input schema (names+dtypes)
};

struct ScalerArtifact {
    ScalerKey key;
    std::string scaler_type;                 // e.g., "standard", "minmax"
    std::vector<std::string> feature_names;  // ordered feature list
    std::vector<double> param_a;             // e.g., means or mins
    std::vector<double> param_b;             // e.g., variances or maxes
};

// --- CSV reading: Date, f1, f2, ... fn ---
// We parse into a columnar frame: a vector of timestamps and N feature columns.
// Missing numeric cells become NaN; header is required and must have first column named "Date".
struct CsvFrame {
    // nanosecond timestamps aligned to rows
    std::vector<std::int64_t> date_ns;
    // feature names in order as read (excludes "Date")
    std::vector<std::string> feature_names;
    // feature columns in column-major form: features[c][row]
    std::vector<std::vector<double>> features;
};

class CsvIO {
public:
    // Read a time-series CSV of the form: Date, f1, f2, ..., fn
    // - Accepts ISO-like "YYYY-MM-DD HH:MM:SS" or integer epoch seconds/millis/micros/nanos in Date.
    // - Trims whitespace around cells.
    // - Returns false and sets error_msg on any structural inconsistency.
    static bool read_time_series_csv(
        const std::filesystem::path& path,
        CsvFrame* out,
        std::string& error_msg);

    // Persist scaler artifacts (fit elsewhere) for artifact-first pipeline.
    static bool write_scaler_artifacts_csv(
        const std::filesystem::path& path,
        const std::vector<ScalerArtifact>& artifacts,
        std::string& error_msg,
        bool append = false);

    // Load scaler artifacts
    static bool read_scaler_artifacts_csv(
        const std::filesystem::path& path,
        std::vector<ScalerArtifact>* out,
        std::string& error_msg);
};

// Timestamp parsing helpers
// Attempts integer fast-path (epoch s/ms/us/ns) then falls back to "YYYY-MM-DD[ HH:MM:SS]".
std::optional<std::int64_t> parse_date_to_ns(const std::string& cell);

} // namespace rivulet


