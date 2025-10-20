// window_manifest.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rivulet {

/**
 * One row in the windows manifest.
 * A row identifies a training example by ticker and time bounds,
 * and may optionally include a target timestamp and label.
 */
    struct WindowRow {
        std::string ticker;
        std::int64_t window_start;
        std::int64_t window_end;
        std::optional<std::int64_t> target_start;
        std::optional<std::int64_t> target_end;

    };



/**
 * Write a complete manifest to the parquet-backed binary format.
 * Returns true on success and fills error_msg on failure.
 */
bool write_windows_manifest_parquet(const std::string& path,
                                    const std::vector<WindowRow>& rows,
                                    std::string* error_msg);

/**
 * Append a single row to an existing parquet manifest.
 * Creates the file if it does not exist.
 */
bool append_windows_manifest_parquet(const std::string& path,
                                     const WindowRow& row,
                                     std::string* error_msg);

/**
 * Append multiple rows to an existing parquet manifest.
 * Creates the file if it does not exist.
 * More efficient than calling append_windows_manifest_parquet repeatedly.
 */
bool append_windows_manifest_parquet_batch(const std::string& path,
                                           const std::vector<WindowRow>& rows,
                                           std::string* error_msg);

/**
 * Read a manifest written by write_windows_manifest_parquet.
 * Populates rows with the parsed window definitions.
 */
bool read_windows_manifest_parquet(const std::string& path,
                                   std::vector<WindowRow>* rows,
                                   std::string* error_msg);


}  // namespace rivulet

