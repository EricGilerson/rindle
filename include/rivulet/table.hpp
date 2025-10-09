//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: include/rivulet/table.hpp

  Purpose:
    Minimal, in-memory columnar table interface used by the dataset builder.
    Designed for loading CSV columns, reading/writing numeric data, and iterating
    rows in time or ordinal order.

  Responsibilities:
    - Expose column names, row count, and typed access to entire numeric columns.
    - Provide access to the Date column as UTC nanoseconds when present.
    - Enforce that column order can be arranged to match a manifest/spec.

  Notes:
    - Intentionally simple: this is not a general DataFrame.
    - Backed by std::vector storage with clear ownership.
==============================================================================*/

#ifndef RIVULET_TABLE_HPP
#define RIVULET_TABLE_HPP

#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace rivulet {

    class Table {
    public:
        Table() = default;

        // Column management
        void add_column(const std::string& name, std::vector<double> data);
        void add_date_column(std::vector<Timestamp> dates);

        bool has_column(const std::string& name) const;
        bool has_date_column() const;

        std::vector<std::string> column_names() const;
        std::size_t num_rows() const;
        std::size_t num_columns() const;

        // Data access
        const std::vector<double>& get_column(const std::string& name) const;
        std::vector<double>& get_column_mut(const std::string& name);

        const std::vector<Timestamp>& get_date_column() const;

        // Get value at specific row/column
        double at(std::size_t row, const std::string& col) const;
        std::optional<Timestamp> date_at(std::size_t row) const;

        // Reordering and filtering
        void reorder_columns(const std::vector<std::string>& new_order);
        void sort_by_date();
        void remove_rows(const std::vector<std::size_t>& indices_to_remove);

        // Validation
        bool is_sorted_by_date() const;
        bool all_columns_same_length() const;

        bool load_from_csv(const std::filesystem::path & path, TimeMode mode, const std::string & string);

    private:
        std::unordered_map<std::string, std::vector<double>> columns_;
        std::optional<std::vector<Timestamp>> dates_;
        std::vector<std::string> column_order_;
    };

} // namespace rivulet

#endif //RIVULET_TABLE_HPP