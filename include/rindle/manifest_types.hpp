#ifndef RIVULET_MANIFEST_TYPES_HPP
#define RIVULET_MANIFEST_TYPES_HPP

#pragma once

#include "types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

namespace rivulet {

struct ManifestContent {
    int version = 1;

    // Dataset configuration
    std::size_t seq_length{};
    std::size_t future_horizon{};
    std::vector<std::string> feature_columns;
    std::optional<std::string> target_column;
    TimeMode time_mode{TimeMode::UTC_NS};
    bool row_major{false};
    ScalerKind scaler_kind{ScalerKind::None};

    // Statistics
    std::size_t total_tickers{};
    std::size_t total_windows{};
    std::size_t total_input_rows{};

    // Per-ticker breakdown
    std::vector<TickerStats> ticker_stats;
    std::unordered_map<std::string, std::size_t> ticker_index;

    std::filesystem::path input_dir;
    std::filesystem::path output_dir;

    // Build metadata
    std::string build_timestamp;

    void build_ticker_index() {
        ticker_index.clear();
        ticker_index.reserve(ticker_stats.size());
        for (std::size_t i = 0; i < ticker_stats.size(); ++i) {
            ticker_index.emplace(ticker_stats[i].ticker, i);
        }
    }

    const TickerStats* find_stats(std::string_view name) const {
        auto it = ticker_index.find(std::string(name));
        if (it == ticker_index.end()) {
            return nullptr;
        }
        return &ticker_stats[it->second];
    }
};

using TickerMap = std::unordered_map<std::string, const TickerStats*>;

} // namespace rivulet

#endif // RIVULET_MANIFEST_TYPES_HPP

