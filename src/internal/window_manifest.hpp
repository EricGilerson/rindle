// window_manifest.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <optional>
#include <sstream>

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

        static std::string csv_header() {
            // Column order must match to_csv_row()
            return "ticker,window_start,window_end,target_start,target_end";
        }

        static std::string csv_escape(const std::string& s) {
            bool needs_quotes = false;
            for (char c : s) {
                if (c == '"' || c == ',' || c == '\n' || c == '\r') { needs_quotes = true; break; }
            }
            if (!needs_quotes) return s;
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');
            for (char c : s) {
                if (c == '"') out.push_back('"'); // escape by doubling
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

        std::string to_csv_row() const {
            std::ostringstream oss;
            oss << csv_escape(ticker) << ','
                << window_start << ','
                << window_end << ',';
            if (target_start.has_value()) oss << *target_start;
            oss << ',';
            if (target_end.has_value()) oss << *target_end;
            return oss.str();
        }
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


}  // namespace rivulet

