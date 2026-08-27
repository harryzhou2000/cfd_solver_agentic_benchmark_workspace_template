// cns2d -- solution storage.
//
// Conserved states, primitive states, gradients and limiter factors live in
// flat, cache-friendly arrays sized to the rank-local cell count (owned +
// ghost).  There is no global-size array anywhere in this structure.
#pragma once

#include <vector>

#include "core/types.h"

namespace cns2d {

// Flat array of kNumVars values per local cell.
class StateField {
 public:
  StateField() = default;
  explicit StateField(Index num_cells) { resize(num_cells); }

  void resize(Index num_cells) {
    num_cells_ = num_cells;
    data_.assign(static_cast<std::size_t>(num_cells) * kNumVars, 0.0);
  }

  Index numCells() const { return num_cells_; }

  Real *cell(Index c) { return data_.data() + static_cast<std::size_t>(c) * kNumVars; }
  const Real *cell(Index c) const { return data_.data() + static_cast<std::size_t>(c) * kNumVars; }

  ConsVec get(Index c) const {
    const Real *p = cell(c);
    ConsVec out{};
    for (int k = 0; k < kNumVars; ++k) out[k] = p[k];
    return out;
  }
  void set(Index c, const ConsVec &v) {
    Real *p = cell(c);
    for (int k = 0; k < kNumVars; ++k) p[k] = v[k];
  }

  Real *data() { return data_.data(); }
  const Real *data() const { return data_.data(); }
  std::size_t size() const { return data_.size(); }
  void setZero() { std::fill(data_.begin(), data_.end(), 0.0); }

 private:
  Index num_cells_{0};
  std::vector<Real> data_;
};

// Gradients of the kNumVars primitive variables: kNumVars * kDim per cell.
class GradientField {
 public:
  static constexpr int kComponents = kNumVars * kDim;

  GradientField() = default;
  explicit GradientField(Index num_cells) { resize(num_cells); }

  void resize(Index num_cells) {
    num_cells_ = num_cells;
    data_.assign(static_cast<std::size_t>(num_cells) * kComponents, 0.0);
  }

  Index numCells() const { return num_cells_; }

  Real *cell(Index c) { return data_.data() + static_cast<std::size_t>(c) * kComponents; }
  const Real *cell(Index c) const { return data_.data() + static_cast<std::size_t>(c) * kComponents; }

  Vec2 get(Index c, int var) const {
    const Real *p = cell(c) + static_cast<std::size_t>(var) * kDim;
    return {p[0], p[1]};
  }
  void set(Index c, int var, Vec2 g) {
    Real *p = cell(c) + static_cast<std::size_t>(var) * kDim;
    p[0] = g.x;
    p[1] = g.y;
  }

  PrimGrad getAll(Index c) const {
    const Real *p = cell(c);
    PrimGrad out{};
    for (int v = 0; v < kNumVars; ++v) {
      out[static_cast<std::size_t>(v)] = {p[v * kDim + 0], p[v * kDim + 1]};
    }
    return out;
  }

  Real *data() { return data_.data(); }
  const Real *data() const { return data_.data(); }
  void setZero() { std::fill(data_.begin(), data_.end(), 0.0); }

 private:
  Index num_cells_{0};
  std::vector<Real> data_;
};

}  // namespace cns2d
