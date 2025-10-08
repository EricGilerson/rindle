//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/window_maker.hpp

  Purpose:
    Sliding-window constructor that produces leakage-free windows (X) and aligned
    future targets (y) from a clean, ordered Table.

  Responsibilities:
    - Given features[], seq_length L, and horizon H, emit:
        X: flattened (time-major) windows of shape [n_rows, L*F]
        y: target rows aligned to the window end, length H (optional)
        index: window_start, window_end, target_end for each row
    - Skip any window that contains NaN/Inf after cleaning.

  Notes:
    - Horizon is in rows, not minutes. When Date exists, timestamps are still
      preserved for audit via index output.
==============================================================================*/

#ifndef RIVULET_WINDOW_MAKER_HPP
#define RIVULET_WINDOW_MAKER_HPP

#pragma once
#include "types.hpp"
#include "table.hpp"
#include "csv_io.hpp"
#include <vector>
#include <string>

namespace rivulet {

    struct WindowOutput {
        std::vector<std::vector<double>> X;  // [n_windows, L*F] or [n_windows, F*L]
        std::vector<std::vector<double>> y;  // [n_windows, H] (optional)
        std::vector<WindowIndex> indices;
        std::size_t windows_created = 0;
        std::size_t windows_skipped = 0;
    };

    class WindowMaker {
    public:
        explicit WindowMaker(const DatasetConfig& config);

        // Create windows from a cleaned table
        Result<WindowOutput> make_windows(
            const Table& table,
            const std::string& ticker,
            std::string& error_msg
        ) const;

    private:
        const DatasetConfig& config_;

        // Flatten a single window (time-major or row-major)
        std::vector<double> flatten_window(
            const Table& table,
            const std::vector<std::string>& feature_cols,
            std::size_t start_row,
            std::size_t length
        ) const;

        // Extract target values
        std::vector<double> extract_targets(
            const Table& table,
            const std::string& target_col,
            std::size_t start_row,
            std::size_t length
        ) const;

        // Check if window contains any invalid values
        bool window_is_valid(
            const Table& table,
            const std::vector<std::string>& cols,
            std::size_t start,
            std::size_t length
        ) const;
    };

} // namespace rivulet

#endif //RIVULET_WINDOW_MAKER_HPP