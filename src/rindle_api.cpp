#include "rindle.hpp"

#include "internal/catalog.hpp"
#include "internal/csv_io.hpp"
#include "internal/driver.hpp"
#include "internal/manifest.hpp"
#include "internal/thread_pool.hpp"
#include "internal/window_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rivulet {

// 1. create_config - Build DatasetConfig with validation

Result<DatasetConfig>
create_config(const std::filesystem::path &input_dir,
              const std::filesystem::path &output_dir,
              const std::vector<std::string> &feature_columns,
              std::size_t seq_length, std::size_t future_horizon,
              const std::optional<std::string> &target_column,
              TimeMode time_mode, bool row_major, ScalerKind scaler_kind) {
  // Validate inputs
  if (!std::filesystem::exists(input_dir)) {
    return Result<DatasetConfig>{
        std::nullopt,
        Status::Error("Input directory does not exist: " + input_dir.string())};
  }

  if (!std::filesystem::is_directory(input_dir)) {
    return Result<DatasetConfig>{
        std::nullopt,
        Status::Error("Input path is not a directory: " + input_dir.string())};
  }

  if (feature_columns.empty()) {
    return Result<DatasetConfig>{
        std::nullopt, Status::Error("Feature columns list cannot be empty")};
  }

  if (seq_length == 0) {
    return Result<DatasetConfig>{
        std::nullopt, Status::Error("Sequence length must be greater than 0")};
  }

  if (future_horizon == 0) {
    return Result<DatasetConfig>{
        std::nullopt, Status::Error("Future horizon must be greater than 0")};
  }

  // Create output directory if it doesn't exist
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    return Result<DatasetConfig>{
        std::nullopt,
        Status::Error("Failed to create output directory: " + ec.message())};
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

  return Result<DatasetConfig>{config, Status::OK()};
}

// 2. build_dataset - Execute full pipeline

Result<ManifestContent> build_dataset(const DatasetConfig &config,
                                     unsigned int thread_count) {
  Driver driver(config, thread_count);

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
    return Result<ManifestContent>{std::nullopt, Status::Error(result.message)};
  }

  const ManifestContent &manifest_content = driver.get_manifest().content();

  return Result<ManifestContent>{manifest_content, Status::OK()};
}

// 3. get_dataset - Load tensors from built dataset (parallelized)

