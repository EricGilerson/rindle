// window_maker.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rindle/types.hpp"
#include "window_manifest.hpp"

namespace rivulet {

/**
 * Configuration for generating windows from a base time series store.
 * The base store is assumed to be keyed by (ticker, timestamp_ns).
 */
struct WindowSpec {
  // Universe selection
  std::vector<std::string> tickers;           // empty means all available
  std::optional<std::int64_t> start_ns;       // inclusive; empty means earliest
  std::optional<std::int64_t> end_ns;         // exclusive; empty means latest

  // Window geometry
  std::int64_t window_length_ns = 0;          // length of each input window
  std::int64_t step_ns = 0;                   // stride between consecutive windows
  std::int64_t horizon_ns = 0;                // prediction horizon for targets

  // Target control
  bool with_targets = true;                   // set false for unlabeled windows

};

/**
 * Single-ticker window specification.
 * Used internally to process one ticker at a time.
 */
struct SingleTickerWindowSpec {
  std::string ticker;

  std::int64_t window_length_ns = 0;
  std::int64_t step_ns = 0;
  std::int64_t horizon_ns = 0;

  bool with_targets = true;
};

/**
 * Generate windows for a SINGLE ticker.
 * This is the core window generation logic for one time series.
 * Returns an empty vector on error and fills error_msg.
 */
std::vector<WindowRow> make_windows_for_ticker(const SingleTickerWindowSpec& spec,
                                               std::string* error_msg,
                                               const TickerStats *ticker_stats);

/**
 * Streaming variant for a single ticker.
 * Calls sink(row) for every generated window and stops early if sink returns false.
 * Returns true on success and fills error_msg on failure.
 */
using WindowSink = std::function<bool(const WindowRow&)>;

bool make_windows_for_ticker_streaming(const SingleTickerWindowSpec& spec,
                                       const WindowSink& sink,
                                       std::string* error_msg);

/**
 * Generate windows for ALL tickers specified in the WindowSpec.
 * This function:
 * 1. Iterates over each ticker in spec.tickers (or all available if empty)
 * 2. Calls make_windows_for_ticker for each one
 * 3. Aggregates results into a single vector
 *
 * Suitable for moderate datasets and testing.
 * Returns an empty vector on error and fills error_msg.
 */
std::vector<WindowRow> make_windows(const WindowSpec& spec,
                                    std::string* error_msg);

/**
 * Streaming variant for multiple tickers.
 * Processes tickers sequentially, calling sink for each window generated.
 * This is memory-efficient for large universes.
 * Returns true on success and fills error_msg on failure.
 */
bool make_windows_streaming(const WindowSpec& spec,
                            const WindowSink& sink,
                            std::string* error_msg);

/**
 * Parallel variant for multiple tickers.
 * Processes tickers in parallel and merges results.
 * Windows are NOT guaranteed to be in any particular order.
 * Use this for large universes where order doesn't matter.
 */
//std::vector<WindowRow> make_windows_parallel(const WindowSpec& spec,
//                                             std::size_t num_threads,
//                                             std::string* error_msg);


}  // namespace rivulet
