/*==============================================================================
File: src/internal/driver.hpp

  Overview:
    Declares the Driver orchestrator that turns a `DatasetConfig` into on-disk
    artifacts and manifest metadata for the dataset build pipeline.

  Key functionality:
    - Provide a `run` entry point that discovers ticker inputs, builds windows,
      persists manifests, and reports aggregate statistics.
    - Expose the generated manifest so helper components (e.g., window maker)
      can look up ticker statistics during the run.
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

    class Driver {
    public:
        explicit Driver(DatasetConfig config, unsigned int thread_count = 0);

        // Main entry point: run the full pipeline
        DriverResult run();

        // Get the built manifest
        const Manifest& get_manifest() const { return manifest_; }

    private:
        DatasetConfig config_;
        unsigned int thread_count_;
        Catalog catalog_;
        Manifest manifest_;

        // Process a single ticker (index is used for thread-safe stats recording)
        bool process_ticker(
            const WorkItem& item,
            std::size_t index,
            std::string& error_msg
        );

        // Finalize: write manifest and summary
        bool finalize(std::string& error_msg);

        // Print summary to console
        void print_summary(const DriverResult& result) const;
    };

} // namespace rivulet

#endif //RIVULET_DRIVER_HPP
