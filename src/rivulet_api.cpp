#include "rivulet/rivulet.hpp"

// Internal headers (users cannot access these)
#include "rivulet/driver.hpp"
#include "rivulet/catalog.hpp"
#include "rivulet/manifest.hpp"
#include "rivulet/csv_io.hpp"
#include "rivulet/window_manifest.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace rivulet {

// 1. create_config - Build DatasetConfig with validation

Result<DatasetConfig> create_config(
    const std::filesystem::path& input_dir,
    const std::filesystem::path& output_dir,
    const std::vector<std::string>& feature_columns,
    std::size_t seq_length,
    std::size_t future_horizon,
    const std::optional<std::string>& target_column,
    TimeMode time_mode,
    bool row_major
) {
    // Validate inputs
    if (!std::filesystem::exists(input_dir)) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Input directory does not exist: " + input_dir.string())
        };
    }

    if (!std::filesystem::is_directory(input_dir)) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Input path is not a directory: " + input_dir.string())
        };
    }

    if (feature_columns.empty()) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Feature columns list cannot be empty")
        };
    }

    if (seq_length == 0) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Sequence length must be greater than 0")
        };
    }

    if (future_horizon == 0) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Future horizon must be greater than 0")
        };
    }

    // Create output directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        return Result<DatasetConfig>{
            std::nullopt,
            Status::Error("Failed to create output directory: " + ec.message())
        };
    }

    // Build config
    DatasetConfig config;
    config.input_dir = input_dir;
    config.output_dir = output_dir;
    config.feature_columns = feature_columns;
    config.target_column = target_column;
    config.seq_length = seq_length;
    config.future_horizon = future_horizon;
    config.time_mode = time_mode;
    config.row_major = row_major;

    return Result<DatasetConfig>{
        config,
        Status::OK()
    };
}

// 2. build_dataset - Execute full pipeline

Result<ManifestContent> build_dataset(const DatasetConfig& config) {
    // Create driver with the config
    Driver driver(config);

    // Run the complete pipeline:
    // 1. Catalog discovers all CSV files
    // 2. For each ticker:
    //    - Read CSV
    //    - Make scalers
    //    - Build sliding windows
    //    - Write window manifest CSV
    // 3. Aggregate all ticker stats
    // 4. Build and write manifest.json
    DriverResult result = driver.run();

    if (!result.success) {
        return Result<ManifestContent>{
            std::nullopt,
            Status::Error(result.message)
        };
    }

    // Get the manifest that was built during the run
    // The Driver stores it in a global 'manifest' variable (as per driver.hpp)
    const ManifestContent& manifest_content = manifest.content();

    return Result<ManifestContent>{
        manifest_content,
        Status::OK()
    };
}

// 3. get_dataset - Load tensors from built dataset

