//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/clean.hpp

  Purpose:
    The minimal cleaning pass for v1 focusing on leakage-free raw datasets.
    Converts selected columns to numeric, ensures strict ordering, and drops rows
    that cannot participate in windows.

  Responsibilities:
    - Validate required columns exist (features and optional target).
    - Convert to numeric; drop rows with missing required values.
    - Sort by time when Date exists; otherwise preserve input row order (ordinal).

  Notes:
    - No scaling, clipping, or imputation in v1 to keep leakage rules obvious.
    - Any rows removed are counted and surfaced to the caller for logging.
==============================================================================*/

#ifndef RIVULET_CLEAN_HPP
#define RIVULET_CLEAN_HPP

#pragma once
#include "types.hpp"
#include "table.hpp"
#include <string>
#include <vector>

namespace rivulet {

    struct CleanResult {
        Table cleaned_table;
        std::size_t rows_dropped = 0;
        bool was_sorted = false;
        std::vector<std::string> warnings;
    };

    class Cleaner {
    public:
        explicit Cleaner(const DatasetConfig& config);

        // Main cleaning interface
        Result<CleanResult> clean(Table&& input_table, std::string& error_msg) const;

    private:
        const DatasetConfig& config_;

        // Validation
        bool validate_columns(const Table& table, std::string& error_msg) const;

        // Drop rows with NaN/Inf in required columns
        std::vector<std::size_t> find_invalid_rows(const Table& table) const;

        // Check if string represents a valid number
        static bool is_numeric(const std::string& str);

        // Check if value is valid (not NaN or Inf)
        static bool is_valid_value(double value);
    };

} // namespace rivulet

#endif //RIVULET_CLEAN_HPP