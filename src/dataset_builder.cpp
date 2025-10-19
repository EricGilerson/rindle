// src/dataset_builder.cpp
#include "internal/dataset_builder.hpp"
#include <algorithm>
#include <cassert>
#include "internal/csv_io.hpp"
#include <unordered_map>
#include <filesystem>

namespace rivulet {

BuildPlan DatasetBuilder::make_plan(const WindowSpec& wspec,
                                    const BuildSpec& dspec,
                                    std::string* error_msg) const {
  if (error_msg) *error_msg = {};

  BuildPlan plan;
  plan.spec = dspec;

  // First pass: count windows per ticker using your streaming API.
  std::vector<TickerPlan> tplans;
  tplans.reserve(wspec.tickers.size());

  std::int64_t running_offset = 0;
  for (const auto& t : wspec.tickers) {
    std::int64_t count_for_ticker = 0;

    // Stream rows just to count; sink returns true to continue.
    SingleTickerWindowSpec single{
      .ticker = t,
      .window_length_ns = wspec.window_length_ns,
      .step_ns = wspec.step_ns,
      .horizon_ns = wspec.horizon_ns,
      .with_targets = wspec.with_targets
    };

    auto counter_sink = [&](const WindowRow&) -> bool {
      if (dspec.max_windows && (plan.total_windows + count_for_ticker) >= *dspec.max_windows) {
        return false; // stop early if capped
      }
      ++count_for_ticker;
      return true;
    };

    if (!make_windows_for_ticker_streaming(single, counter_sink, error_msg)) {
      // error_msg already set by callee if needed
      return plan;
    }

    TickerPlan tp;
    tp.ticker = t;
    tp.window_count = count_for_ticker;
    tp.global_offset = running_offset;

    running_offset += count_for_ticker;
    plan.total_windows += count_for_ticker;
    tplans.push_back(std::move(tp));
  }

  plan.tickers = std::move(tplans);
  return plan;
}

Dataset DatasetBuilder::allocate_from_plan(const BuildPlan& plan) const {
  Dataset ds;
  ds.X = Tensor3D(plan.total_windows, plan.spec.seq_len, plan.spec.feature_count);
  ds.Y = Tensor3D(plan.total_windows, plan.spec.seq_len, plan.spec.target_count);
  ds.meta.resize(static_cast<std::size_t>(plan.total_windows));
  return ds;
}

bool DatasetBuilder::fill_data(const WindowSpec& wspec,
                               const BuildPlan& plan,
                               Dataset& out,
                               std::string* error_msg) const {
  if (error_msg) *error_msg = {};
  if (out.X.windows != plan.total_windows || out.Y.windows != plan.total_windows) {
    if (error_msg) *error_msg = "Dataset tensors not allocated to plan sizes";
    return false;
  }
  if (out.X.seq_len != plan.spec.seq_len || out.Y.seq_len != plan.spec.seq_len) {
    if (error_msg) *error_msg = "Sequence length mismatch between plan and dataset";
    return false;
  }

  const std::int64_t S = plan.spec.seq_len;
  const std::int64_t Fx = plan.spec.feature_count;
  const std::int64_t Fy = plan.spec.target_count;
  const bool need_targets = (Fy > 0) && wspec.with_targets;

  std::vector<float> fx_buf(static_cast<std::size_t>(S * Fx));
  std::vector<float> fy_buf;
  if (need_targets) {
    fy_buf.resize(static_cast<std::size_t>(S * Fy));
  }

  // Second pass: stream again to materialize X/Y and meta.
  std::int64_t global_w = 0;

  auto sink = [&](const WindowRow& r) -> bool {
    const std::string& t = r.ticker;
    const std::int64_t start_row = r.window_start;
    const std::int64_t end_row = r.window_end;

    // Caller supplies these readers; they fill contiguous [S*F] buffers.
    if (!read_features_block(t, start_row, S, fx_buf, error_msg)) return false;
    if (need_targets) {
      if (!read_targets_block(t, start_row, S, fy_buf, error_msg)) return false;
    }

    // Copy into contiguous window block.
    float* xdst = out.X.window_ptr(global_w);
    std::copy(fx_buf.begin(), fx_buf.end(), xdst);

    if (need_targets) {
      float* ydst = out.Y.window_ptr(global_w);
      std::copy(fy_buf.begin(), fy_buf.end(), ydst);
    }

    // Meta
    WindowMeta meta;
    meta.ticker = t;
    meta.start_row = start_row;
    meta.end_row = end_row;
    meta.target_start = r.target_start;
    meta.target_end = r.target_end;
    out.meta[static_cast<std::size_t>(global_w)] = std::move(meta);

    ++global_w;
    if (plan.spec.max_windows && global_w >= *plan.spec.max_windows) {
      return false; // stop once filled cap
    }
    return true;
  };

  if (!make_windows_streaming(wspec, sink, error_msg)) {
    // If the sink stopped us due to max_windows, treat as success.
    if (error_msg && !error_msg->empty()) return false;
  }

  return true;
}

Dataset DatasetBuilder::build(const WindowSpec& wspec,
                              const BuildSpec& dspec,
                              std::string* error_msg) const {
  auto plan = make_plan(wspec, dspec, error_msg);
  if (error_msg && !error_msg->empty()) return {};
  auto ds = allocate_from_plan(plan);
  if (!fill_data(wspec, plan, ds, error_msg)) return {};
  return ds;
}

Dataset DatasetBuilder::build_for_ticker(const std::string& ticker,
                                         const WindowSpec& base_wspec,
                                         const BuildSpec& dspec,
                                         std::string* error_msg) const {
  WindowSpec w = base_wspec;
  w.tickers.clear();
  w.tickers.push_back(ticker);

  return build(w, dspec, error_msg);
}
// naive placeholder — replace with your real resolver (e.g., Catalog/Manifest)
inline std::filesystem::path resolve_csv_path_for_ticker(const std::string& ticker) {
  return std::filesystem::path(ticker + ".csv");
}

// cache: ticker -> loaded frame
struct CsvCache {
  std::unordered_map<std::string, std::shared_ptr<rivulet::CsvFrame>> frames;
  bool ensure_loaded(const std::string& ticker, std::string* error_msg) {
    if (frames.find(ticker) != frames.end()) return true;
    auto frame = std::make_shared<rivulet::CsvFrame>();
    std::string err;
    rivulet::CsvIO io;
    const auto path = resolve_csv_path_for_ticker(ticker);
    if (!io.read_time_series_csv(path, frame.get(), err)) {
      if (error_msg) *error_msg = err;
      return false;
    }
    frames.emplace(ticker, std::move(frame));
    return true;
  }
  const rivulet::CsvFrame* get(const std::string& ticker) const {
    auto it = frames.find(ticker);
    return (it == frames.end()) ? nullptr : it->second.get();
  }
};

inline CsvCache& cache() {
  static CsvCache c;
  return c;
}

} // namespace

