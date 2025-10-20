//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/catalog.hpp

  Purpose:
    Discovers input files (one per ticker), normalizes ticker names, and resolves
    output paths for per-ticker and combined artifacts.

  Responsibilities:
    - Scan an input directory for CSVs; map filenames to ticker symbols.
    - Provide an iterable list of "work items" with input and output locations.
    - Track running counts for rows/windows to populate the manifest.

  Notes:
    - Only file discovery and naming conventions live here; no parsing logic.
==============================================================================*/

#ifndef RIVULET_CATALOG_HPP
#define RIVULET_CATALOG_HPP

#pragma once
#include "rivulet/types.hpp"
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
