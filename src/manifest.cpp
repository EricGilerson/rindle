//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/manifest.cpp

Overview:
    Implements the JSON serialization and deserialization routines for dataset
    manifest metadata, including scaler parameter encoding.

Key functionality:
    - Populate `ManifestContent` from build inputs and stamp it with metadata.
    - Persist manifest.json with pretty formatting and a build timestamp.
    - Reload manifests from disk, rebuilding ticker lookup tables and reporting
      detailed errors when parsing fails.
==============================================================================*/

#include "internal/manifest.hpp"
#include "rindle/scaler.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace rivulet {

  using json = nlohmann::json;

  Manifest::Manifest(const DatasetConfig &config, const Catalog &catalog) {
    populate(config, catalog);
  }

  void Manifest::populate(const DatasetConfig &config, const Catalog &catalog) {
    content_.seq_length = config.seq_length;
    content_.future_horizon = config.future_horizon;
    content_.feature_columns = config.feature_columns;
    content_.target_column = config.target_column;
    content_.time_mode = config.time_mode;
    content_.row_major = config.row_major;
    content_.scaler_kind = config.scaler_kind;
    content_.total_tickers = catalog.work_items().size();
    content_.total_windows = catalog.total_windows_created();
    content_.total_input_rows = catalog.total_rows_processed();
    content_.ticker_stats = catalog.all_stats();
    content_.input_dir = config.input_dir;
    content_.output_dir = config.output_dir;
    content_.build_ticker_index();

  }

  bool Manifest::write_to_file(const std::filesystem::path &path, std::string &error_msg) const {
    try {
      // Generate current timestamp
      auto now = std::chrono::system_clock::now();
      auto now_time_t = std::chrono::system_clock::to_time_t(now);
      std::ostringstream oss;
      oss << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%d %H:%M:%S UTC");
      const_cast<Manifest*>(this)->content_.build_timestamp = oss.str();

      // Open file for writing
      std::ofstream file(path);
      if (!file.is_open()) {
        error_msg = "Failed to open file for writing: " + path.string();
        return false;
      }

      // Write JSON with pretty formatting
      file << to_json();

      if (!file.good()) {
        error_msg = "Error occurred while writing to file: " + path.string();
        return false;
      }

      return true;
    } catch (const std::exception &e) {
      error_msg = std::string("Exception during write: ") + e.what();
      return false;
    }
  }

  Result<Manifest> Manifest::read_from_file(const std::filesystem::path &path) {
    try {
      std::ifstream file(path);
      if (!file.is_open()) {
        return {std::nullopt, Status::Error("Failed to open file for reading: " + path.string())};
      }

      // Read entire file into string
      std::string json_str((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

      if (json_str.empty()) {
        return {std::nullopt, Status::Error("File is empty: " + path.string())};
      }

      std::string error_msg;
      auto result = from_json(json_str, error_msg);

      if (!result.has_value()) {
        return {std::nullopt, Status::Error(error_msg)};
      }

      return {result.value(), Status::OK()};
    } catch (const std::exception &e) {
      return {std::nullopt, Status::Error(std::string("Exception during read: ") + e.what())};
    }
  }

  std::string Manifest::to_json() const {
    json j;

    // Basic metadata
    j["version"] = content_.version;

    // Dataset configuration
    j["seq_length"] = content_.seq_length;
    j["future_horizon"] = content_.future_horizon;
    j["feature_columns"] = content_.feature_columns;

    if (content_.target_column.has_value()) {
      j["target_column"] = content_.target_column.value();
    } else {
      j["target_column"] = nullptr;
    }

    // Serialize TimeMode enum
    j["time_mode"] = (content_.time_mode == TimeMode::UTC_NS) ? "UTC_NS" : "ORDINAL";
    j["row_major"] = content_.row_major;
    j["scaler"] = scaler_kind_to_string(content_.scaler_kind);

    // Statistics
    j["total_tickers"] = content_.total_tickers;
    j["total_windows"] = content_.total_windows;
    j["total_input_rows"] = content_.total_input_rows;

    // Per-ticker stats
    json ticker_stats_array = json::array();
    for (const auto& stats : content_.ticker_stats) {
      json stats_obj;
      stats_obj["ticker"] = stats.ticker;
      stats_obj["input_rows"] = stats.input_rows;
      stats_obj["processed_rows"] = stats.processed_rows;
      stats_obj["windows_created"] = stats.windows_created;
      stats_obj["was_sorted"] = stats.was_sorted;
      stats_obj["scaler_kind"] = scaler_kind_to_string(stats.scaler_kind);

      json feature_scalers = json::array();
      for (const auto& feature_params : stats.feature_scalers) {
        json feature_obj;
        feature_obj["feature"] = feature_params.feature;
        feature_obj["params"] = scaler_params_to_json(feature_params.params);
        feature_scalers.push_back(std::move(feature_obj));
      }
      stats_obj["feature_scalers"] = feature_scalers;
      ticker_stats_array.push_back(stats_obj);
    }
    j["ticker_stats"] = ticker_stats_array;
    j["input_dir"] = content_.input_dir.string();
    j["output_dir"] = content_.output_dir.string();

    // Build metadata
    j["build_timestamp"] = content_.build_timestamp;

    return j.dump(4); // Pretty print with 4-space indent
  }

  std::optional<Manifest> Manifest::from_json(const std::string &json_str, std::string &error_msg) {
    try {
      json j = json::parse(json_str);

      Manifest manifest;

      // Basic metadata
      manifest.content_.version = j.value("version", 1);

      // Dataset configuration
      manifest.content_.seq_length = j.at("seq_length").get<std::size_t>();
      manifest.content_.future_horizon = j.at("future_horizon").get<std::size_t>();
      manifest.content_.feature_columns = j.at("feature_columns").get<std::vector<std::string>>();

      if (j.contains("target_column") && !j["target_column"].is_null()) {
        manifest.content_.target_column = j["target_column"].get<std::string>();
      } else {
        manifest.content_.target_column = std::nullopt;
      }

      // Deserialize TimeMode enum
      std::string time_mode_str = j.at("time_mode").get<std::string>();
      manifest.content_.time_mode = (time_mode_str == "UTC_NS") ? TimeMode::UTC_NS : TimeMode::ORDINAL;

      manifest.content_.row_major = j.at("row_major").get<bool>();
      if (j.contains("scaler")) {
        auto scaler_opt = scaler_kind_from_string(j.at("scaler").get<std::string>());
        manifest.content_.scaler_kind = scaler_opt.value_or(ScalerKind::None);
      } else {
        manifest.content_.scaler_kind = ScalerKind::None;
      }

      // Statistics
      manifest.content_.total_tickers = j.at("total_tickers").get<std::size_t>();
      manifest.content_.total_windows = j.at("total_windows").get<std::size_t>();
      manifest.content_.total_input_rows = j.at("total_input_rows").get<std::size_t>();
      manifest.content_.input_dir = j.at("input_dir").get<std::string>();
      manifest.content_.output_dir = j.at("output_dir").get<std::string>();
      // Per-ticker stats
      manifest.content_.ticker_stats.clear();
      if (j.contains("ticker_stats") && j["ticker_stats"].is_array()) {
        for (const auto& stats_json : j["ticker_stats"]) {
          TickerStats stats;
          stats.ticker = stats_json.at("ticker").get<std::string>();
          stats.input_rows = stats_json.at("input_rows").get<std::size_t>();
          if (stats_json.contains("processed_rows")) {
            stats.processed_rows = stats_json.at("processed_rows").get<std::size_t>();
          } else {
            stats.processed_rows = stats.input_rows;
          }
          stats.windows_created = stats_json.at("windows_created").get<std::size_t>();
          stats.was_sorted = stats_json.at("was_sorted").get<bool>();
          if (stats_json.contains("scaler_kind")) {
            auto scaler_opt = scaler_kind_from_string(stats_json.at("scaler_kind").get<std::string>());
            stats.scaler_kind = scaler_opt.value_or(manifest.content_.scaler_kind);
          } else {
            stats.scaler_kind = manifest.content_.scaler_kind;
          }
          if (stats_json.contains("feature_scalers") && stats_json["feature_scalers"].is_array()) {
            for (const auto& feature_json : stats_json["feature_scalers"]) {
              FeatureScalerParams params;
              params.feature = feature_json.at("feature").get<std::string>();
              params.params = scaler_params_from_json(feature_json.at("params"));
              stats.feature_scalers.push_back(std::move(params));
            }
          }
          manifest.content_.ticker_stats.push_back(stats);
        }
      }

      manifest.content_.build_ticker_index();

      // Build metadata
      manifest.content_.build_timestamp = j.value("build_timestamp", "");

      return manifest;

    } catch (const json::parse_error &e) {
      error_msg = std::string("JSON parse error: ") + e.what();
      return std::nullopt;
    } catch (const json::out_of_range &e) {
      error_msg = std::string("Missing required JSON field: ") + e.what();
      return std::nullopt;
    } catch (const json::type_error &e) {
      error_msg = std::string("JSON type error: ") + e.what();
      return std::nullopt;
    } catch (const std::exception &e) {
      error_msg = std::string("Unexpected error: ") + e.what();
      return std::nullopt;
    }
  }

} // namespace rivulet