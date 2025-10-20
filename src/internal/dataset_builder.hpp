// include/rivulet/dataset_builder.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "window_manifest.hpp"
#include "window_maker.hpp"
#include "rindle/dataset_types.hpp"

namespace rivulet {

// Build parameters (kept simple; everything else comes from WindowSpec/manifest).
struct BuildSpec {
  std::int64_t seq_len = 0;
  std::int64_t feature_count = 0;
  std::int64_t target_count = 1;
  std::optional<std::int64_t> max_windows;  // cap if you want
};

// Summaries per ticker inside the plan.
struct TickerPlan {
  std::string ticker;
  std::int64_t window_count = 0;
  std::int64_t global_offset = 0; // starting window index in combined tensors
};

// Overall allocation plan derived from streaming the windows once.
struct BuildPlan {
  BuildSpec spec;
  std::vector<TickerPlan> tickers;
  std::int64_t total_windows = 0;

  std::size_t features_buffer_size() const {
    return static_cast<std::size_t>(total_windows * spec.seq_len * spec.feature_count);
  }
  std::size_t targets_buffer_size() const {
    return static_cast<std::size_t>(total_windows * spec.seq_len * spec.target_count);
  }
};

// Public builder that relies on your existing window/manifest utilities.
class DatasetBuilder {
public:
  DatasetBuilder() = default;

  // 1) Derive sizes by streaming windows (no CSV value reads here).
  //    Uses make_windows_streaming to avoid holding all WindowRow in memory.
  BuildPlan make_plan(const WindowSpec& wspec, const BuildSpec& dspec,
                      std::string* error_msg) const;

  // 2) Allocate tensors and metadata vectors according to the plan.
  Dataset allocate_from_plan(const BuildPlan& plan) const;

  // 3) Fill tensors by re-streaming windows and reading CSV blocks per (ticker, start_row).
  //    You implement the CSV readers; we drive shape/indexing.
  bool fill_data(const WindowSpec& wspec,
                 const BuildPlan& plan,
                 Dataset& out,
                 std::string* error_msg) const;

  // One-shot build: plan → allocate → fill.
  Dataset build(const WindowSpec& wspec, const BuildSpec& dspec, std::string* error_msg) const;

  // Single-ticker convenience (keeps shapes consistent).
  Dataset build_for_ticker(const std::string& ticker,
                           const WindowSpec& base_wspec,
                           const BuildSpec& dspec,
                           std::string* error_msg) const;

private:
  // Scratch readers you will provide in the .cpp:
  // Read [seq_len × feature_count] features starting at row_start (inclusive).
  bool read_features_block(const std::string& ticker,
                           std::int64_t row_start,
                           std::int64_t seq_len,
                           std::vector<float>& out_flat,   // size = seq_len*feature_count
                           std::string* error_msg) const;

  // Read [seq_len × target_count] targets aligned to the same rows.
  bool read_targets_block(const std::string& ticker,
                          std::int64_t row_start,
                          std::int64_t seq_len,
                          std::vector<float>& out_flat,   // size = seq_len*target_count
                          std::string* error_msg) const;
};

} // namespace rivulet

