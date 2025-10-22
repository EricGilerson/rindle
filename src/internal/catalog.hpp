/*==============================================================================
File: src/internal/catalog.hpp

  Overview:
    Declares the Catalog helper responsible for discovering ticker CSV files
    and capturing the statistics needed by the build manifest.

  Key functionality:
    - Scan the configured input directory, normalizing filenames into ticker
      symbols and producing work items for the driver.
    - Resolve the base output directory for ticker artifacts and optional
      combined exports.
    - Accumulate ticker statistics so downstream components can report totals.
==============================================================================*/

#ifndef RIVULET_CATALOG_HPP
#define RIVULET_CATALOG_HPP

#pragma once
#include "rindle/types.hpp"
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>

namespace rivulet {

    class Catalog {
    public:
        explicit Catalog(const DatasetConfig& config);

        // Discover all input CSV files
        bool discover(std::string& error_msg);

        // Get list of work items
        const std::vector<WorkItem>& work_items() const { return work_items_; }
        std::size_t num_tickers() const { return work_items_.size(); }

        // Get output paths
        std::filesystem::path output_dir_for_ticker(const std::string& ticker) const;
        std::filesystem::path combined_output_dir() const;

        // Track statistics
        void record_ticker_stats(const TickerStats& stats);
        const std::vector<TickerStats>& all_stats() const { return stats_; }

        // Summary
        std::size_t total_windows_created() const;
        std::size_t total_rows_processed() const;

    private:
        const DatasetConfig& config_;
        std::vector<WorkItem> work_items_;
        std::vector<TickerStats> stats_;

        // Extract ticker symbol from filename (e.g., "AAPL.csv" -> "AAPL")
        static std::string normalize_ticker(const std::filesystem::path& path);
    };

} // namespace rivulet

#endif //RIVULET_CATALOG_HPP
