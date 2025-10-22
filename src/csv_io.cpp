// =======================
// File: src/csv_io.cpp
// =======================
/*==============================================================================
  File: src/csv_io.cpp

  Purpose:
    Artifact-first CSV utilities for Rivulet.
    - Provides a robust CSV reader for canonical time-series format: Date, f1..fn
      (no scaling or NA repair here; missing numeric cells are parsed as NaN).
    - Provides read/write helpers for scaler artifacts so the engine can load
      per-fold, per-ticker scaler parameters at window materialization time.

  Notes:
    - We do not write pre-scaled X/Y files. Transforms are applied at window time.
==============================================================================*/

#include "internal/csv_io.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <charconv>
#include <algorithm>
#include <limits>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace rivulet {

// --- small helpers ---
static inline void rtrim(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}
static inline void ltrim(std::string& s) {
    size_t i = 0; while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; s.erase(0, i);
}
static inline void trim(std::string& s) { rtrim(s); ltrim(s); }

static inline bool split_csv_line(const std::string& line, std::vector<std::string>& cells) {
    cells.clear();
    std::string cur;
    cur.reserve(line.size());
    bool in_quotes = false;
    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes; // simplistic quotes toggle (no escaped quotes)
            continue;
        }
        if (ch == ',' && !in_quotes) {
            trim(cur); cells.push_back(cur); cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    trim(cur); cells.push_back(cur);
    return true;
}

std::optional<std::int64_t> parse_date_to_ns(const std::string& cell) {
    // Fast path: integer epoch in s/ms/us/ns
    std::string s = cell; std::string t = s; trim(t);
    if (!t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || (t[0] == '-' && t.size() > 1))) {
        std::int64_t v = 0; // from_chars for speed and no locale
        auto* beg = t.data();
        auto* end = t.data() + t.size();
        auto res = std::from_chars(beg, end, v);
        if (res.ec == std::errc() && res.ptr == end) {
            // Heuristic: choose unit by magnitude
            // s ~ 1e9, ms ~ 1e12, us ~ 1e15, ns ~ 1e18
            std::int64_t abs_v = v < 0 ? -v : v;
            if (abs_v < 5'000'000'000LL) { // < ~2119-01 in seconds
                return v * 1'000'000'000LL;
            } else if (abs_v < 5'000'000'000'000LL) {
                return v * 1'000'000LL; // ms -> ns
            } else if (abs_v < 5'000'000'000'000'000LL) {
                return v * 1'000LL; // us -> ns
            } else {
                return v; // already ns
            }
        }
    }

    // Fallback: "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS"
    std::tm tm = {};
    std::istringstream ss(t);
    // try with time first
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        ss.clear(); ss.str(t);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        if (ss.fail()) return std::nullopt;
    }
#if defined(_WIN32)
    // Windows lacks timegm; approximate using _mkgmtime
    std::time_t time_c = _mkgmtime(&tm);
#else
    std::time_t time_c = timegm(&tm);
#endif
    if (time_c == -1) return std::nullopt;
    return static_cast<std::int64_t>(time_c) * 1'000'000'000LL;
}

bool CsvIO::read_time_series_csv(
    const std::filesystem::path& path,
    CsvFrame* out,
    std::string& error_msg
) {
    out->date_ns.clear();
    out->feature_names.clear();
    out->features.clear();

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        error_msg = "Failed to open CSV: " + path.string();
        return false;
    }

    std::string header_line;
    if (!std::getline(ifs, header_line)) {
        error_msg = "Empty CSV: " + path.string();
        return false;
    }

    std::vector<std::string> header_cells;
    split_csv_line(header_line, header_cells);
    if (header_cells.empty() || header_cells[0] != "Date") {
        error_msg = "CSV must have header with first column 'Date'";
        return false;
    }

    // Prepare feature columns
    out->feature_names.assign(header_cells.begin() + 1, header_cells.end());
    const std::size_t F = out->feature_names.size();
    out->features.assign(F, {});

    std::string line;
    std::vector<std::string> cells;
    std::size_t expected_cols = header_cells.size();

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        split_csv_line(line, cells);
        if (cells.size() != expected_cols) {
            error_msg = "Row has " + std::to_string(cells.size()) + " cells, expected " + std::to_string(expected_cols);
            return false;
        }
        auto ts_opt = parse_date_to_ns(cells[0]);
        if (!ts_opt) {
            error_msg = "Failed to parse Date cell: '" + cells[0] + "'";
            return false;
        }
        out->date_ns.push_back(*ts_opt);

        // parse numeric features; missing becomes NaN
        for (std::size_t j = 0; j < F; ++j) {
            const std::string& cell = cells[j + 1];
            double val;
            if (cell.empty()) {
                val = std::numeric_limits<double>::quiet_NaN();
            } else {
                // Use strtod for broad numeric forms (handles scientific notation);
                // avoid std::from_chars(double) for wider libc++ compatibility.
                errno = 0;
                char* endp = nullptr;
                const char* startp = cell.c_str();
                val = std::strtod(startp, &endp);
                if (endp == startp || *endp != '\0' || errno == ERANGE) {
                    val = std::numeric_limits<double>::quiet_NaN();
                }
            }
            out->features[j].push_back(val);
        }
    }

    return true;
}

