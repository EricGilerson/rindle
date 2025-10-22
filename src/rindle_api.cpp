#include "rindle.hpp"

// Internal headers (users cannot access these)
#include "internal/driver.hpp"
#include "internal/catalog.hpp"
#include "internal/manifest.hpp"
#include "internal/csv_io.hpp"
#include "internal/window_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

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
    bool row_major,
    ScalerKind scaler_kind
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
    config.scaler_kind = scaler_kind;

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
    //    - Write window manifest parquet
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

    if (manifest_content.input_dir.empty() ||
        !std::filesystem::exists(manifest_content.input_dir) ||
        !std::filesystem::is_directory(manifest_content.input_dir)) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Input directory does not exist: " +
                         manifest_content.input_dir.string())
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
    std::unordered_map<std::string, std::vector<ScalerParams>> ticker_scaler_params;
    std::string scaler_error;

    auto fetch_scalers_for_ticker = [&](const std::string& ticker)
        -> const std::vector<ScalerParams>* {
        auto existing = ticker_scaler_params.find(ticker);
        if (existing != ticker_scaler_params.end()) {
            return &existing->second;
        }

        const TickerStats* stats = manifest_content.find_stats(ticker);
        if (!stats) {
            scaler_error = "Scaler parameters missing for ticker: " + ticker;
            return nullptr;
        }

        std::unordered_map<std::string, const ScalerParams*> feature_lookup;
        feature_lookup.reserve(stats->feature_scalers.size());
        for (const auto& feature_params : stats->feature_scalers) {
            feature_lookup.emplace(feature_params.feature, &feature_params.params);
        }

        std::vector<ScalerParams> ordered_params;
        ordered_params.reserve(n_features);
        for (const auto& feature_name : manifest_content.feature_columns) {
            auto feature_it = feature_lookup.find(feature_name);
            if (feature_it == feature_lookup.end()) {
                scaler_error = "Scaler parameters missing for feature '" + feature_name +
                               "' in ticker " + ticker;
                return nullptr;
            }
            ordered_params.push_back(*feature_it->second);
        }

        auto inserted = ticker_scaler_params.emplace(ticker, std::move(ordered_params));
        return &inserted.first->second;
    };

    // Build lookup map from normalized ticker -> input CSV path
    auto normalize_ticker = [](const std::filesystem::path& path) {
        std::string filename = path.stem().string();
        std::string ticker;
        ticker.reserve(filename.size());
        for (char c : filename) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                ticker += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return ticker;
    };

    std::unordered_map<std::string, std::filesystem::path> ticker_to_input_path;
    for (const auto& entry : std::filesystem::directory_iterator(manifest_content.input_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
            continue;
        }

        std::string ticker = normalize_ticker(entry.path());
        if (!ticker.empty() && !ticker_to_input_path.count(ticker)) {
            ticker_to_input_path.emplace(std::move(ticker), entry.path());
        }
    }

    // Read window manifests for each ticker
    for (const auto& ticker_stats : manifest_content.ticker_stats) {
        const std::string& ticker = ticker_stats.ticker;

        // Path to this ticker's window manifest: output_dir/{ticker}_windows.parquet
        std::filesystem::path window_manifest_path =
            manifest_content.output_dir / (ticker + "_windows.parquet");

        if (!std::filesystem::exists(window_manifest_path)) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Window manifest not found for ticker: " + ticker +
                             " at " + window_manifest_path.string())
            };
        }

        std::vector<WindowRow> ticker_windows;
        if (!read_windows_manifest_parquet(window_manifest_path.string(), &ticker_windows, &error_msg)) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Failed to read window manifest: " +
                             window_manifest_path.string() +
                             (error_msg.empty() ? std::string() : (": " + error_msg)))
            };
        }

        for (auto& row : ticker_windows) {
            row.ticker = ticker;
            all_windows.push_back(row);
            total_windows++;
        }
    }

    // Allocate tensors
    if (total_windows > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Number of windows exceeds supported range")
        };
    }
    if (seq_len > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Sequence length exceeds supported range")
        };
    }
    if (n_features > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Feature count exceeds supported range")
        };
    }

    const auto total_windows_i64 = static_cast<std::int64_t>(total_windows);
    const auto seq_len_i64 = static_cast<std::int64_t>(seq_len);
    const auto n_features_i64 = static_cast<std::int64_t>(n_features);

    dataset.X.reshape(total_windows_i64, seq_len_i64, n_features_i64);

    std::int64_t future_horizon_i64 = 0;
    if (has_target) {
        if (manifest_content.future_horizon > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Future horizon exceeds supported range")
            };
        }
        future_horizon_i64 = static_cast<std::int64_t>(manifest_content.future_horizon);
        dataset.Y.reshape(total_windows_i64, future_horizon_i64, static_cast<std::int64_t>(1));
    }
    dataset.meta.reserve(total_windows);

    // Now fill tensors by reading actual CSV data from INPUT directory
    for (std::size_t w = 0; w < all_windows.size(); ++w) {
        const auto w_i64 = static_cast<std::int64_t>(w);
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
            auto path_it = ticker_to_input_path.find(window_row.ticker);
            if (path_it == ticker_to_input_path.end()) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Cannot find original input CSV for ticker: " + window_row.ticker)
                };
            }

            const std::filesystem::path& input_csv = path_it->second;

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
        if (window_row.window_end < 0) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Window end index is negative")
            };
        }
        if (window_row.window_end >= static_cast<std::int64_t>(std::numeric_limits<std::size_t>::max())) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Window end index exceeds supported range")
            };
        }
        if (frame.features.empty()) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("CSV frame is missing feature columns")
            };
        }

        const auto required_rows = static_cast<std::size_t>(window_row.window_end) + 1;
        if (frame.features[0].size() < required_rows) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error("Not enough rows in CSV for window [" +
                             std::to_string(window_row.window_start) + ", " +
                             std::to_string(window_row.window_end) + "]")
            };
        }

        const auto* scaler_params = fetch_scalers_for_ticker(window_row.ticker);
        if (!scaler_params) {
            return Result<Dataset>{
                std::nullopt,
                Status::Error(scaler_error)
            };
        }

        // Fill X tensor: extract window_start to window_end
        for (std::int64_t s = 0; s < seq_len_i64; ++s) {
            std::int64_t row_idx = window_row.window_start + s;

            if (row_idx < 0) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Window row index is negative")
                };
            }
            if (row_idx > static_cast<std::int64_t>(std::numeric_limits<std::size_t>::max())) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Window row index exceeds supported range")
                };
            }
            const auto row_idx_usize = static_cast<std::size_t>(row_idx);

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

                const auto csv_col_idx = static_cast<std::size_t>(std::distance(frame.feature_names.begin(), it));
                if (row_idx_usize >= frame.features[csv_col_idx].size()) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Row index out of bounds for feature column: " + feature_name)
                    };
                }
                double value = frame.features[csv_col_idx][row_idx_usize];
                double scaled_value = apply_scaler_value(value, (*scaler_params)[f]);
                dataset.X.at(w_i64, s, static_cast<std::int64_t>(f)) = static_cast<float>(scaled_value);
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
            
            const auto target_col_idx = static_cast<std::size_t>(std::distance(frame.feature_names.begin(), it));

            const std::int64_t target_start = *window_row.target_start;
            if (target_start < 0) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Target start index is negative")
                };
            }
            if (target_start > static_cast<std::int64_t>(std::numeric_limits<std::size_t>::max())) {
                return Result<Dataset>{
                    std::nullopt,
                    Status::Error("Target start index exceeds supported range")
                };
            }

            for (std::int64_t h = 0; h < future_horizon_i64; ++h) {
                std::int64_t target_row = target_start + h;

                if (target_row < 0) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Target row is negative")
                    };
                }
                if (target_row > static_cast<std::int64_t>(std::numeric_limits<std::size_t>::max())) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Target row exceeds supported range")
                    };
                }
                const auto target_row_idx = static_cast<std::size_t>(target_row);

                if (target_row_idx >= frame.features[target_col_idx].size()) {
                    return Result<Dataset>{
                        std::nullopt,
                        Status::Error("Target row out of bounds: " + std::to_string(target_row))
                    };
                }

                double value = frame.features[target_col_idx][target_row_idx];
                dataset.Y.at(w_i64, h, 0) = static_cast<float>(value);
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

