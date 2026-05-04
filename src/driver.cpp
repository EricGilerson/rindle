/*==============================================================================
  File: src/driver.cpp

Overview:
    Coordinates the dataset build by wiring catalog discovery, CSV ingestion,
    scaler fitting, window generation, and manifest emission into a single run.
    Tickers are processed in parallel using a thread pool.
==============================================================================*/

#include "internal/driver.hpp"
#include "internal/csv_io.hpp"
#include "internal/thread_pool.hpp"
#include "internal/window_manifest.hpp"
#include "rindle/scaler.hpp"

#include <atomic>
#include <iostream>
#include <filesystem>
#include <limits>
#include <mutex>
#include <sstream>

namespace rivulet {

Driver::Driver(DatasetConfig config, unsigned int thread_count)
    : config_(std::move(config))
    , thread_count_(thread_count)
    , catalog_(config_)
    , manifest_()
{
}

DriverResult Driver::run() {
    DriverResult result;
    std::string error_msg;

    std::cout << "Discovering input files...\n";
    if (!catalog_.discover(error_msg)) {
        result.success = false;
        result.message = "Catalog discovery failed: " + error_msg;
        return result;
    }

    std::cout << "Found " << catalog_.num_tickers() << " tickers\n";

    std::error_code ec;
    std::filesystem::create_directories(config_.output_dir, ec);
    if (ec) {
        result.success = false;
        result.message = "Failed to create output directory: " + ec.message();
        return result;
    }

    const auto& work_items = catalog_.work_items();
    const std::size_t n = work_items.size();

    catalog_.prepare_stats_slots(n);

    std::vector<std::string> errors(n);
    std::atomic<bool> any_failed{false};

    {
        ThreadPool pool(thread_count_);
        std::vector<std::future<void>> futures;
        futures.reserve(n);

        for (std::size_t i = 0; i < n; ++i) {
            futures.push_back(pool.submit([this, &work_items, &errors, &any_failed, i]() {
                if (any_failed.load(std::memory_order_relaxed)) return;
                std::string err;
                if (!process_ticker(work_items[i], i, err)) {
                    errors[i] = std::move(err);
                    any_failed.store(true, std::memory_order_relaxed);
                }
            }));
        }

        for (auto& f : futures) f.get();
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!errors[i].empty()) {
            result.success = false;
            result.message = "Failed processing ticker " + work_items[i].ticker + ": " + errors[i];
            return result;
        }
    }

    result.tickers_processed = n;

    std::cout << "\nFinalizing dataset...\n";
    if (!finalize(error_msg)) {
        result.success = false;
        result.message = "Finalization failed: " + error_msg;
        return result;
    }

    result.success = true;
    result.total_windows = catalog_.total_windows_created();
    result.total_rows = catalog_.total_rows_processed();
    result.message = "Dataset built successfully";

    print_summary(result);

    return result;
}

bool Driver::process_ticker(
    const WorkItem& item,
    std::size_t index,
    std::string& error_msg
) {
    std::ostringstream log;

    CsvFrame frame;
    if (!CsvIO::read_time_series_csv(item.input_path, &frame, error_msg)) {
        return false;
    }

    std::size_t num_rows = frame.date_ns.size();
    log << "  [" << item.ticker << "] Read " << num_rows << " rows\n";

    TickerStats stats;
    stats.ticker = item.ticker;
    stats.input_rows = num_rows;
    stats.processed_rows = num_rows;
    stats.was_sorted = false;
    stats.scaler_kind = config_.scaler_kind;

    if (frame.features.size() == frame.feature_names.size()) {
        stats.feature_scalers.reserve(frame.feature_names.size());
        for (std::size_t i = 0; i < frame.feature_names.size(); ++i) {
            auto scaler = make_scaler(config_.scaler_kind);
            std::vector<double> column = frame.features[i];
            scaler->fit(column);

            FeatureScalerParams feature_params;
            feature_params.feature = frame.feature_names[i];
            feature_params.params = scaler->params();
            stats.feature_scalers.push_back(std::move(feature_params));
        }
    }

    SingleTickerWindowSpec window_spec;
    window_spec.ticker = item.ticker;
    if (config_.seq_length > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        error_msg = "Sequence length exceeds supported range";
        return false;
    }
    if (config_.future_horizon > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        error_msg = "Future horizon exceeds supported range";
        return false;
    }

    window_spec.window_length_ns = static_cast<std::int64_t>(config_.seq_length);
    window_spec.step_ns = 1;
    window_spec.horizon_ns = static_cast<std::int64_t>(config_.future_horizon);
    window_spec.with_targets = config_.target_column.has_value();

    std::vector<WindowRow> windows = make_windows_for_ticker(
        window_spec,
        &error_msg,
        &stats
    );

    if (!error_msg.empty()) {
        return false;
    }

    stats.windows_created = windows.size();
    log << "  [" << item.ticker << "] Created " << windows.size() << " windows\n";

    std::filesystem::path window_manifest_path =
        config_.output_dir / (item.ticker + "_windows.parquet");

    if (!write_windows_manifest_parquet(window_manifest_path.string(), windows, &error_msg)) {
        return false;
    }

    log << "  [" << item.ticker << "] Wrote window manifest to: " << window_manifest_path.filename() << "\n";

    catalog_.record_ticker_stats_at(index, std::move(stats));

    {
        static std::mutex cout_mutex;
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << log.str();
    }

    return true;
}

bool Driver::finalize(std::string& error_msg) {
    manifest_.populate(config_, catalog_);

    std::filesystem::path manifest_path = config_.output_dir / "manifest.json";

    if (!manifest_.write_to_file(manifest_path, error_msg)) {
        return false;
    }

    std::cout << "Wrote manifest to: " << manifest_path << "\n";
    return true;
}

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