Result<Dataset> get_dataset(const ManifestContent &manifest_content,
                            double percentage,
                            unsigned int thread_count) {
  if (percentage <= 0.0 || percentage > 1.0) {
    return Result<Dataset>{
        std::nullopt,
        Status::Error(
            "Percentage must be between 0.0 (exclusive) and 1.0 (inclusive)")};
  }

  if (manifest_content.total_windows == 0) {
    return Result<Dataset>{std::nullopt,
                           Status::Error("Manifest reports zero windows")};
  }

  if (!std::filesystem::exists(manifest_content.output_dir)) {
    return Result<Dataset>{std::nullopt,
                           Status::Error("Output directory does not exist: " +
                                         manifest_content.output_dir.string())};
  }

  if (manifest_content.input_dir.empty() ||
      !std::filesystem::exists(manifest_content.input_dir) ||
      !std::filesystem::is_directory(manifest_content.input_dir)) {
    return Result<Dataset>{std::nullopt,
                           Status::Error("Input directory does not exist: " +
                                         manifest_content.input_dir.string())};
  }

  const std::size_t n_features = manifest_content.feature_columns.size();
  const std::size_t seq_len = manifest_content.seq_length;
  const bool has_target = manifest_content.target_column.has_value();

  // Build normalized ticker -> input CSV path map (sequential, fast)
  auto normalize_ticker = [](const std::filesystem::path &path) {
    std::string filename = path.stem().string();
    std::string ticker;
    ticker.reserve(filename.size());
    for (char c : filename) {
      if (!std::isspace(static_cast<unsigned char>(c))) {
        ticker +=
            static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
    }
    return ticker;
  };

  std::unordered_map<std::string, std::filesystem::path> ticker_to_input_path;
  for (const auto &entry :
       std::filesystem::directory_iterator(manifest_content.input_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
      continue;
    }
    std::string ticker = normalize_ticker(entry.path());
    if (!ticker.empty() && !ticker_to_input_path.count(ticker)) {
      ticker_to_input_path.emplace(std::move(ticker), entry.path());
    }
  }

  // ============================================================
  // Phase 1: Parallel manifest read + subsample
  // ============================================================
  struct TickerWindowResult {
    std::vector<WindowRow> windows;
    std::string error;
  };

  const std::size_t n_tickers = manifest_content.ticker_stats.size();
  std::vector<TickerWindowResult> ticker_results(n_tickers);

  {
    ThreadPool pool(thread_count);
    pool.parallel_for(n_tickers, [&](std::size_t t) {
      const auto &ts = manifest_content.ticker_stats[t];
      const std::string &ticker = ts.ticker;
      auto &result = ticker_results[t];

      std::filesystem::path window_manifest_path =
          manifest_content.output_dir / (ticker + "_windows.parquet");

      if (!std::filesystem::exists(window_manifest_path)) {
        result.error = "Window manifest not found for ticker: " + ticker +
                       " at " + window_manifest_path.string();
        return;
      }

      std::vector<WindowRow> ticker_windows;
      std::string err;
      if (!read_windows_manifest_parquet(window_manifest_path.string(),
                                         &ticker_windows, &err)) {
        result.error = "Failed to read window manifest: " +
                       window_manifest_path.string() +
                       (err.empty() ? std::string() : (": " + err));
        return;
      }

      const std::size_t total_ticker_windows = ticker_windows.size();
      const std::size_t keep_count = static_cast<std::size_t>(
          std::ceil(static_cast<double>(total_ticker_windows) * percentage));

      std::size_t actual_keep = std::min(keep_count, total_ticker_windows);
      if (actual_keep == 0 && total_ticker_windows > 0 && percentage > 0) {
        actual_keep = 1;
      }

      std::vector<std::size_t> indices(total_ticker_windows);
      std::iota(indices.begin(), indices.end(), 0);

      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(indices.begin(), indices.end(), g);

      result.windows.reserve(actual_keep);
      for (std::size_t i = 0; i < actual_keep; ++i) {
        auto &row = ticker_windows[indices[i]];
        row.ticker = ticker;
        result.windows.push_back(std::move(row));
      }
    });
  }

  // Merge results and check for errors
  std::vector<WindowRow> all_windows;
  for (auto &tr : ticker_results) {
    if (!tr.error.empty()) {
      return Result<Dataset>{std::nullopt, Status::Error(tr.error)};
    }
    all_windows.insert(all_windows.end(),
                       std::make_move_iterator(tr.windows.begin()),
                       std::make_move_iterator(tr.windows.end()));
  }
  ticker_results.clear();

  const std::size_t total_windows = all_windows.size();
  if (total_windows == 0) {
    return Result<Dataset>{std::nullopt,
                           Status::Error("No windows after subsampling")};
  }

  // ============================================================
  // Phase 2: Parallel CSV pre-loading
  // ============================================================
  std::unordered_set<std::string> needed_tickers;
  for (const auto &w : all_windows) needed_tickers.insert(w.ticker);

  std::vector<std::string> ticker_list(needed_tickers.begin(),
                                       needed_tickers.end());
  needed_tickers.clear();

  // Pre-allocate cache entries so map won't rehash during parallel writes
  std::unordered_map<std::string, CsvFrame> csv_cache;
  csv_cache.reserve(ticker_list.size());
  for (const auto &t : ticker_list) csv_cache[t] = CsvFrame{};

  std::vector<std::string> load_errors(ticker_list.size());

  {
    ThreadPool pool(thread_count);
    pool.parallel_for(ticker_list.size(), [&](std::size_t i) {
      const auto &ticker = ticker_list[i];
      auto path_it = ticker_to_input_path.find(ticker);
      if (path_it == ticker_to_input_path.end()) {
        load_errors[i] = "Cannot find input CSV for ticker: " + ticker;
        return;
      }
      std::string err;
      if (!CsvIO::read_time_series_csv(path_it->second, &csv_cache[ticker],
                                        err)) {
        load_errors[i] = "Failed to read CSV for ticker " + ticker + ": " + err;
      }
    });
  }

  for (const auto &err : load_errors) {
    if (!err.empty()) {
      return Result<Dataset>{std::nullopt, Status::Error(err)};
    }
  }

  // Build feature_name -> column_index maps per ticker (eliminates O(n) std::find)
  std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>>
      ticker_feature_idx;
  for (const auto &[ticker, frame] : csv_cache) {
    auto &idx_map = ticker_feature_idx[ticker];
    idx_map.reserve(frame.feature_names.size());
    for (std::size_t c = 0; c < frame.feature_names.size(); ++c) {
      idx_map[frame.feature_names[c]] = c;
    }
  }

  // Pre-compute ordered scaler params per ticker
  std::unordered_map<std::string, std::vector<ScalerParams>>
      ticker_scaler_params;
  for (const auto &ticker : ticker_list) {
    const TickerStats *stats = manifest_content.find_stats(ticker);
    if (!stats) {
      return Result<Dataset>{
          std::nullopt,
          Status::Error("Scaler parameters missing for ticker: " + ticker)};
    }

    std::unordered_map<std::string, const ScalerParams *> feature_lookup;
    feature_lookup.reserve(stats->feature_scalers.size());
    for (const auto &fp : stats->feature_scalers) {
      feature_lookup.emplace(fp.feature, &fp.params);
    }

    std::vector<ScalerParams> ordered_params;
    ordered_params.reserve(n_features);
    for (const auto &feature_name : manifest_content.feature_columns) {
      auto it = feature_lookup.find(feature_name);
      if (it == feature_lookup.end()) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Scaler parameters missing for feature '" +
                          feature_name + "' in ticker " + ticker)};
      }
      ordered_params.push_back(*it->second);
    }
    ticker_scaler_params[ticker] = std::move(ordered_params);
  }

  // Pre-validate: check all frames have required features and enough rows
  for (const auto &w : all_windows) {
    const auto &frame = csv_cache.at(w.ticker);
    const auto &idx_map = ticker_feature_idx.at(w.ticker);

    for (const auto &fname : manifest_content.feature_columns) {
      if (idx_map.find(fname) == idx_map.end()) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Feature column not found in CSV: " + fname)};
      }
    }

    if (has_target && manifest_content.target_column.has_value()) {
      if (idx_map.find(*manifest_content.target_column) == idx_map.end()) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Target column not found in CSV: " +
                          *manifest_content.target_column)};
      }
    }

    if (w.window_end < 0) {
      return Result<Dataset>{std::nullopt,
                             Status::Error("Window end index is negative")};
    }
    const auto required_rows = static_cast<std::size_t>(w.window_end) + 1;
    if (frame.features.empty() || frame.features[0].size() < required_rows) {
      return Result<Dataset>{
          std::nullopt,
          Status::Error("Not enough rows in CSV for window [" +
                        std::to_string(w.window_start) + ", " +
                        std::to_string(w.window_end) + "]")};
    }

    if (has_target && w.target_start.has_value()) {
      const auto target_end_row = static_cast<std::size_t>(
          *w.target_start +
          static_cast<std::int64_t>(manifest_content.future_horizon));
      const auto &target_col = *manifest_content.target_column;
      std::size_t tcol = idx_map.at(target_col);
      if (frame.features[tcol].size() < target_end_row) {
        return Result<Dataset>{
            std::nullopt,
            Status::Error("Target rows out of bounds for ticker " + w.ticker)};
      }
    }
  }

  // ============================================================
  // Phase 3: Allocate tensors + parallel fill
  // ============================================================
  if (total_windows >
      static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
    return Result<Dataset>{
        std::nullopt,
        Status::Error("Number of windows exceeds supported range")};
  }

  const auto total_windows_i64 = static_cast<std::int64_t>(total_windows);
  const auto seq_len_i64 = static_cast<std::int64_t>(seq_len);
  const auto n_features_i64 = static_cast<std::int64_t>(n_features);

  Dataset dataset;
  dataset.X.reshape(total_windows_i64, seq_len_i64, n_features_i64);

  std::int64_t future_horizon_i64 = 0;
  if (has_target) {
    future_horizon_i64 =
        static_cast<std::int64_t>(manifest_content.future_horizon);
    dataset.Y.reshape(total_windows_i64, future_horizon_i64,
                      static_cast<std::int64_t>(1));
  }
  dataset.meta.resize(total_windows);

  {
    ThreadPool pool(thread_count);
    pool.parallel_for(total_windows, [&](std::size_t w) {
      const auto w_i64 = static_cast<std::int64_t>(w);
      const auto &window_row = all_windows[w];
      const CsvFrame &frame = csv_cache.at(window_row.ticker);
      const auto &idx_map = ticker_feature_idx.at(window_row.ticker);
      const auto &scaler_params = ticker_scaler_params.at(window_row.ticker);

      // Fill metadata
      WindowMeta &meta = dataset.meta[w];
      meta.ticker = window_row.ticker;
      meta.start_row = window_row.window_start;
      meta.end_row = window_row.window_end;
      meta.target_start = window_row.target_start;
      meta.target_end = window_row.target_end;

      // Fill X tensor
      for (std::int64_t s = 0; s < seq_len_i64; ++s) {
        const auto row_idx =
            static_cast<std::size_t>(window_row.window_start + s);

        for (std::size_t f = 0; f < n_features; ++f) {
          const std::size_t csv_col =
              idx_map.at(manifest_content.feature_columns[f]);
          double value = frame.features[csv_col][row_idx];
          double scaled = apply_scaler_value(value, scaler_params[f]);
          dataset.X.at(w_i64, s, static_cast<std::int64_t>(f)) =
              static_cast<float>(scaled);
        }
      }

      // Fill Y tensor
      if (has_target && window_row.target_start.has_value()) {
        const std::size_t tcol =
            idx_map.at(*manifest_content.target_column);
        const std::int64_t tstart = *window_row.target_start;

        for (std::int64_t h = 0; h < future_horizon_i64; ++h) {
          const auto trow = static_cast<std::size_t>(tstart + h);
          double value = frame.features[tcol][trow];
          dataset.Y.at(w_i64, h, 0) = static_cast<float>(value);
        }
      }
    });
  }

  return Result<Dataset>{std::move(dataset), Status::OK()};
}

