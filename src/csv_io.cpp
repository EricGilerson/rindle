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
#include <ctime>
#include "rivulet/types.hpp"

namespace rivulet {
  Result<Table> CsvIO::read_csv(
      const std::filesystem::path& path,
      TimeMode mode,
      std::string& error_msg
  ) {
      Table table;
      if (!table.load_from_csv(path, mode, error_msg)) {
          return {std::nullopt, Status::Error(error_msg)};
      }
      return {table, Status::OK()};
  }

  bool CsvIO::write_features(
      const std::filesystem::path& path,
      const std::vector<std::vector<double>>& X,
      const std::vector<std::string>& feature_names,
      std::string& error_msg
  ) {
      // Ensure output directory exists
      std::filesystem::create_directories(path.parent_path());

      std::ofstream ofs(path);
      if (!ofs.is_open()) {
          error_msg = "Failed to open file for writing: " + path.string();
          return false;
      }

      // Write header
      for (size_t i = 0; i < feature_names.size(); ++i) {
          ofs << feature_names[i];
          if (i < feature_names.size() - 1) {
              ofs << ",";
          }
      }
      ofs << "\n";

      // Write data
      for (const auto& row : X) {
          for (size_t i = 0; i < row.size(); ++i) {
              ofs << row[i];
              if (i < row.size() - 1) {
                  ofs << ",";
              }
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
  bool CsvIO::write_targets(const std::filesystem::path &path, const std::vector<std::vector<double> > &y, const std::string &target_name, std::string &error_msg) {
        // Ensure output directory exists
        std::filesystem::create_directories(path.parent_path());

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            error_msg = "Failed to open file for writing: " + path.string();
            return false;
        }

        // Write header
        ofs << target_name << "\n";

        // Write data
        for (const auto& row : y) {
            for (size_t i = 0; i < row.size(); ++i) {
                ofs << row[i];
                if (i < row.size() - 1) {
                    ofs << ",";
                }
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
  bool CsvIO::write_index(std::filesystem::path const &path, std::vector<WindowIndex> const &indices, bool has_targets, std::string &error_msg) {
        // Ensure output directory exists
        std::filesystem::create_directories(path.parent_path());

        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            error_msg = "Failed to open file for writing: " + path.string();
            return false;
        }

        // Write header
        ofs << "ticker,window_start,window_end";
        if (has_targets) {
            ofs << ",target_end";
        }
        ofs << ",start_time,end_time\n";

        // Write data
        for (const auto& idx : indices) {
            ofs << idx.ticker << "," << idx.window_start << "," << idx.window_end;
            if (has_targets) {
                ofs << ",";
                if (idx.target_end.has_value()) {
                    ofs << idx.target_end.value();
                } else {
                    ofs << "";
                }
            }
            ofs << ",";
            if (idx.start_time.has_value()) {
                ofs << idx.start_time.value();
            } else {
                ofs << "";
            }
            ofs << ",";
            if (idx.end_time.has_value()) {
                ofs << idx.end_time.value();
            } else {
                ofs << "";
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
  bool CsvIO::ensure_directory_exists(const std::filesystem::path &dir) {
      return std::filesystem::is_directory(dir);
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