//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/driver.cpp

  Purpose:
    Wires catalog, csv_io, window_maker, and manifest into a single run.
    No cleaning stage - works directly with raw input CSVs.

  Responsibilities:
    - Iterate tickers: read → window → write per-ticker outputs.
    - Handle error propagation and summary reporting.
    - Write manifest.json at the end with final counts and time mode.
    - All outputs saved flat to output_dir: {ticker}_windows.csv, manifest.json

  Notes:
    - Keeps the control flow and logging in one place; no heavy logic inside.
    - Raw CSV data is used as-is; window_maker works with row indices.
==============================================================================*/

#include "rivulet/driver.hpp"
#include "rivulet/csv_io.hpp"
#include "rivulet/window_manifest.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace rivulet {

//==============================================================================
// Constructor
//==============================================================================

Driver::Driver(DatasetConfig config)
    : config_(std::move(config))
    , catalog_(config_)
    , manifest_()
{
}

//==============================================================================
// Main Pipeline
//==============================================================================

DriverResult Driver::run() {
    DriverResult result;
    std::string error_msg;

    // Step 1: Discover all input CSV files
    std::cout << "Discovering input files...\n";
    if (!catalog_.discover(error_msg)) {
        result.success = false;
        result.message = "Catalog discovery failed: " + error_msg;
        return result;
    }

    std::cout << "Found " << catalog_.num_tickers() << " tickers\n";

    // Step 2: Create output directory
    std::error_code ec;
    std::filesystem::create_directories(config_.output_dir, ec);
    if (ec) {
        result.success = false;
        result.message = "Failed to create output directory: " + ec.message();
        return result;
    }

    // Step 3: Process each ticker
    const auto& work_items = catalog_.work_items();
    for (std::size_t i = 0; i < work_items.size(); ++i) {
        const auto& item = work_items[i];

        std::cout << "[" << (i + 1) << "/" << work_items.size() << "] Processing "
                  << item.ticker << "...\n";

        if (!process_ticker(item, error_msg)) {
            std::cerr << "  ERROR: " << error_msg << "\n";
            result.success = false;
            result.message = "Failed processing ticker " + item.ticker + ": " + error_msg;
            return result;
        }

        result.tickers_processed++;
    }

    // Step 4: Finalize - write manifest
    std::cout << "\nFinalizing dataset...\n";
    if (!finalize(error_msg)) {
        result.success = false;
        result.message = "Finalization failed: " + error_msg;
        return result;
    }

    // Step 5: Populate result with statistics
    result.success = true;
    result.total_windows = catalog_.total_windows_created();
    result.total_rows = catalog_.total_rows_processed();
    result.message = "Dataset built successfully";

    // Print summary
    print_summary(result);

    return result;
}

//==============================================================================
// Process Single Ticker
//==============================================================================

bool Driver::process_ticker(
    const WorkItem& item,
    std::string& error_msg
) {
    // Step 1: Read CSV using CsvIO to get row count
    CsvFrame frame;
    if (!CsvIO::read_time_series_csv(item.input_path, &frame, error_msg)) {
        return false;
    }

    std::size_t num_rows = frame.date_ns.size();
    std::cout << "  Read " << num_rows << " rows\n";

    // Step 2: Create ticker stats
    TickerStats stats;
    stats.ticker = item.ticker;
    stats.input_rows = num_rows;
    stats.processed_rows = num_rows;  // No cleaning, so all rows are processed
    stats.was_sorted = false;         // Assuming input is already sorted

    // Step 3: Build windows using window_maker
    SingleTickerWindowSpec window_spec;
    window_spec.ticker = item.ticker;
    window_spec.window_length_ns = config_.seq_length;  // Using as row count
    window_spec.step_ns = 1;  // Step by 1 row
    window_spec.horizon_ns = config_.future_horizon;  // Using as row count
    window_spec.with_targets = config_.target_column.has_value();

    // Generate windows
    std::vector<WindowRow> windows = make_windows_for_ticker(
        window_spec,
        &error_msg,
        &stats
    );

    if (!error_msg.empty()) {
        return false;
    }

    stats.windows_created = windows.size();
    std::cout << "  Created " << windows.size() << " windows\n";

    // Step 4: Write window manifest CSV - flat structure: {ticker}_windows.csv
    std::filesystem::path window_manifest_path =
        config_.output_dir / (item.ticker + "_windows.csv");

    if (!write_windows_manifest_csv(window_manifest_path.string(), windows, &error_msg)) {
        return false;
    }

    std::cout << "  Wrote window manifest to: " << window_manifest_path.filename() << "\n";

    // Step 5: Record stats in catalog
    catalog_.record_ticker_stats(stats);

    return true;
}

//==============================================================================
// Finalize - Write Manifest
//==============================================================================

bool Driver::finalize(std::string& error_msg) {
    // Build manifest from config and catalog
    manifest_.populate(config_, catalog_);

    // Store manifest in global for window_maker access
    manifest = manifest_;

    // Write manifest.json to output_dir
    std::filesystem::path manifest_path = config_.output_dir / "manifest.json";

    if (!manifest_.write_to_file(manifest_path, error_msg)) {
        return false;
    }

    std::cout << "Wrote manifest to: " << manifest_path << "\n";
    return true;
}

//==============================================================================
// Print Summary
//==============================================================================

void Driver::print_summary(const DriverResult& result) const {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Dataset Build Summary\n";
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Status:            " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "Tickers processed: " << result.tickers_processed << "\n";
    std::cout << "Total windows:     " << result.total_windows << "\n";
    std::cout << "Total rows:        " << result.total_rows << "\n";
    std::cout << "Output directory:  " << config_.output_dir << "\n";
    std::cout << std::string(60, '=') << "\n";
}

} // namespace rivulet