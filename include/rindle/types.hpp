//
// Created by Eric Gilerson on 10/6/25.
//
/*==============================================================================
File: include/rindle/types.hpp

  Overview:
    Central type definitions shared across the library, covering timestamps,
    dataset configuration objects, scaler descriptors, and lightweight status
    helpers.

  Key functionality:
    - Establishes canonical time representations and key aliases.
    - Provides structs for dataset configuration, ticker statistics, and work
      items consumed by the build pipeline.
    - Supplies reusable Status/Result helpers for consistent error handling.
==============================================================================*/

#ifndef RIVULET_TYPES_HPP
#define RIVULET_TYPES_HPP

#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>
#include "dataset_types.hpp"
#include "scaler.hpp"

namespace rivulet {

    // High-resolution event timestamp (nanoseconds since epoch).
    using Nanoseconds = std::chrono::nanoseconds;
    using Timestamp  = std::chrono::time_point<std::chrono::system_clock, Nanoseconds>;

    // Logical key for a stream (symbol, user id, athlete id, etc.).
    using Key = std::string;

    // Simple field map for v0 (numeric features). You can generalize later.
    using FieldMap = std::unordered_map<std::string, double>;

    // One input row.
    struct Event {
        Key key;
        int index;
        Timestamp event_time;  // event-time, not arrival-time
        FieldMap fields;
    };

    // Features returned to callers.
    struct FeatureVector {
        std::vector<std::string> names;
        std::vector<double> values;
    };

    // Basic status and result types for clearer error handling.
    struct Status {
        bool ok = true;
        std::string message;

        static Status OK() { return {true, ""}; }
        static Status Error(std::string msg) { return {false, std::move(msg)}; }
    };

    template <typename T>
    struct Result {
        std::optional<T> value;
        Status status;
        explicit operator bool() const { return status.ok && value.has_value(); }
    };

    struct EngineConfig {
        std::string store_path;                       // on-disk location (used later)
        std::chrono::milliseconds watermark_delay{2000};
        std::size_t max_window_samples{2048};
    };

    // Time mode for datasets
    enum class TimeMode {
        UTC_NS,    // Timestamps in UTC nanoseconds
        ORDINAL    // Row-index based ordering (no Date column)
    };

    // Configuration for dataset building
    struct DatasetConfig {
        std::filesystem::path input_dir;
        std::filesystem::path output_dir;
        std::vector<std::string> feature_columns;
        std::optional<std::string> target_column;
        std::size_t seq_length;           // L: window length in rows
        std::size_t future_horizon;       // H: target horizon in rows
        TimeMode time_mode = TimeMode::UTC_NS;
        bool row_major = false;           // false = time-major flattening (default)
        ScalerKind scaler_kind = ScalerKind::Standard;
    };

    struct FeatureScalerParams {
        std::string feature;
        ScalerParams params;
    };

    // Summary statistics for a ticker
    struct TickerStats {
        std::string ticker;
        std::size_t input_rows = 0;
        std::size_t processed_rows = 0;
        std::size_t windows_created = 0;
        bool was_sorted = false;
        ScalerKind scaler_kind = ScalerKind::None;
        std::vector<FeatureScalerParams> feature_scalers;
    };

    // Work item for processing a single ticker
    struct WorkItem {
        std::string ticker;
        std::filesystem::path input_path;
        std::filesystem::path output_dir;
    };

} // namespace rivulet

#endif //RIVULET_TYPES_HPP
