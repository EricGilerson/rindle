//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/window_maker.cpp

  Purpose:
    Implements sliding-window construction and target alignment.

  Responsibilities:
    - Produce flattened, time-major windows of length L over features F.
    - Align targets to the window end row and take H future steps.
    - Populate index rows with original timestamps or ordinal indices.

  Notes:
    - Guarantees “no future leakage”: windows use rows ≤ end, targets start at end.
==============================================================================*/

#include "rivulet/window_maker.hpp"
#include "filesystem"
#include "rivulet/driver.hpp"

namespace rivulet {
  bool build_and_write_manifest_csv(const WindowSpec& spec,
                                    const std::string& manifest_csv_path,
                                    std::string* error_msg) {
    std::vector<WindowRow> windows = make_windows(spec, error_msg);
    write_windows_manifest_csv(manifest_csv_path, windows, error_msg);
    return true; // Placeholder
  }

  std::vector<WindowRow> make_windows(const WindowSpec& spec,
                                      std::string* error_msg) {
    std::vector<WindowRow> all_windows;
    for (const auto& ticker : spec.tickers) {
      SingleTickerWindowSpec single_spec{
        .ticker = ticker,
        .window_length_ns = spec.window_length_ns,
        .step_ns = spec.step_ns,
        .horizon_ns = spec.horizon_ns,
        .with_targets = spec.with_targets,
        .min_history_ns = spec.min_history_ns,
        .max_gap_ns = spec.max_gap_ns
      };
      const TickerStats *stats = manifest.content().find_stats(ticker);
      if (!stats) {
        if (error_msg) *error_msg = "No TickerStats found for ticker: " + ticker;
        return {};
      }

      //get ticker stats for that ticker
      auto windows = make_windows_for_ticker(single_spec, error_msg, stats);
      if (!error_msg->empty()) {
        return {};
      }
      all_windows.insert(all_windows.end(), windows.begin(), windows.end());
    }
    return all_windows;
  }

  std::vector<WindowRow> make_windows_for_ticker(const SingleTickerWindowSpec& spec, std::string* error_msg,const TickerStats *ticker_stats) {
    std::vector<WindowRow> windows;
    //get ticker information on number of rows then make windows based on that
    int start = spec.window_length_ns - 1;
    int end = ticker_stats->input_rows - spec.horizon_ns - 1;
    for (int i = start; i <= end; i += spec.step_ns) {
      WindowRow row;
      row.ticker = spec.ticker;
      row.window_start = i - (spec.window_length_ns - 1);
      row.window_end = i;
      if (spec.with_targets) {
        row.target_start = i - (spec.window_length_ns - 1);
        row.target_end = row.target_start + spec.horizon_ns;
      }
      windows.push_back(row);
    }

    return windows;
  }
}