Result<Dataset> get_dataset(const std::filesystem::path &manifest_path,
                            double percentage,
                            unsigned int thread_count) {
  auto manifest_result = Manifest::read_from_file(manifest_path);

  if (!manifest_result) {
    return Result<Dataset>{std::nullopt, manifest_result.status};
  }

  return get_dataset(manifest_result.value->content(), percentage, thread_count);
}

Result<FittedScaler> get_feature_scaler(const ManifestContent &manifest_content,
                                        const std::string &ticker,
                                        const std::string &feature) {
  const TickerStats *stats = manifest_content.find_stats(ticker);
  if (!stats) {
    return Result<FittedScaler>{
        std::nullopt, Status::Error("Ticker not found in manifest: " + ticker)};
  }

  auto feature_it =
      std::find_if(stats->feature_scalers.begin(), stats->feature_scalers.end(),
                   [&](const FeatureScalerParams &params) {
                     return params.feature == feature;
                   });

  if (feature_it == stats->feature_scalers.end()) {
    return Result<FittedScaler>{
        std::nullopt,
        Status::Error("Scaler parameters not found for feature '" + feature +
                      "' in ticker " + ticker)};
  }

  return Result<FittedScaler>{FittedScaler(feature_it->params), Status::OK()};
}

Result<FittedScaler>
get_feature_scaler(const std::filesystem::path &manifest_path,
                   const std::string &ticker, const std::string &feature) {
  auto manifest_result = Manifest::read_from_file(manifest_path);
  if (!manifest_result) {
    return Result<FittedScaler>{std::nullopt, manifest_result.status};
  }

  const Manifest &manifest_obj = manifest_result.value.value();
  return get_feature_scaler(manifest_obj.content(), ticker, feature);
}

} // namespace rivulet