bool rivulet::DatasetBuilder::read_features_block(const std::string& ticker,
                                                  std::int64_t row_start,
                                                  std::int64_t seq_len,
                                                  std::vector<float>& out_flat,
                                                  std::string* error_msg) const {
  if (error_msg) *error_msg = {};
  // Ensure frame is cached
  if (!cache().ensure_loaded(ticker, error_msg)) return false;
  const CsvFrame* frame = cache().get(ticker);
  if (!frame) { if (error_msg) *error_msg = "CSV frame missing after load"; return false; }

  const std::int64_t nrows = static_cast<std::int64_t>(frame->date_ns.size());
  const std::int64_t F = (seq_len > 0) ? static_cast<std::int64_t>(out_flat.size()) / seq_len : 0;

  if (seq_len <= 0 || F <= 0) { if (error_msg) *error_msg = "Invalid seq_len or output size for features"; return false; }
  if (row_start < 0 || row_start + seq_len > nrows) {
    if (error_msg) *error_msg = "Requested feature block is out of bounds";
    return false;
  }
  if (frame->features.size() < static_cast<std::size_t>(F)) {
    if (error_msg) *error_msg = "CSV has fewer feature columns than requested";
    return false;
  }

  // Fill row-major: for s in [0,S), for f in [0,F) -> out[s*F + f]
  float* dst = out_flat.data();
  for (std::int64_t s = 0; s < seq_len; ++s) {
    const std::int64_t src_row = row_start + s;
    for (std::int64_t f = 0; f < F; ++f) {
      const auto& col = frame->features[static_cast<std::size_t>(f)];
      const double v = col[static_cast<std::size_t>(src_row)];
      dst[s * F + f] = static_cast<float>(v);
    }
  }
  return true;
}

bool rivulet::DatasetBuilder::read_targets_block(const std::string& ticker,
                                                 std::int64_t row_start,
                                                 std::int64_t seq_len,
                                                 std::vector<float>& out_flat,
                                                 std::string* error_msg) const {
  if (error_msg) *error_msg = {};
  // Reuse the same cached frame; if your targets come from a different source,
  // swap the loader here.
  if (!cache().ensure_loaded(ticker, error_msg)) return false;
  const CsvFrame* frame = cache().get(ticker);
  if (!frame) { if (error_msg) *error_msg = "CSV frame missing after load"; return false; }

  const std::int64_t nrows = static_cast<std::int64_t>(frame->date_ns.size());
  const std::int64_t T = (seq_len > 0) ? static_cast<std::int64_t>(out_flat.size()) / seq_len : 0;

  if (seq_len <= 0 || T <= 0) { if (error_msg) *error_msg = "Invalid seq_len or output size for targets"; return false; }
  if (row_start < 0 || row_start + seq_len > nrows) {
    if (error_msg) *error_msg = "Requested target block is out of bounds";
    return false;
  }

  // Default policy:
  //   If T <= available columns, take the *last T* columns as targets.
  //   Adjust here to use your manifest.target_column(s) mapping if desired.
  const std::int64_t F_available = static_cast<std::int64_t>(frame->features.size());
  if (T > F_available) {
    if (error_msg) *error_msg = "Requested target channels exceed available CSV columns";
    return false;
  }
  const std::int64_t first_t_col = F_available - T;

  float* dst = out_flat.data();
  for (std::int64_t s = 0; s < seq_len; ++s) {
    const std::int64_t src_row = row_start + s;
    for (std::int64_t t = 0; t < T; ++t) {
      const auto& col = frame->features[static_cast<std::size_t>(first_t_col + t)];
      const double v = col[static_cast<std::size_t>(src_row)];
      dst[s * T + t] = static_cast<float>(v);
    }
  }
  return true;
}// namespace rivulet
