//
// Created by Eric Gilerson on 10/6/25.
//

#ifndef RIVULET_TYPES_HPP
#define RIVULET_TYPES_HPP

#endif //RIVULET_TYPES_HPP

#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

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

} // namespace rivulet
