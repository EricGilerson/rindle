// window_manifest.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace rivulet {

/**
 * One row in the windows manifest.
 * A row identifies a training example by ticker and time bounds,
 * and may optionally include a target timestamp and label.
 */
struct WindowRow {
  std::string ticker;
  std::int64_t window_start_ns;
  std::int64_t window_end_ns;
  std::optional<std::int64_t> target_end_ns;  // empty when unlabeled
  std::optional<double> y;                    // empty when unlabeled
};

/**
 * Optional metadata to write alongside a manifest.
 * Useful for reproducibility and cache management.
 */
struct WindowsManifestMeta {
  std::string feature_spec_id;     // hash or name of the feature function
  std::string raw_data_version;    // identifier for the raw/base data
  std::int64_t created_at_ns = 0;  // UTC time of creation
  std::string generator_version;   // version of the window maker
};

/**
 * Write a complete manifest to CSV.
 * Columns: ticker,window_start_ns,window_end_ns,target_end_ns,y
 * Empty optional values are written as empty cells.
 * Returns true on success and fills error_msg on failure.
 */
bool write_windows_manifest_csv(const std::string& path,
                                const std::vector<WindowRow>& rows,
                                std::string* error_msg);

/**
 * Append a single row to an existing CSV manifest.
 * Creates the file with header if it does not exist.
 */
bool append_windows_manifest_csv(const std::string& path,
                                 const WindowRow& row,
                                 std::string* error_msg);

/**
 * Append multiple rows to an existing CSV manifest.
 * Creates the file with header if it does not exist.
 * More efficient than calling append_windows_manifest_csv repeatedly.
 */
bool append_windows_manifest_csv_batch(const std::string& path,
                                       const std::vector<WindowRow>& rows,
                                       std::string* error_msg);

/**
 * Write sidecar metadata as JSON next to a CSV manifest.
 * If manifest is /dir/manifest.csv, metadata is /dir/manifest.meta.json.
 */
bool write_windows_manifest_metadata_json(const std::string& manifest_csv_path,
                                          const WindowsManifestMeta& meta,
                                          std::string* error_msg);


}  // namespace rivulet