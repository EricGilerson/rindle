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
    - For each ticker: read → clean → make windows/targets → write outputs.
    - Append to combined outputs and write the final manifest.json.
    - Emit a concise summary (row counts, dropped rows, any sorting performed).

  Notes:
    - Driver does not contain parsing or window math; it wires modules together.
==============================================================================*/

#ifndef RIVULET_DRIVER_HPP
#define RIVULET_DRIVER_HPP

#pragma once
#include "types.hpp"
#include "catalog.hpp"
#include "clean.hpp"
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
        std::size_t total_dropped = 0;
    };

    inline Manifest manifest;
    class Driver {
    public:
        explicit Driver(DatasetConfig config);

        // Main entry point: run the full pipeline
        DriverResult run();

        // Process a single ticker (used internally, but exposed for testing)
        bool process_ticker(
            const WorkItem& item,
            bool is_first_ticker,
            std::string& error_msg
        );

    private:
        DatasetConfig config_;
        Catalog catalog_;
        Cleaner cleaner_;

        // Write combined outputs header on first ticker
        bool init_combined_outputs(std::string& error_msg);

        // Finalize: write manifest and summary
        bool finalize(std::string& error_msg);

        // Print summary to console
        void print_summary(const DriverResult& result) const;
    };

} // namespace rivulet

#endif //RIVULET_DRIVER_HPP