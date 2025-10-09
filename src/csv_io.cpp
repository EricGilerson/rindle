//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/csv_io.cpp

  Purpose:
    Implements CSV reading and writing for v1. Converts Date to UTC nanoseconds
    when present and writes per-ticker and combined outputs.

  Responsibilities:
    - Robustly parse input CSVs; report schema errors with context.
    - Write X.csv, y.csv, index.csv with consistent headers and ordering.
    - Create output directories and handle append semantics for combined outputs.

  Notes:
    - Swap internals to Parquet/Arrow later without changing the header API.
==============================================================================*/

#include "rivulet/csv_io.hpp"

#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include "rivulet/types.hpp"

namespace rivulet {

    bool CsvIO::write_features(
      const std::filesystem::path& path,
      const std::vector<std::vector<double>>& X,
      const std::vector<std::string>& feature_names,
      std::string& error_msg,
      bool append
  ) {
      std::filesystem::create_directories(path.parent_path());
      const bool file_exists = std::filesystem::exists(path);
      std::ofstream ofs(path, append ? std::ios::app : std::ios::out);
      if (!ofs.is_open()) {
          error_msg = "Failed to open file for writing: " + path.string();
          return false;
      }

      // header once
      if (!append || !file_exists) {
          for (size_t i = 0; i < feature_names.size(); ++i) {
              ofs << feature_names[i];
              if (i + 1 < feature_names.size()) ofs << ",";
          }
          ofs << "\n";
      }

      for (const auto& row : X) {
          for (size_t i = 0; i < row.size(); ++i) {
              ofs << row[i];
              if (i + 1 < row.size()) ofs << ",";
          }
          ofs << "\n";
      }

      ofs.close();
      if (ofs.fail()) {
          error_msg = "Failed to write data to file: " + path.string();
          return false;
      }
      return true;
  }

    bool CsvIO::write_targets(
      const std::filesystem::path& path,
      const std::vector<std::vector<double>>& y,
      const std::string& target_name,
      std::string& error_msg,
      bool append /* = false */
  ) {
      std::filesystem::create_directories(path.parent_path());
      const bool file_exists = std::filesystem::exists(path);
      std::ofstream ofs(path, append ? std::ios::app : std::ios::out);
      if (!ofs.is_open()) {
          error_msg = "Failed to open file for writing: " + path.string();
          return false;
      }

      if (!append || !file_exists) {
          ofs << target_name << "\n";
      }

      for (const auto& row : y) {
          for (size_t i = 0; i < row.size(); ++i) {
              ofs << row[i];
              if (i + 1 < row.size()) ofs << ",";
          }
          ofs << "\n";
      }

      ofs.close();
      if (ofs.fail()) {
          error_msg = "Failed to write data to file: " + path.string();
          return false;
      }
      return true;
  }


    std::optional<Timestamp> parse_timestamp(const std::string& date_str, std::string& error_msg) {
      // Expected format: "YYYY-MM-DD HH:MM:SS" (UTC)
      std::tm tm = {};
      std::istringstream ss(date_str);
      ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
      if (ss.fail()) {
          error_msg = "Failed to parse date: " + date_str;
          return std::nullopt;
      }

      // Convert broken-down UTC time to time_t (seconds since epoch)
      std::time_t time_c = timegm(&tm); // GNU extension; parses as UTC
      if (time_c == -1) {
          error_msg = "Invalid date value: " + date_str;
          return std::nullopt;
      }

      // Convert to system_clock time_point, then to Timestamp (nanoseconds)
      auto tp_seconds = std::chrono::system_clock::from_time_t(time_c);
      auto ns_since_epoch = std::chrono::duration_cast<Nanoseconds>(tp_seconds.time_since_epoch());
      return Timestamp{ns_since_epoch};
  }




}