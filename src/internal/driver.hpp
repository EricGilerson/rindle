//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/driver.hpp

  Purpose:
    Orchestrates the end-to-end dataset build for all tickers. This is the
    entrypoint the CLI calls.

  Responsibilities:
    - Load configuration (seq_length, horizon, features, paths, time mode).
    - For each ticker: read → make windows → write outputs.
    - Write window manifests as {ticker}_windows.parquet in output_dir (flat structure).
    - Write the final manifest.json.
    - Emit a concise summary (row counts, window counts).

  Notes:
    - Driver does not contain parsing or window math; it wires modules together.
    - All outputs go directly to output_dir without per-ticker subdirectories.
    - No cleaning stage - works directly with raw CSV data.
==============================================================================*/

#ifndef RIVULET_DRIVER_HPP
#define RIVULET_DRIVER_HPP

#pragma once
#include "rindle/types.hpp"
#include "catalog.hpp"
#include "window_maker.hpp"
#include "manifest.hpp"
#include <string>

namespace rivulet {

    struct DriverResult {
        bool success = false;
        std::string message;
        std::size_t tickers_processed = 0;
        std::size_t total_windows = 0;
        std::size_t total_rows = 0;
    };

    // Global manifest accessible to window_maker
    inline Manifest manifest;

    class Driver {
    public:
        explicit Driver(DatasetConfig config);

        // Main entry point: run the full pipeline
        DriverResult run();

        // Get the built manifest
        const Manifest& get_manifest() const { return manifest_; }

    private:
        DatasetConfig config_;
        Catalog catalog_;
        Manifest manifest_;

        // Process a single ticker
        bool process_ticker(
            const WorkItem& item,
            std::string& error_msg
        );

        // Finalize: write manifest and summary
        bool finalize(std::string& error_msg);

        // Print summary to console
        void print_summary(const DriverResult& result) const;
    };

} // namespace rivulet

#endif //RIVULET_DRIVER_HPP
