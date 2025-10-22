/*==============================================================================
File: include/rindle/dataset_types.hpp

  Overview:
    Defines the tensor containers and window metadata structures returned by
    the public dataset-loading APIs.

  Key functionality:
    - Tensor3D models a contiguous [window, sequence, feature] block.
    - WindowMeta captures the provenance for each generated window.
    - Dataset bundles feature/target tensors together with their metadata.
==============================================================================*/

#ifndef RIVULET_DATASET_TYPES_HPP
#define RIVULET_DATASET_TYPES_HPP

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rivulet {

// Compact row-major 3D tensor: [Window][Sequence][Feature] contiguous.
struct Tensor3D {
  using value_type = float;

  std::vector<value_type> data;
  std::int64_t windows = 0;
  std::int64_t seq_len = 0;
  std::int64_t features = 0;

  Tensor3D() = default;
  Tensor3D(std::int64_t W, std::int64_t S, std::int64_t F)
      : data(static_cast<std::size_t>(W * S * F)), windows(W), seq_len(S), features(F) {}

  std::size_t size() const {
    return static_cast<std::size_t>(windows * seq_len * features);
  }

  std::size_t offset(std::int64_t w, std::int64_t s, std::int64_t f) const {
    return static_cast<std::size_t>((w * seq_len + s) * features + f);
  }

  value_type& at(std::int64_t w, std::int64_t s, std::int64_t f) {
    return data[offset(w, s, f)];
  }
  const value_type& at(std::int64_t w, std::int64_t s, std::int64_t f) const {
    return data[offset(w, s, f)];
  }

  value_type* window_ptr(std::int64_t w) {
    return data.data() + static_cast<std::size_t>(w * seq_len * features);
  }
  const value_type* window_ptr(std::int64_t w) const {
    return data.data() + static_cast<std::size_t>(w * seq_len * features);
  }

  void reshape(std::int64_t W, std::int64_t S, std::int64_t F) {
    data.resize(static_cast<std::size_t>(W * S * F));
    windows = W; seq_len = S; features = F;
  }
};

// Origin metadata per window; mirrors your WindowRow lineage.
struct WindowMeta {
  std::string ticker;
  std::int64_t start_row = 0; // inclusive
  std::int64_t end_row = 0;   // inclusive
  std::optional<std::int64_t> target_start;
  std::optional<std::int64_t> target_end;
};

// Final dataset container: X and Y are both [W, S, F].
struct Dataset {
  Tensor3D X;                        // features
  Tensor3D Y;                        // targets
  std::vector<WindowMeta> meta;      // one per window

  std::int64_t n_windows() const { return X.windows; }
  std::int64_t seq_length() const { return X.seq_len; }
  std::int64_t n_features() const { return X.features; }
  std::int64_t n_target_features() const { return Y.features; }

  bool aligned_by_window_and_time() const {
    return X.windows == Y.windows && X.seq_len == Y.seq_len;
  }

  void clear() { X = {}; Y = {}; meta.clear(); }
};

} // namespace rivulet

#endif // RIVULET_DATASET_TYPES_HPP