Result<FittedScaler> get_feature_scaler(
    const ManifestContent& manifest_content,
    const std::string& ticker,
    const std::string& feature
) {
    const TickerStats* stats = manifest_content.find_stats(ticker);
    if (!stats) {
        return Result<FittedScaler>{
            std::nullopt,
            Status::Error("Ticker not found in manifest: " + ticker)
        };
    }

    auto feature_it = std::find_if(
        stats->feature_scalers.begin(),
        stats->feature_scalers.end(),
        [&](const FeatureScalerParams& params) {
            return params.feature == feature;
        }
    );

    if (feature_it == stats->feature_scalers.end()) {
        return Result<FittedScaler>{
            std::nullopt,
            Status::Error(
                "Scaler parameters not found for feature '" + feature +
                "' in ticker " + ticker
            )
        };
    }

    return Result<FittedScaler>{
        FittedScaler(feature_it->params),
        Status::OK()
    };
}

Result<FittedScaler> get_feature_scaler(
    const std::filesystem::path& manifest_path,
    const std::string& ticker,
    const std::string& feature
) {
    auto manifest_result = Manifest::read_from_file(manifest_path);
    if (!manifest_result) {
        return Result<FittedScaler>{
            std::nullopt,
            manifest_result.status
        };
    }

    const Manifest& manifest_obj = manifest_result.value.value();
    return get_feature_scaler(manifest_obj.content(), ticker, feature);
}

} // namespace rivulet
