//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/window_maker.cpp

Overview:
    Implements sliding-window generation using ticker statistics from the
    manifest to produce window ranges and optional prediction targets.

  Key functionality:
    - Stream or materialize windows per ticker based on the configured length,
      stride, and horizon while preventing look-ahead leakage.
    - Support on-the-fly manifest writing by piping streamed rows into the
      binary window-manifest helpers.
    - Provide convenience wrappers that aggregate windows across multiple
      tickers when working in memory.
==============================================================================*/

#include "internal/window_maker.hpp"
#include <filesystem>
#include <limits>
#include "internal/driver.hpp"

namespace rivulet {
  bool build_and_write_manifest_parquet(const WindowSpec& spec,
                                        const std::string& manifest_path,
                                        std::string* error_msg) {
    if (error_msg) *error_msg = {};

    // Define a sink that appends each streamed row to the manifest file.
    // Return false only on I/O failure (so we don't stop early by design).
    auto sink = [&](const WindowRow& row) -> bool {
      return append_windows_manifest_parquet(manifest_path, row, error_msg);
    };

    // Produce windows and push them directly to the sink.
    // On failure, error_msg is already set (I/O or generator constraint).
    if (!make_windows_streaming(spec, sink, error_msg)) {
      if (error_msg && error_msg->empty()) {
        *error_msg = "make_windows_streaming failed";
      }
      return false;
    }

    return true;
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

  std::vector<WindowRow> make_windows_for_ticker(const SingleTickerWindowSpec& spec,
                                                 std::string* error_msg,
                                                 const TickerStats* ticker_stats) {
    if (error_msg) *error_msg = {};

    std::vector<WindowRow> windows;

    if (!ticker_stats || ticker_stats->input_rows == 0) {
      if (error_msg) *error_msg = "Invalid or empty TickerStats";
      return windows;
    }
    if (spec.window_length_ns <= 0 || spec.step_ns <= 0) {
      if (error_msg) *error_msg = "window_length_ns and step_ns must be positive";
      return windows;
    }

    if (ticker_stats->input_rows > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
      if (error_msg) *error_msg = "TickerStats input_rows exceeds supported range";
      return windows;
    }

    const std::int64_t total_rows = static_cast<std::int64_t>(ticker_stats->input_rows);
    const std::int64_t start = spec.window_length_ns - 1;
    const std::int64_t end   = total_rows - spec.horizon_ns - 1;

    if (end < start) {
      // Not enough history; not an error.
      return windows;
    }

    for (std::int64_t i = start; i <= end; i += spec.step_ns) {
      WindowRow row;
      row.ticker = spec.ticker;
      row.window_start = i - (spec.window_length_ns - 1);
      row.window_end   = i;

      if (spec.with_targets) {
        const std::int64_t tstart = i - (spec.window_length_ns - 1);
        row.target_start = tstart;                    // optional gets a value
        row.target_end   = tstart + spec.horizon_ns;  // assign plain int64_t
      } else {
        row.target_start.reset();
        row.target_end.reset();
      }

      windows.push_back(std::move(row));
    }

    return windows;
  }


bool make_windows_streaming(const WindowSpec &spec,
                            const WindowSink &sink,
                            std::string *error_msg) {
  if (error_msg) error_msg->clear();

  for (const auto& ticker : spec.tickers) {
    SingleTickerWindowSpec single_spec{
      .ticker = ticker,
      .window_length_ns = spec.window_length_ns,
      .step_ns = spec.step_ns,
      .horizon_ns = spec.horizon_ns,
      .with_targets = spec.with_targets
    };

    // Lookup stats for this ticker
    const TickerStats* stats = manifest.content().find_stats(ticker);
    if (!stats) {
      if (error_msg) *error_msg = "No TickerStats found for ticker: " + ticker;
      return false;
    }

    // Stream all windows for this ticker
    if (!make_windows_for_ticker_streaming(single_spec, sink, error_msg)) {
      // error_msg already set by callee on failure or early stop
      return false;
    }
  }

  return true;
}

bool make_windows_for_ticker_streaming(const SingleTickerWindowSpec &spec,
                                       const WindowSink &sink,
                                       std::string *error_msg) {
  if (error_msg) error_msg->clear();

  // Retrieve stats for this ticker (needed for input_rows bound)
  const TickerStats* stats = manifest.content().find_stats(spec.ticker);
  if (!stats) {
    if (error_msg) *error_msg = "No TickerStats found for ticker: " + spec.ticker;
    return false;
  }

  // Guardrails for indices based on your non-streaming logic
  const std::int64_t L = spec.window_length_ns;
  const std::int64_t H = spec.horizon_ns;
  const std::int64_t step = spec.step_ns;

  if (L <= 0 || step <= 0) {
    if (error_msg) *error_msg = "Invalid window parameters: window_length and step must be positive.";
    return false;
  }
  if (stats->input_rows == 0) {
    if (error_msg) *error_msg = "Ticker has no input rows: " + spec.ticker;
    return false;
  }
  if (stats->input_rows > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
    if (error_msg) *error_msg = "TickerStats input_rows exceeds supported range";
    return false;
  }

  const std::int64_t total_rows = static_cast<std::int64_t>(stats->input_rows);
  // Match the index math from your vector-building version
  const std::int64_t start_i = L - 1;
  const std::int64_t end_i   = total_rows - H - 1;

  if (end_i < start_i) {
    // Not enough history to form a single window
    return true; // Not an error; simply nothing to stream
  }

  for (std::int64_t i = start_i; i <= end_i; i += step) {
    WindowRow row;
    row.ticker       = spec.ticker;
    row.window_start = i - (L - 1);
    row.window_end   = i;

    if (spec.with_targets) {
      // Your current non-streaming version aligns targets beginning at the same
      // start index and extending H steps
      row.target_start = row.window_start;
      row.target_end   = row.window_start + H;
    } else {
      row.target_start.reset();
      row.target_end.reset();
    }

    // Push to sink; allow sink to stop streaming early by returning false
    if (!sink(row)) {
      if (error_msg) *error_msg = "Window streaming stopped by sink for ticker: " + spec.ticker;
      return false;
    }
  }

  return true;
}


}
