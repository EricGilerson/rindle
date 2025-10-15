//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/manifest.hpp

  Purpose:
    Defines the manifest structure and JSON serialization for the dataset build.
    The manifest is the contract your training code relies upon.

  Responsibilities:
    - Record seq_length, future_horizon, ordered feature list, flatten order,
      time mode (utc_ns or ordinal), and whether targets exist.
    - Capture per-ticker and combined row counts and whether sorting occurred.
    - Persist the output layout so downstream code can load deterministically.

  Notes:
    - Bump a version field here if you change the on-disk contract.
==============================================================================*/

#ifndef RIVULET_MANIFEST_HPP
#define RIVULET_MANIFEST_HPP

#pragma once
#include "types.hpp"
#include <filesystem>
#include <string>
#include <vector>

#include "catalog.hpp"

namespace rivulet {
    using TickerMap = std::unordered_map<std::string, const TickerStats*>;
    struct ManifestContent {
        int version = 1;

        // Dataset configuration
        std::size_t seq_length;
        std::size_t future_horizon;
        std::vector<std::string> feature_columns;
        std::optional<std::string> target_column;
        TimeMode time_mode;
        bool row_major;

        // Statistics
        std::size_t total_tickers;
        std::size_t total_windows;
        std::size_t total_input_rows;

        // Per-ticker breakdown
        std::vector<TickerStats> ticker_stats;
        std::unordered_map<std::string, std::size_t> ticker_index;
        std::filesystem::path output_dir;

        void build_ticker_index() {
            ticker_index.clear();
            ticker_index.reserve(ticker_stats.size());
            for (std::size_t i = 0; i < ticker_stats.size(); ++i) {
                ticker_index.emplace(ticker_stats[i].ticker, i);
            }
        }
        const TickerStats* find_stats(std::string_view name) const {
            auto it = ticker_index.find(std::string(name));
            if (it == ticker_index.end()) return nullptr;
            return &ticker_stats[it->second];
        }
        // Build metadata
        std::string build_timestamp;
    };

    class Manifest {
    public:
        Manifest() = default;
        explicit Manifest(const DatasetConfig& config, const Catalog& catalog);

        // Populate from config and catalog
        void populate(const DatasetConfig& config, const Catalog& catalog);

        // Serialize to JSON file
        bool write_to_file(const std::filesystem::path& path, std::string& error_msg) const;

        // Deserialize from JSON file
        static Result<Manifest> read_from_file(
            const std::filesystem::path &path
        );

        // Access content
        const ManifestContent& content() const { return content_; }
        ManifestContent& content_mut() { return content_; }

        const TickerMap& ticker_map() const;


    private:
        ManifestContent content_;

        mutable TickerMap ticker_map_cache_;

        std::string to_json() const;
        static std::optional<Manifest> from_json(const std::string &json_str, std::string &error_msg);
    };

} // namespace rivulet

#endif //RIVULET_MANIFEST_HPP