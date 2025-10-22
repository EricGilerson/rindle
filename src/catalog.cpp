/*==============================================================================
  File: src/catalog.cpp

Overview:
    Discovers ticker CSV files, normalizes their symbols, and captures the
    statistics needed to populate the build manifest.

Key functionality:
    - Scan the configured input directory for `.csv` files and build work items.
    - Resolve the base output directory for each ticker artifact.
    - Accumulate processed row and window totals for later reporting.
==============================================================================*/

#include "internal/catalog.hpp"

namespace rivulet {
    Catalog::Catalog(const DatasetConfig& config) : config_(config) {}

    bool Catalog::discover(std::string& error_msg) {
        if (!std::filesystem::exists(config_.input_dir) ||
            !std::filesystem::is_directory(config_.input_dir)) {
            error_msg = "Input directory does not exist or is not a directory: " +
                        config_.input_dir.string();
            return false;
            }

        work_items_.clear();

        for (const auto& entry : std::filesystem::directory_iterator(config_.input_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                std::string ticker = normalize_ticker(entry.path());
                if (ticker.empty()) {
                    continue; // Skip files that don't yield a valid ticker
                }

                WorkItem item;
                item.ticker = ticker;
                item.input_path = entry.path();
                item.output_dir = output_dir_for_ticker(ticker);
                work_items_.push_back(std::move(item));
            }
        }

        if (work_items_.empty()) {
            error_msg = "No CSV files found in input directory: " + config_.input_dir.string();
            return false;
        }

        return true;
    }
    std::filesystem::path Catalog::output_dir_for_ticker(const std::string& ticker) const {
        return config_.output_dir;
    }
    //should not be used
    std::filesystem::path Catalog::combined_output_dir() const {
        return config_.output_dir / "combined";
    }
    void Catalog::record_ticker_stats(const TickerStats& stats) {
        stats_.push_back(stats);
    }
    std::size_t Catalog::total_windows_created() const {
        std::size_t total = 0;
        for (const auto& s : stats_) {
            total += s.windows_created;
        }
        return total;
    }
    std::size_t Catalog::total_rows_processed() const {
        std::size_t total = 0;
        for (const auto& s : stats_) {
            total += s.processed_rows;
        }
        return total;
    }

    std::string Catalog::normalize_ticker(const std::filesystem::path& path) {
        std::string filename = path.stem().string();
        std::string ticker;
        for (char c : filename) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                ticker += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return ticker;
    }
}