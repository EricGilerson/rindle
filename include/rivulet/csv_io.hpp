//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/csv_io.hpp

  Purpose:
    File I/O boundary for v1. Reads per-ticker CSVs into Table objects and writes
    flattened window matrices (X), target matrices (y), and index files. Also
    appends to combined outputs.

  Responsibilities:
    - Parse input CSVs with either a Date column (UTC) or no Date (ordinal mode).
    - Write per-ticker files: X.csv, y.csv (optional), index.csv.
    - Write combined files under raw/combined with the same schemas.
    - Create directories as needed and report I/O errors clearly.

  Notes:
    - Implementation can be swapped to Parquet/Arrow later with the same interface.
    - Index files always record window_start, window_end, and target_end (when y exists).
==============================================================================*/

#ifndef RIVULET_CSV_IO_HPP
#define RIVULET_CSV_IO_HPP

#pragma once
#include "types.hpp"
#include <filesystem>
#include <string>
#include <vector>

#include "window_manifest.hpp"

namespace rivulet {

    class CsvIO {
    public:

        // Write per-ticker outputs
        static bool write_features(
            const std::filesystem::path& path,
            const std::vector<std::vector<double>>& X,
            const std::vector<std::string>& feature_names,
            std::string& error_msg,
            bool append
        );

        static bool write_targets(
            const std::filesystem::path& path,
            const std::vector<std::vector<double>>& y,
            const std::string& target_name,
            std::string& error_msg,
            bool append
        );

        static bool write_index(
            const std::filesystem::path& path,
            const std::vector<WindowRow>& indices,
            bool has_targets,
            std::string& error_msg,
            bool append
        );


        // Utility
        static bool ensure_directory_exists(const std::filesystem::path& dir);

    private:
        static std::optional<Timestamp> parse_timestamp(const std::string& str);
    };

} // namespace rivulet

#endif //RIVULET_CSV_IO_HPP