static void write_joined(std::ostream& os, const std::vector<std::string>& v) {
    for (size_t i = 0; i < v.size(); ++i) { os << v[i]; if (i + 1 < v.size()) os << ';'; }
}
static void write_joined(std::ostream& os, const std::vector<double>& v) {
    for (size_t i = 0; i < v.size(); ++i) { os << v[i]; if (i + 1 < v.size()) os << ';'; }
}

bool CsvIO::write_scaler_artifacts_csv(
    const std::filesystem::path& path,
    const std::vector<ScalerArtifact>& artifacts,
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

    if (!append || !file_exists) {
        ofs << "fold_index,train_start_ns,train_end_ns,ticker,feature_set_version,schema_hash,scaler_type,feature_names,param_a,param_b";
    }

    for (const auto& a : artifacts) {
        ofs << a.key.fold.index << ','
            << a.key.fold.train_start_ns << ','
            << a.key.fold.train_end_ns << ','
            << a.key.ticker << ','
            << a.key.feature_set_version << ','
            << a.key.schema_hash << ','
            << a.scaler_type << ',';
        write_joined(ofs, a.feature_names); ofs << ',';
        write_joined(ofs, a.param_a); ofs << ',';
        write_joined(ofs, a.param_b); ofs << ',';
    }

    ofs.close();
    if (ofs.fail()) {
        error_msg = "Failed to write artifacts: " + path.string();
        return false;
    }
    return true;
}

bool CsvIO::read_scaler_artifacts_csv(
    const std::filesystem::path& path,
    std::vector<ScalerArtifact>* out,
    std::string& error_msg
) {
    out->clear();
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        error_msg = "Failed to open artifacts file: " + path.string();
        return false;
    }

    std::string line;
    // skip header
    if (!std::getline(ifs, line)) return true;

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string cell;
        ScalerArtifact a;

        if (!std::getline(ss, cell, ',')) break; a.key.fold.index = std::stoi(cell);
        if (!std::getline(ss, cell, ',')) break; a.key.fold.train_start_ns = std::stoll(cell);
        if (!std::getline(ss, cell, ',')) break; a.key.fold.train_end_ns   = std::stoll(cell);
        if (!std::getline(ss, cell, ',')) break; a.key.ticker = cell;
        if (!std::getline(ss, cell, ',')) break; a.key.feature_set_version = cell;
        if (!std::getline(ss, cell, ',')) break; a.key.schema_hash = cell;
        if (!std::getline(ss, cell, ',')) break; a.scaler_type = cell;

        if (!std::getline(ss, cell, ',')) break; {
            std::stringstream fs(cell); std::string tok; while (std::getline(fs, tok, ';')) a.feature_names.push_back(tok);
        }
        if (!std::getline(ss, cell, ',')) break; {
            std::stringstream fs(cell); std::string tok; while (std::getline(fs, tok, ';')) if (!tok.empty()) a.param_a.push_back(std::stod(tok));
        }
        if (!std::getline(ss, cell, ',')) break; {
            std::stringstream fs(cell); std::string tok; while (std::getline(fs, tok, ';')) if (!tok.empty()) a.param_b.push_back(std::stod(tok));
        }

        out->push_back(std::move(a));
    }

    return true;
}

} // namespace rivulet
