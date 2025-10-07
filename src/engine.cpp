//
// Created by Eric Gilerson on 10/6/25.
//

#include "rivulet/engine.hpp"
#include <deque>
#include <algorithm>

namespace rivulet {

struct Engine::KeyState {
  std::deque<Event> window;                // event-time ordered
  Timestamp max_event_time = Timestamp{};
};

static Timestamp min_ts() {
  return Timestamp(std::chrono::nanoseconds::min());
}

Engine::Engine(EngineConfig cfg) : cfg_(std::move(cfg)) {}

bool Engine::load_feature_spec(std::string_view, std::string& ) {
  // v0: stub that accepts anything. Later: parse JSON, build operator graph.
  return true;
}

bool Engine::ingest(const Event& e, std::string& error_msg) {
  auto& ks = state_[e.key];

  // Maintain max event-time
  if (ks.max_event_time < e.event_time) ks.max_event_time = e.event_time;

  // Compute watermark
  auto watermark = ks.max_event_time - cfg_.watermark_delay;

  // Late beyond watermark? drop
  if (e.event_time <= watermark) {
    error_msg = "late_event_dropped";
    return false;
  }

  // Insert in event-time order (small v0 approach; later use ring buffer)
  auto& w = ks.window;
  auto it = std::upper_bound(w.begin(), w.end(), e.event_time,
    [](const Timestamp& t, const Event& ev){ return t < ev.event_time; });
  w.insert(it, e);

  // Trim to max_window_samples
  while (w.size() > cfg_.max_window_samples) w.pop_front();

  return true;
}

std::optional<FeatureVector> Engine::feature_at(std::string_view key, Timestamp t, std::string& error_msg) const {
  auto it = state_.find(std::string(key));
  if (it == state_.end()) {
    error_msg = "unknown_key";
    return std::nullopt;
  }
  const auto& w = it->second.window;
  // Build a trivial v0 feature set: count, mean of a "price" field
  double sum = 0.0;
  std::size_t count = 0;

  for (const auto& ev : w) {
    if (ev.event_time > t) break;
    auto p = ev.fields.find("price");
    if (p != ev.fields.end()) { sum += p->second; ++count; }
  }

  if (count == 0) {
    error_msg = "no_data_before_t";
    return std::nullopt;
  }

  FeatureVector fv;
  fv.names = {"count", "mean_price"};
  fv.values = {static_cast<double>(count), sum / static_cast<double>(count)};
  return fv;
}

void Engine::flush() {
  // v0: no-op. Later: write parquet, manifests, etc.
}

} // namespace rivulet