Result<Dataset> get_dataset(const ManifestContent& manifest_content) {
    std::string error_msg;

    // Validate manifest
    if (manifest_content.total_windows == 0) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Manifest reports zero windows")
        };
    }

    if (!std::filesystem::exists(manifest_content.output_dir)) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Output directory does not exist: " +
                         manifest_content.output_dir.string())
        };
    }

    // Prepare dataset structure
    Dataset dataset;

    const std::size_t n_features = manifest_content.feature_columns.size();
    const std::size_t seq_len = manifest_content.seq_length;
    const bool has_target = manifest_content.target_column.has_value();

    std::vector<WindowRow> all_windows;
    std::size_t total_windows = 0;

    // Cache for loaded CSV data (ticker -> CsvFrame)
    std::unordered_map<std::string, CsvFrame> csv_cache;

    // Read window manifests for each ticker
    for (const auto& ticker_stats : manifest_content.ticker_stats) {
        const std::string& ticker = ticker_stats.ticker;

        // Path to this ticker's window manifest: output_dir/{ticker}_windows.csv
        std::filesystem::path window_manifest_path =
            manifest_content.output_dir / (ticker + "_windows.csv");

        if (!std::filesystem::exists(window_manifest_path)) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Window manifest not found for ticker: " + ticker +
                             " at " + window_manifest_path.string())
            };
        }

        // Read window manifest CSV
        std::ifstream manifest_file(window_manifest_path);
        if (!manifest_file.is_open()) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Failed to open window manifest: " +
                             window_manifest_path.string())
            };
        }

        std::string line;
        // Skip header
        std::getline(manifest_file, line);

        // Parse each window row
        while (std::getline(manifest_file, line)) {
            if (line.empty()) continue;

            WindowRow row;
            std::istringstream ss(line);
            std::string cell;

            // Parse CSV: ticker,window_start,window_end,target_start,target_end
            std::getline(ss, cell, ',');
            row.ticker = cell;

            std::getline(ss, cell, ',');
            row.window_start = std::stoll(cell);

            std::getline(ss, cell, ',');
            row.window_end = std::stoll(cell);

            std::getline(ss, cell, ',');
            if (!cell.empty()) {
                row.target_start = std::stoll(cell);
            }

            std::getline(ss, cell, ',');
            if (!cell.empty()) {
                row.target_end = std::stoll(cell);
            }

            all_windows.push_back(row);
            total_windows++;
        }
    }

    // Allocate tensors
    dataset.X.reshape(total_windows, seq_len, n_features);
    if (has_target) {
        dataset.Y.reshape(total_windows, manifest_content.future_horizon, 1);
    }
    dataset.meta.reserve(total_windows);

    // Now fill tensors by reading actual CSV data from INPUT directory
    for (std::size_t w = 0; w < all_windows.size(); ++w) {
        const auto& window_row = all_windows[w];

        // Store metadata
        WindowMeta meta;
        meta.ticker = window_row.ticker;
        meta.start_row = window_row.window_start;
        meta.end_row = window_row.window_end;
        meta.target_start = window_row.target_start;
        meta.target_end = window_row.target_end;
        dataset.meta.push_back(meta);

        // Check if we've already loaded this ticker's CSV
        CsvFrame* frame_ptr = nullptr;
        auto cache_it = csv_cache.find(window_row.ticker);

        if (cache_it == csv_cache.end()) {
            // Need to load the CSV from the ORIGINAL input directory
            // Find the original input file path from the catalog/manifest

            // The input file should be: input_dir/{ticker}.csv
            // We need to reconstruct this path
            std::filesystem::path input_csv;

            // Search for matching CSV file in input directory
            // The catalog normalizes ticker names, so we need to find the original file
            bool found = false;
            for (const auto& entry : std::filesystem::directory_iterator(manifest_content.output_dir.parent_path() / ".." / "input")) {
                if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                    std::string filename = entry.path().stem().string();
                    // Normalize and compare
                    std::string normalized;
                    for (char c : filename) {
                        if (!std::isspace(static_cast<unsigned char>(c))) {
                            normalized += std::toupper(static_cast<unsigned char>(c));
                        }
                    }
                    if (normalized == window_row.ticker) {
                        input_csv = entry.path();
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                // Fallback: try direct path construction
                // Assume input_dir is stored or can be inferred
                // Since manifest doesn't store input_dir, we need to look for the file
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Cannot find original input CSV for ticker: " + window_row.ticker)
                };
            }

            // Load the CSV
            CsvFrame frame;
            if (!CsvIO::read_time_series_csv(input_csv, &frame, error_msg)) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Failed to read CSV for ticker " +
                                 window_row.ticker + ": " + error_msg)
                };
            }

            // Cache it
            csv_cache[window_row.ticker] = std::move(frame);
            frame_ptr = &csv_cache[window_row.ticker];
        } else {
            frame_ptr = &cache_it->second;
        }

        const CsvFrame& frame = *frame_ptr;
        
        // Validate we have enough rows
        if (frame.features.empty() || frame.features[0].size() < static_cast<std::size_t>(window_row.window_end + 1)) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Not enough rows in CSV for window [" + 
                             std::to_string(window_row.window_start) + ", " + 
                             std::to_string(window_row.window_end) + "]")
            };
        }
        
        // Fill X tensor: extract window_start to window_end
        for (std::int64_t s = 0; s < static_cast<std::int64_t>(seq_len); ++s) {
            std::int64_t row_idx = window_row.window_start + s;
            
            for (std::size_t f = 0; f < n_features; ++f) {
                // Find the feature column index in the CSV
                const std::string& feature_name = manifest_content.feature_columns[f];
                auto it = std::find(frame.feature_names.begin(), 
                                   frame.feature_names.end(), 
                                   feature_name);
                
                if (it == frame.feature_names.end()) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Feature column not found in CSV: " + feature_name)
                    };
                }
                
                std::size_t csv_col_idx = std::distance(frame.feature_names.begin(), it);
                double value = frame.features[csv_col_idx][row_idx];
                
                dataset.X.at(w, s, f) = static_cast<float>(value);
            }
        }
        
        // Fill Y tensor if we have targets
        if (has_target && window_row.target_start.has_value()) {
            const std::string& target_col = *manifest_content.target_column;
            
            auto it = std::find(frame.feature_names.begin(), 
                               frame.feature_names.end(), 
                               target_col);
            
            if (it == frame.feature_names.end()) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Target column not found in CSV: " + target_col)
                };
            }
            
            std::size_t target_col_idx = std::distance(frame.feature_names.begin(), it);
            
            for (std::int64_t h = 0; h < static_cast<std::int64_t>(manifest_content.future_horizon); ++h) {
                std::int64_t target_row = *window_row.target_start + h;
                
                if (target_row >= static_cast<std::int64_t>(frame.features[target_col_idx].size())) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Target row out of bounds: " + std::to_string(target_row))
                    };
                }
                
                double value = frame.features[target_col_idx][target_row];
                dataset.Y.at(w, h, 0) = static_cast<float>(value);
            }
        }
    }
    
    return Result<Dataset>{
        std::move(dataset),
        Status::OK()
    };
}

Result<Dataset> get_dataset(const std::filesystem::path& manifest_path) {
    // Load manifest from file
    auto manifest_result = Manifest::read_from_file(manifest_path);
    
    if (!manifest_result) {
        return Result<Dataset>{
            std::nullopt,
            manifest_result.status
        };
    }
    
    // Delegate to the in-memory version
    return get_dataset(manifest_result.value->content());
}

} // namespace rivulet