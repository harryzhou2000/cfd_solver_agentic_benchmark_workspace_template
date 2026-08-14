#include "cfd/output.hpp"

#include "cfd/gas.hpp"
#include "cfd/provenance.hpp"

#include <nlohmann/json.hpp>

#include <mpi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace cfd {
namespace {

using Json = nlohmann::json;

class Sha256 {
 public:
  Sha256()
      : state_{{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U}} {}

  void update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += size;
    while (size > 0U) {
      const std::size_t take = std::min(size, block_.size() - block_size_);
      std::copy_n(bytes, take, block_.begin() + static_cast<std::ptrdiff_t>(block_size_));
      block_size_ += take;
      bytes += take;
      size -= take;
      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0U;
      }
    }
  }

  std::string finish() {
    const std::uint64_t bits = static_cast<std::uint64_t>(total_bytes_) * 8U;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0U);
      transform(block_.data());
      block_size_ = 0U;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0U);
    for (int i = 0; i < 8; ++i) {
      block_[63U - static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(bits >> static_cast<unsigned>(8 * i));
    }
    transform(block_.data());
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (std::uint32_t value : state_) text << std::setw(8) << value;
    return text.str();
  }

 private:
  static std::uint32_t rotate(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
  }

  void transform(const std::uint8_t* input) {
    static constexpr std::array<std::uint32_t, 64> constants{{
      0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
      0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
      0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
      0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
      0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
      0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
      0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
      0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U}};
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16U; ++i) {
      words[i] = (static_cast<std::uint32_t>(input[4U * i]) << 24U) |
                 (static_cast<std::uint32_t>(input[4U * i + 1U]) << 16U) |
                 (static_cast<std::uint32_t>(input[4U * i + 2U]) << 8U) |
                 static_cast<std::uint32_t>(input[4U * i + 3U]);
    }
    for (std::size_t i = 16U; i < words.size(); ++i) {
      const std::uint32_t s0 = rotate(words[i - 15U], 7U) ^ rotate(words[i - 15U], 18U) ^
                               (words[i - 15U] >> 3U);
      const std::uint32_t s1 = rotate(words[i - 2U], 17U) ^ rotate(words[i - 2U], 19U) ^
                               (words[i - 2U] >> 10U);
      words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }
    std::uint32_t a=state_[0], b=state_[1], c=state_[2], d=state_[3];
    std::uint32_t e=state_[4], f=state_[5], g=state_[6], h=state_[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
      const std::uint32_t s1 = rotate(e,6U) ^ rotate(e,11U) ^ rotate(e,25U);
      const std::uint32_t choice = (e & f) ^ ((~e) & g);
      const std::uint32_t t1 = h + s1 + choice + constants[i] + words[i];
      const std::uint32_t s0 = rotate(a,2U) ^ rotate(a,13U) ^ rotate(a,22U);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + majority;
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
  }

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_{};
  std::size_t total_bytes_{};
};

std::string sha256_bytes(const void* data, std::size_t size) {
  Sha256 hash;
  hash.update(data, size);
  return hash.finish();
}

int mpi_count(std::size_t value, const char* what) {
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::overflow_error(std::string(what) + " exceeds MPI int count");
  }
  return static_cast<int>(value);
}

void mpi_check(int status, const char* what) {
  if (status != MPI_SUCCESS) throw std::runtime_error(std::string("MPI ") + what + " failed");
}

template <class T>
void append(std::vector<std::uint8_t>& bytes, const T& value) {
  static_assert(std::is_trivially_copyable<T>::value, "binary value must be trivial");
  const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
  bytes.insert(bytes.end(), first, first + sizeof(T));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value) {
  const std::uint64_t size = static_cast<std::uint64_t>(value.size());
  append(bytes, size);
  bytes.insert(bytes.end(), value.begin(), value.end());
}

class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

  template <class T>
  T get() {
    static_assert(std::is_trivially_copyable<T>::value, "binary value must be trivial");
    if (offset_ > bytes_.size() || sizeof(T) > bytes_.size() - offset_) {
      throw std::runtime_error("truncated binary payload");
    }
    T value{};
    std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
    offset_ += sizeof(T);
    return value;
  }

  std::string string() {
    const std::uint64_t wide = get<std::uint64_t>();
    if (wide > static_cast<std::uint64_t>(bytes_.size() - offset_)) {
      throw std::runtime_error("truncated binary string");
    }
    const auto size = static_cast<std::size_t>(wide);
    std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
    offset_ += size;
    return value;
  }

  bool done() const noexcept { return offset_ == bytes_.size(); }

 private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t offset_{};
};

std::vector<std::uint8_t> gather_bytes(const std::vector<std::uint8_t>& local,
                                       std::vector<int>& counts,
                                       std::vector<int>& displacements,
                                       MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  mpi_check(MPI_Comm_rank(communicator, &rank), "Comm_rank");
  mpi_check(MPI_Comm_size(communicator, &size), "Comm_size");
  const int local_count = mpi_count(local.size(), "byte payload");
  if (rank == 0) counts.resize(static_cast<std::size_t>(size));
  mpi_check(MPI_Gather(&local_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, communicator),
            "Gather byte counts");
  std::vector<std::uint8_t> global;
  if (rank == 0) {
    displacements.resize(static_cast<std::size_t>(size));
    std::size_t total = 0;
    for (int i = 0; i < size; ++i) {
      displacements[static_cast<std::size_t>(i)] = mpi_count(total, "byte displacement");
      total += static_cast<std::size_t>(counts[static_cast<std::size_t>(i)]);
    }
    global.resize(total);
  }
  mpi_check(MPI_Gatherv(local.data(), local_count, MPI_BYTE, global.data(), counts.data(),
                        displacements.data(), MPI_BYTE, 0, communicator),
            "Gatherv bytes");
  return global;
}

void broadcast_string(std::string& value, int root, MPI_Comm communicator) {
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  std::uint64_t size = rank == root ? static_cast<std::uint64_t>(value.size()) : 0U;
  mpi_check(MPI_Bcast(&size, 1, MPI_UINT64_T, root, communicator), "Bcast string size");
  if (size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::overflow_error("broadcast string is too large");
  }
  if (rank != root) value.resize(static_cast<std::size_t>(size));
  mpi_check(MPI_Bcast(value.data(), static_cast<int>(size), MPI_CHAR, root, communicator),
            "Bcast string");
}

void require_finite(double value, const std::string& name) {
  if (!std::isfinite(value)) throw std::runtime_error(name + " is not finite");
}

void validate_finite_json(const Json& value, const std::string& path = "$") {
  if (value.is_number_float()) {
    require_finite(value.get<double>(), "JSON value " + path);
  } else if (value.is_array()) {
    for (std::size_t i = 0; i < value.size(); ++i) {
      validate_finite_json(value[i], path + "[" + std::to_string(i) + "]");
    }
  } else if (value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      validate_finite_json(it.value(), path + "." + it.key());
    }
  }
}

void write_json(const std::filesystem::path& path, const Json& value) {
  validate_finite_json(value);
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open JSON output '" + path.string() + "'");
  output << std::setw(2) << value << '\n';
  if (!output) throw std::runtime_error("failed writing JSON output '" + path.string() + "'");
}

std::string list(const std::vector<int>& values) {
  std::ostringstream text;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) text << ';';
    text << values[i];
  }
  return text.str();
}

std::string list(const std::vector<std::size_t>& values) {
  std::ostringstream text;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) text << ';';
    text << values[i];
  }
  return text.str();
}

struct RestartRecord {
  GlobalId id{};
  std::array<double, 16> state{};
};

void append_inner_statistics(std::vector<std::uint8_t>& bytes,
                             const InnerStatistics& value) {
  append(bytes, static_cast<std::uint64_t>(value.samples));
  append(bytes, value.minimum);
  append(bytes, value.maximum);
  append(bytes, value.mean);
  append(bytes, static_cast<std::uint64_t>(value.target_misses));
  append(bytes, value.target_converged_fraction);
  append(bytes, value.last_ratio);
}

InnerStatistics read_inner_statistics(ByteReader& reader) {
  InnerStatistics value;
  value.samples = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.minimum = reader.get<int>();
  value.maximum = reader.get<int>();
  value.mean = reader.get<double>();
  value.target_misses = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.target_converged_fraction = reader.get<double>();
  value.last_ratio = reader.get<double>();
  return value;
}

std::vector<std::uint8_t> serialize_continuation(const ContinuationState& value) {
  std::vector<std::uint8_t> bytes;
  append(bytes, value.steady_residual_baseline);
  append(bytes, value.last_residual);
  append_inner_statistics(bytes, value.inner);
  append_string(bytes, value.original_start_time_utc);
  append(bytes, static_cast<std::uint8_t>(value.all_accepted_transient_targets));
  append(bytes, static_cast<std::uint8_t>(value.history_complete));
  append(bytes, static_cast<std::uint64_t>(value.rollbacks));
  append(bytes, static_cast<std::uint64_t>(value.total_attempted_steps));
  append(bytes, value.last_inner_iterations);
  append(bytes, static_cast<std::uint64_t>(value.residual_output_rows));
  append(bytes, static_cast<std::uint64_t>(value.force_output_rows));
  append(bytes, static_cast<std::uint8_t>(value.last_residual_output_step.has_value()));
  append(bytes, static_cast<std::uint64_t>(value.last_residual_output_step.value_or(0U)));
  append(bytes, static_cast<std::uint8_t>(value.last_force_output_step.has_value()));
  append(bytes, static_cast<std::uint64_t>(value.last_force_output_step.value_or(0U)));
  append(bytes, value.solver.cfl);
  append(bytes, static_cast<std::uint64_t>(value.solver.nonlinear_steps));
  append(bytes, value.solver.steady_previous_residual);
  append(bytes, value.solver.steady_best_residual);
  append(bytes, value.solver.steady_trend_reference_residual);
  append(bytes, value.solver.steady_trend_samples);
  append(bytes, static_cast<std::uint8_t>(value.solver.steady_probe_active));
  append(bytes, value.solver.steady_rejected_attempts);
  append(bytes, value.solver.steady_recovery_probe_cfl);
  append(bytes, static_cast<std::uint8_t>(
                     value.solver.steady_recovery_restore_pending));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_jfnk_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_attempts));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_rejected_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_cfl_halvings));
  append(bytes, value.solver.steady_last_fallback_cfl);
  append(bytes, static_cast<std::uint8_t>(value.solver.steady_fallback_mode));
  append(bytes, value.solver.steady_fallback_cfl);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_steps_since_jfnk));
  append(bytes, value.solver.steady_jfnk_failure_streak);
  append(bytes, static_cast<std::uint64_t>(value.solver.steady_jfnk_attempts));
  append(bytes, value.solver.steady_initial_residual_scale);
  append(bytes, value.solver.steady_reconstruction_blend);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_first_order_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_order_ramp_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_full_order_accepted_steps));
  append(bytes, value.solver.steady_full_order_initial_residual);
  append(bytes, value.solver.steady_full_order_best_residual);
  append(bytes, static_cast<std::uint8_t>(
                    value.solver.steady_order_rescue_promoted));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_rescue_attempts));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_rescue_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_rescue_total_gmres_iterations));
  append(bytes, value.solver.steady_rescue_last_gmres_iterations);
  append(bytes, value.solver.steady_rescue_max_gmres_iterations);
  append(bytes, value.solver.steady_rescue_last_gmres_ratio);
  append(bytes, value.solver.steady_rescue_last_cfl);
  append(bytes, value.solver.steady_rescue_last_line_scale);
  append(bytes, value.solver.steady_rescue_reference_residual);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_rescue_stagnation_count));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_rescue_cooldown_attempts));
  append(bytes, static_cast<std::uint8_t>(
                    value.solver.steady_fallback_disabled));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_consecutive_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_growth_disables));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_lusgs_preconditioner_applications));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_lusgs_preconditioner_sweeps));
  append(bytes, value.solver.steady_lusgs_last_defect_ratio);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_fallback_residual_window.size()));
  for (double residual : value.solver.steady_fallback_residual_window) {
    append(bytes, residual);
  }
  append(bytes, value.solver.transient_stats.observed_min);
  append(bytes, value.solver.transient_stats.observed_mean);
  append(bytes, value.solver.transient_stats.observed_max);
  append(bytes, static_cast<std::uint64_t>(value.solver.transient_stats.target_misses));
  append(bytes, value.solver.transient_stats.target_met_fraction);
  append(bytes, value.solver.transient_stats.last_ratio);
  append(bytes, static_cast<std::uint64_t>(value.solver.transient_samples));
  append(bytes, value.solver.transient_iteration_sum);
  append(bytes, static_cast<std::uint8_t>(value.solver.steady_target_met));
  append(bytes, static_cast<std::uint64_t>(value.force_window.size()));
  for (const auto& force : value.force_window) {
    append(bytes, force.first);
    append(bytes, force.second);
  }
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_trust_region_retry_batches));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_trust_region_retry_candidates));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_trust_region_retry_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_trust_region_retry_total_gmres_iterations));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_trust_region_retry_last_candidate_count));
  append(bytes,
         value.solver.steady_trust_region_retry_last_total_gmres_iterations);
  append(bytes,
         value.solver.steady_trust_region_retry_last_accepted_gmres_iterations);
  append(bytes, value.solver.steady_trust_region_retry_last_accepted_cfl);
  append(bytes, value.solver.steady_trust_region_retry_last_line_scale);
  append(bytes, value.solver.steady_trust_region_retry_last_initial_residual);
  append(bytes, value.solver.steady_trust_region_retry_last_final_residual);
  append(bytes, static_cast<std::uint64_t>(
                     value.solver.steady_trust_region_retry_cooldown_attempts));
  append(bytes, value.solver.steady_jfnk_epsilon_reference_residual);
  append(bytes, value.solver.steady_jfnk_epsilon_multiplier);
  append(bytes, value.solver.steady_jfnk_last_epsilon);
  append(bytes, value.solver.steady_jfnk_last_epsilon_halvings);
  append(bytes, static_cast<std::uint8_t>(
                    value.solver.steady_initial_residual_is_original_run));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_residual_window.size()));
  for (double residual : value.solver.steady_nonmonotone_residual_window) {
    append(bytes, residual);
  }
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_strict_decrease_stagnation_streak));
  append(bytes, static_cast<std::uint8_t>(
                    value.solver.steady_nonmonotone_bridge_active));
  append(bytes, static_cast<std::uint8_t>(
                    value.solver.steady_nonmonotone_bridge_disabled));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_steps_since_strict_best));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_accepted_steps));
  append(bytes, value.solver.steady_nonmonotone_max_relative_increase);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_strict_best_improvements));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_watchdog_resets));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_bypass_attempts));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_bypass_accepted_steps));
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_bypass_trial_evaluations));
  append(bytes,
         value.solver.steady_nonmonotone_bypass_last_actual_trial_residual);
  append(bytes, value.solver.steady_nonmonotone_bypass_last_gmres_ratio);
  append(bytes, value.solver.steady_nonmonotone_envelope_reference);
  append(bytes, static_cast<std::uint64_t>(
                    value.solver.steady_nonmonotone_envelope_accepted_steps));
  append(bytes,
         value.solver.steady_nonmonotone_envelope_max_relative_increase);
  const SteadyImplicitBridgeState& bridge = value.solver.steady_implicit_bridge;
  append(bytes, static_cast<std::uint8_t>(bridge.active));
  append(bytes, static_cast<std::uint8_t>(bridge.disabled));
  append(bytes, bridge.cfl);
  append(bytes, bridge.entry_best_residual);
  append(bytes, static_cast<std::uint64_t>(bridge.attempts));
  append(bytes, static_cast<std::uint64_t>(bridge.accepted_steps));
  append(bytes, static_cast<std::uint64_t>(bridge.rejected_steps));
  append(bytes,
         static_cast<std::uint64_t>(bridge.accepted_steps_since_best));
  append(bytes, bridge.residual_minimum);
  append(bytes, bridge.residual_maximum);
  append(bytes, bridge.maximum_relative_growth);
  append(bytes,
         static_cast<std::uint64_t>(bridge.meaningful_best_improvements));
  append(bytes, static_cast<std::uint64_t>(bridge.watchdog_stops));
  append(bytes, static_cast<std::uint64_t>(bridge.linear_sweeps));
  return bytes;
}

ContinuationState deserialize_continuation(const std::vector<std::uint8_t>& bytes,
                                           std::uint32_t restart_version) {
  ByteReader reader(bytes);
  ContinuationState value;
  value.steady_residual_baseline = reader.get<double>();
  value.last_residual = reader.get<double>();
  value.inner = read_inner_statistics(reader);
  value.original_start_time_utc = reader.string();
  value.all_accepted_transient_targets = reader.get<std::uint8_t>() != 0U;
  value.history_complete = reader.get<std::uint8_t>() != 0U;
  value.rollbacks = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.total_attempted_steps = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.last_inner_iterations = reader.get<int>();
  value.residual_output_rows = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.force_output_rows = static_cast<std::size_t>(reader.get<std::uint64_t>());
  const bool has_residual_step = reader.get<std::uint8_t>() != 0U;
  const auto residual_step = static_cast<std::size_t>(reader.get<std::uint64_t>());
  if (has_residual_step) value.last_residual_output_step = residual_step;
  const bool has_force_step = reader.get<std::uint8_t>() != 0U;
  const auto force_step = static_cast<std::size_t>(reader.get<std::uint64_t>());
  if (has_force_step) value.last_force_output_step = force_step;
  value.solver.cfl = reader.get<double>();
  value.solver.nonlinear_steps = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_previous_residual = reader.get<double>();
  value.solver.steady_best_residual = reader.get<double>();
  value.solver.steady_trend_reference_residual = reader.get<double>();
  value.solver.steady_trend_samples = reader.get<int>();
  value.solver.steady_probe_active = reader.get<std::uint8_t>() != 0U;
  value.solver.steady_rejected_attempts = reader.get<int>();
  value.solver.steady_recovery_probe_cfl = reader.get<double>();
  value.solver.steady_recovery_restore_pending =
      reader.get<std::uint8_t>() != 0U;
  value.solver.steady_jfnk_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_attempts =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_rejected_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_cfl_halvings =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_last_fallback_cfl = reader.get<double>();
  value.solver.steady_fallback_mode = reader.get<std::uint8_t>() != 0U;
  value.solver.steady_fallback_cfl = reader.get<double>();
  value.solver.steady_fallback_steps_since_jfnk =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_jfnk_failure_streak = reader.get<int>();
  value.solver.steady_jfnk_attempts =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_initial_residual_scale = reader.get<double>();
  value.solver.steady_reconstruction_blend = reader.get<double>();
  value.solver.steady_first_order_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_order_ramp_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_full_order_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_full_order_initial_residual = reader.get<double>();
  value.solver.steady_full_order_best_residual = reader.get<double>();
  value.solver.steady_order_rescue_promoted =
      reader.get<std::uint8_t>() != 0U;
  value.solver.steady_rescue_attempts =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_rescue_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_rescue_total_gmres_iterations =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_rescue_last_gmres_iterations = reader.get<int>();
  value.solver.steady_rescue_max_gmres_iterations = reader.get<int>();
  value.solver.steady_rescue_last_gmres_ratio = reader.get<double>();
  value.solver.steady_rescue_last_cfl = reader.get<double>();
  value.solver.steady_rescue_last_line_scale = reader.get<double>();
  value.solver.steady_rescue_reference_residual = reader.get<double>();
  value.solver.steady_rescue_stagnation_count =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_rescue_cooldown_attempts =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_disabled =
      reader.get<std::uint8_t>() != 0U;
  value.solver.steady_fallback_consecutive_accepted_steps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_fallback_growth_disables =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_lusgs_preconditioner_applications =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_lusgs_preconditioner_sweeps =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.steady_lusgs_last_defect_ratio = reader.get<double>();
  const std::uint64_t fallback_window_size = reader.get<std::uint64_t>();
  if (fallback_window_size > steady_fallback_window_capacity) {
    throw std::runtime_error("restart fallback residual window exceeds bound");
  }
  value.solver.steady_fallback_residual_window.resize(
      static_cast<std::size_t>(fallback_window_size));
  for (double& residual : value.solver.steady_fallback_residual_window) {
    residual = reader.get<double>();
    require_finite(residual, "restart fallback residual window value");
    if (residual < 0.0) {
      throw std::runtime_error(
          "restart fallback residual window contains negative value");
    }
  }
  value.solver.transient_stats.observed_min = reader.get<int>();
  value.solver.transient_stats.observed_mean = reader.get<double>();
  value.solver.transient_stats.observed_max = reader.get<int>();
  value.solver.transient_stats.target_misses =
      static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.transient_stats.target_met_fraction = reader.get<double>();
  value.solver.transient_stats.last_ratio = reader.get<double>();
  value.solver.transient_samples = static_cast<std::size_t>(reader.get<std::uint64_t>());
  value.solver.transient_iteration_sum = reader.get<double>();
  value.solver.steady_target_met = reader.get<std::uint8_t>() != 0U;
  const std::uint64_t forces = reader.get<std::uint64_t>();
  if (forces > 4096U) throw std::runtime_error("restart periodicity window exceeds bound");
  value.force_window.resize(static_cast<std::size_t>(forces));
  for (auto& force : value.force_window) {
    force.first = reader.get<double>();
    force.second = reader.get<double>();
  }
  if (restart_version >= 9U) {
    value.solver.steady_trust_region_retry_batches =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_trust_region_retry_candidates =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_trust_region_retry_accepted_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_trust_region_retry_total_gmres_iterations =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_trust_region_retry_last_candidate_count =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_trust_region_retry_last_total_gmres_iterations =
        reader.get<int>();
    value.solver.steady_trust_region_retry_last_accepted_gmres_iterations =
        reader.get<int>();
    value.solver.steady_trust_region_retry_last_accepted_cfl =
        reader.get<double>();
    value.solver.steady_trust_region_retry_last_line_scale =
        reader.get<double>();
    value.solver.steady_trust_region_retry_last_initial_residual =
        reader.get<double>();
    value.solver.steady_trust_region_retry_last_final_residual =
        reader.get<double>();
    value.solver.steady_trust_region_retry_cooldown_attempts =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
  }
  if (restart_version >= 10U) {
    value.solver.steady_jfnk_epsilon_reference_residual =
        reader.get<double>();
    value.solver.steady_jfnk_epsilon_multiplier = reader.get<double>();
    value.solver.steady_jfnk_last_epsilon = reader.get<double>();
    value.solver.steady_jfnk_last_epsilon_halvings = reader.get<int>();
  }
  value.solver.steady_initial_residual_is_original_run =
      restart_version >= 11U ? reader.get<std::uint8_t>() != 0U : false;
  if (restart_version >= 12U) {
    const std::uint64_t window_size = reader.get<std::uint64_t>();
    if (window_size > steady_nonmonotone_window_capacity) {
      throw std::runtime_error(
          "restart nonmonotone residual window exceeds bound");
    }
    value.solver.steady_nonmonotone_residual_window.resize(
        static_cast<std::size_t>(window_size));
    for (double& residual :
         value.solver.steady_nonmonotone_residual_window) {
      residual = reader.get<double>();
      require_finite(residual,
                     "restart nonmonotone residual window value");
      if (residual < 0.0) {
        throw std::runtime_error(
            "restart nonmonotone residual window contains negative value");
      }
    }
    value.solver.steady_strict_decrease_stagnation_streak =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_bridge_active =
        reader.get<std::uint8_t>() != 0U;
    value.solver.steady_nonmonotone_bridge_disabled =
        reader.get<std::uint8_t>() != 0U;
    value.solver.steady_nonmonotone_steps_since_strict_best =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_accepted_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_max_relative_increase =
        reader.get<double>();
    value.solver.steady_nonmonotone_strict_best_improvements =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_watchdog_resets =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
  }
  if (restart_version >= 13U) {
    value.solver.steady_nonmonotone_bypass_attempts =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_bypass_accepted_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_bypass_trial_evaluations =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_bypass_last_actual_trial_residual =
        reader.get<double>();
    value.solver.steady_nonmonotone_bypass_last_gmres_ratio =
        reader.get<double>();
  }
  if (restart_version >= 14U) {
    value.solver.steady_nonmonotone_envelope_reference = reader.get<double>();
    value.solver.steady_nonmonotone_envelope_accepted_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    value.solver.steady_nonmonotone_envelope_max_relative_increase =
        reader.get<double>();
  }
  if (restart_version >= 15U) {
    SteadyImplicitBridgeState& bridge =
        value.solver.steady_implicit_bridge;
    bridge.active = reader.get<std::uint8_t>() != 0U;
    bridge.disabled = reader.get<std::uint8_t>() != 0U;
    bridge.cfl = reader.get<double>();
    bridge.entry_best_residual = reader.get<double>();
    bridge.attempts =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.accepted_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.rejected_steps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.accepted_steps_since_best =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.residual_minimum = reader.get<double>();
    bridge.residual_maximum = reader.get<double>();
    bridge.maximum_relative_growth = reader.get<double>();
    bridge.meaningful_best_improvements =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.watchdog_stops =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
    bridge.linear_sweeps =
        static_cast<std::size_t>(reader.get<std::uint64_t>());
  }
  if (!reader.done()) throw std::runtime_error("restart continuation has trailing bytes");
  for (double number : {value.steady_residual_baseline, value.last_residual,
                        value.inner.mean, value.inner.target_converged_fraction,
                         value.inner.last_ratio, value.solver.cfl,
                         value.solver.steady_previous_residual,
                         value.solver.steady_best_residual,
                           value.solver.steady_trend_reference_residual,
                            value.solver.steady_recovery_probe_cfl,
                            value.solver.steady_last_fallback_cfl,
                            value.solver.steady_fallback_cfl,
                             value.solver.steady_initial_residual_scale,
                             value.solver.steady_reconstruction_blend,
                             value.solver.steady_full_order_initial_residual,
                              value.solver.steady_full_order_best_residual,
                              value.solver.steady_lusgs_last_defect_ratio,
                              value.solver
                                  .steady_trust_region_retry_last_accepted_cfl,
                              value.solver
                                  .steady_trust_region_retry_last_line_scale,
                              value.solver
                                  .steady_trust_region_retry_last_initial_residual,
                               value.solver
                                   .steady_trust_region_retry_last_final_residual,
                               value.solver
                                   .steady_jfnk_epsilon_reference_residual,
                                value.solver.steady_jfnk_epsilon_multiplier,
                                value.solver.steady_jfnk_last_epsilon,
                                 value.solver
                                     .steady_nonmonotone_max_relative_increase,
                                 value.solver
                                     .steady_nonmonotone_bypass_last_actual_trial_residual,
                                 value.solver
                                     .steady_nonmonotone_bypass_last_gmres_ratio,
                                 value.solver
                                     .steady_nonmonotone_envelope_reference,
                                 value.solver
                                     .steady_nonmonotone_envelope_max_relative_increase,
                                 value.solver.steady_implicit_bridge.cfl,
                                 value.solver.steady_implicit_bridge
                                     .entry_best_residual,
                                 value.solver.steady_implicit_bridge
                                     .residual_minimum,
                                 value.solver.steady_implicit_bridge
                                     .residual_maximum,
                                 value.solver.steady_implicit_bridge
                                     .maximum_relative_growth,
                          value.solver.transient_stats.observed_mean,
                        value.solver.transient_stats.target_met_fraction,
                        value.solver.transient_stats.last_ratio,
                        value.solver.transient_iteration_sum}) {
    require_finite(number, "restart continuation value");
  }
  return value;
}

struct CellRecord {
  GlobalId id{};
  int owner{};
  std::vector<Vec2> points;
  Conservative state{};
};

struct SurfaceRecord {
  GlobalId id{};
  BoundaryCondition condition{};
  Primitive state{};
  Vec2 normal{};
  Vec2 shear_force{};
  Vec2 center{};
  double length{};
  std::string tag;
};

std::vector<std::uint8_t> serialize_cells(const DistributedMesh& mesh,
                                          const RestartableSolution& solution) {
  std::vector<std::uint8_t> bytes;
  append(bytes, static_cast<std::uint64_t>(mesh.owned_cell_count));
  for (std::size_t i = 0; i < mesh.owned_cell_count; ++i) {
    const LocalCell& cell = mesh.cells[i];
    append(bytes, cell.global_id);
    append(bytes, cell.owner);
    append(bytes, static_cast<std::uint64_t>(cell.vertices.size()));
    for (LocalIndex vertex : cell.vertices) {
      const Vec2 point = mesh.vertices.at(static_cast<std::size_t>(vertex)).position;
      append(bytes, point.x);
      append(bytes, point.y);
    }
    for (std::size_t k = 0; k < 4U; ++k) append(bytes, solution.U[i * 4U + k]);
  }
  return bytes;
}

std::vector<CellRecord> deserialize_cells(const std::vector<std::uint8_t>& global,
                                          const std::vector<int>& counts,
                                          const std::vector<int>& displacements) {
  std::vector<CellRecord> cells;
  for (std::size_t rank = 0; rank < counts.size(); ++rank) {
    const auto first = global.begin() + displacements[rank];
    std::vector<std::uint8_t> payload(first, first + counts[rank]);
    ByteReader reader(payload);
    const auto count = reader.get<std::uint64_t>();
    for (std::uint64_t i = 0; i < count; ++i) {
      CellRecord cell;
      cell.id = reader.get<GlobalId>();
      cell.owner = reader.get<int>();
      const auto points = reader.get<std::uint64_t>();
      if (points != 3U && points != 4U) {
        throw std::runtime_error("VTU output supports only triangle and quadrilateral cells");
      }
      cell.points.resize(static_cast<std::size_t>(points));
      for (Vec2& point : cell.points) {
        point.x = reader.get<double>();
        point.y = reader.get<double>();
      }
      for (double& value : cell.state) value = reader.get<double>();
      cells.push_back(std::move(cell));
    }
    if (!reader.done()) throw std::runtime_error("extra bytes in gathered cell payload");
  }
  std::sort(cells.begin(), cells.end(),
            [](const CellRecord& a, const CellRecord& b) { return a.id < b.id; });
  return cells;
}

double mean(const std::vector<double>& values, std::size_t begin, std::size_t end) {
  return std::accumulate(values.begin() + static_cast<std::ptrdiff_t>(begin),
                         values.begin() + static_cast<std::ptrdiff_t>(end), 0.0) /
         static_cast<double>(end - begin);
}

double rms_about(const std::vector<double>& values, std::size_t begin, std::size_t end,
                 double center) {
  double sum = 0.0;
  for (std::size_t i = begin; i < end; ++i) {
    const double delta = values[i] - center;
    sum += delta * delta;
  }
  return std::sqrt(sum / static_cast<double>(end - begin));
}

std::vector<std::size_t> upcrossings(const std::vector<double>& values,
                                     std::size_t begin, std::size_t end,
                                     double center) {
  std::vector<std::size_t> result;
  for (std::size_t i = begin + 1U; i < end; ++i) {
    if (values[i - 1U] <= center && values[i] > center) result.push_back(i);
  }
  return result;
}

}  // namespace

std::string cli_usage() {
  return "usage: cfd_solver solve --case PATH --output DIR [--restart FILE] "
         "[--resume-output] [--report-level brief|full] [--max-steps N] "
         "[--final-time T] [--progress-every N] [--flush-every N]";
}

CliOptions parse_cli(int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) != "solve") {
    throw std::invalid_argument(cli_usage());
  }
  CliOptions result;
  std::set<std::string> seen;
  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--help" || option == "-h") throw std::invalid_argument(cli_usage());
    if (option.rfind("--", 0) != 0) {
      throw std::invalid_argument("unexpected positional argument '" + option + "'; " +
                                  cli_usage());
    }
    if (!seen.insert(option).second) throw std::invalid_argument("duplicate option " + option);
    if (option == "--resume-output") {
      result.resume_output = true;
      continue;
    }
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + option);
    const std::string value = argv[++i];
    if (value.empty()) throw std::invalid_argument("empty value for " + option);
    if (option == "--case") result.case_file = value;
    else if (option == "--output") result.output_directory = value;
    else if (option == "--restart") result.restart_file = value;
    else if (option == "--report-level") {
      if (value != "brief" && value != "full") {
        throw std::invalid_argument("--report-level must be brief or full");
      }
      result.report_level = value;
    } else if (option == "--max-steps" || option == "--progress-every" ||
               option == "--flush-every") {
      std::size_t used = 0;
      long parsed = 0;
      try { parsed = std::stol(value, &used); }
      catch (const std::exception&) { throw std::invalid_argument(option + " must be a positive integer"); }
      if (used != value.size() || parsed < 1 || parsed > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(option + " must be a positive integer");
      }
      if (option == "--max-steps") result.max_steps_override = static_cast<int>(parsed);
      else if (option == "--progress-every") result.progress_every = static_cast<int>(parsed);
      else result.flush_every = static_cast<int>(parsed);
    } else if (option == "--final-time") {
      std::size_t used = 0;
      double parsed = 0.0;
      try { parsed = std::stod(value, &used); }
      catch (const std::exception&) { throw std::invalid_argument("--final-time must be finite and positive"); }
      if (used != value.size() || !(parsed > 0.0) || !std::isfinite(parsed)) {
        throw std::invalid_argument("--final-time must be finite and positive");
      }
      result.final_time_override = parsed;
    } else {
      throw std::invalid_argument("unknown option '" + option + "'; " + cli_usage());
    }
  }
  if (result.case_file.empty()) throw std::invalid_argument("missing required --case PATH");
  if (result.output_directory.empty()) throw std::invalid_argument("missing required --output DIR");
  if (result.resume_output && !result.restart_file.has_value()) {
    throw std::invalid_argument("--resume-output requires --restart PATH");
  }
  return result;
}

std::string shell_join(int argc, char** argv) {
  std::ostringstream result;
  for (int i = 0; i < argc; ++i) {
    if (i != 0) result << ' ';
    const std::string value = argv[i];
    const bool quote = value.find_first_of(" \t\n\"'\\") != std::string::npos;
    if (!quote) result << value;
    else {
      result << '\'';
      for (char c : value) result << (c == '\'' ? "'\\''" : std::string(1, c));
      result << '\'';
    }
  }
  return result.str();
}

std::string utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream result;
  result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return result.str();
}

void InnerStatistics::observe(int iterations, bool target_met, double ratio) {
  require_finite(ratio, "inner residual ratio");
  if (iterations < 0) throw std::invalid_argument("negative inner iteration count");
  if (samples == 0U) minimum = maximum = iterations;
  else {
    minimum = std::min(minimum, iterations);
    maximum = std::max(maximum, iterations);
  }
  mean = (mean * static_cast<double>(samples) + static_cast<double>(iterations)) /
         static_cast<double>(samples + 1U);
  ++samples;
  if (!target_met) ++target_misses;
  target_converged_fraction =
      static_cast<double>(samples - target_misses) / static_cast<double>(samples);
  last_ratio = ratio;
}

PeriodicityResult test_force_periodicity(
    const std::vector<std::pair<double, double>>& forces) {
  PeriodicityResult result;
  result.samples = forces.size();
  if (forces.size() < 200U) {
    result.explanation = "requires at least 200 post-transient force samples";
    return result;
  }
  std::vector<double> cl;
  std::vector<double> cd;
  cl.reserve(forces.size());
  cd.reserve(forces.size());
  for (const auto& force : forces) {
    require_finite(force.first, "periodicity lift sample");
    require_finite(force.second, "periodicity drag sample");
    cl.push_back(force.first);
    cd.push_back(force.second);
  }
  const std::size_t split = forces.size() / 2U;
  const double cl_mean_a = mean(cl, 0U, split);
  const double cl_mean_b = mean(cl, split, cl.size());
  const double cd_mean_a = mean(cd, 0U, split);
  const double cd_mean_b = mean(cd, split, cd.size());
  const double cl_rms_a = rms_about(cl, 0U, split, cl_mean_a);
  const double cl_rms_b = rms_about(cl, split, cl.size(), cl_mean_b);
  result.mean_drag = mean(cd, 0U, cd.size());
  result.lift_rms = rms_about(cl, 0U, cl.size(), mean(cl, 0U, cl.size()));
  result.relative_drag_mean_drift =
      std::abs(cd_mean_b - cd_mean_a) / std::max(1.0e-8, std::abs(cd_mean_a));
  result.relative_lift_rms_drift =
      std::abs(cl_rms_b - cl_rms_a) / std::max(1.0e-8, cl_rms_a);
  const auto crossings_a = upcrossings(cl, 0U, split, cl_mean_a);
  const auto crossings_b = upcrossings(cl, split, cl.size(), cl_mean_b);
  result.first_half_upcrossings = static_cast<int>(crossings_a.size());
  result.second_half_upcrossings = static_cast<int>(crossings_b.size());
  std::vector<double> periods;
  for (std::size_t i = 1; i < crossings_b.size(); ++i) {
    periods.push_back(static_cast<double>(crossings_b[i] - crossings_b[i - 1U]));
  }
  if (periods.size() >= 2U) {
    const double period_mean = mean(periods, 0U, periods.size());
    result.period_coefficient_of_variation =
        rms_about(periods, 0U, periods.size(), period_mean) / period_mean;
  } else {
    result.period_coefficient_of_variation = 1.0;
  }
  result.passed = result.mean_drag > 0.0 && result.lift_rms >= 1.0e-4 &&
                  cl_rms_a >= 1.0e-4 && crossings_a.size() >= 3U &&
                  crossings_b.size() >= 3U && result.relative_drag_mean_drift <= 0.05 &&
                  result.relative_lift_rms_drift <= 0.10 &&
                  result.period_coefficient_of_variation <= 0.15;
  result.explanation =
      "last bounded force window split in half: require positive mean drag, lift RMS "
      ">=1e-4, >=3 mean "
      "upcrossings per half, <=5% drag-mean drift, <=10% lift-RMS drift, and <=15% "
      "period coefficient of variation";
  return result;
}

namespace {

void truncate_partial_line(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("required resume stream is missing: " + path.string());
  std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (data.empty()) throw std::runtime_error("required resume stream is empty: " + path.string());
  if (data.back() != '\n') {
    const std::size_t newline = data.find_last_of('\n');
    if (newline == std::string::npos) {
      throw std::runtime_error("resume stream has no complete line: " + path.string());
    }
    std::filesystem::resize_file(path, newline + 1U);
  }
}

std::pair<std::optional<std::size_t>, std::size_t> validate_resume_csv(
    const std::filesystem::path& path, const std::string& header,
    std::size_t accepted_step, std::optional<std::size_t> recorded_step) {
  truncate_partial_line(path);
  if (recorded_step.has_value() && *recorded_step > accepted_step) {
    throw std::runtime_error("checkpoint stream evidence exceeds accepted step: " + path.string());
  }
  std::ifstream input(path);
  std::string line;
  if (!std::getline(input, line) || line != header) {
    throw std::runtime_error("resume CSV header mismatch: " + path.string());
  }
  std::optional<std::size_t> previous;
  std::optional<std::uintmax_t> truncate_at;
  std::size_t rows = 0;
  while (true) {
    const std::streampos row_start = input.tellg();
    if (!std::getline(input, line)) break;
    if (line.empty()) throw std::runtime_error("empty complete row in resume CSV: " + path.string());
    const std::size_t comma = line.find(',');
    if (comma == std::string::npos) throw std::runtime_error("malformed resume CSV row: " + path.string());
    std::size_t used = 0;
    unsigned long long step = 0;
    try { step = std::stoull(line.substr(0, comma), &used); }
    catch (const std::exception&) { throw std::runtime_error("invalid step in resume CSV: " + path.string()); }
    if (used != comma || (previous.has_value() && step <= *previous)) {
      throw std::runtime_error("duplicate or non-increasing step in resume CSV: " + path.string());
    }
    if (!recorded_step.has_value() || step > *recorded_step) {
      if (row_start < std::streampos(0)) {
        throw std::runtime_error("cannot locate repair boundary in resume CSV: " + path.string());
      }
      truncate_at = static_cast<std::uintmax_t>(row_start);
      break;
    }
    if (step > accepted_step) {
      throw std::runtime_error("resume CSV step exceeds checkpoint accepted step: " + path.string());
    }
    if (static_cast<std::size_t>(std::count(line.begin(), line.end(), ',')) !=
        static_cast<std::size_t>(std::count(header.begin(), header.end(), ','))) {
      throw std::runtime_error("wrong column count in resume CSV: " + path.string());
    }
    std::istringstream fields(line);
    std::string field;
    while (std::getline(fields, field, ',')) {
      std::size_t numeric_used = 0;
      double number = 0.0;
      try { number = std::stod(field, &numeric_used); }
      catch (const std::exception&) {
        throw std::runtime_error("non-numeric value in resume CSV: " + path.string());
      }
      if (numeric_used != field.size() || !std::isfinite(number)) {
        throw std::runtime_error("invalid numeric value in resume CSV: " + path.string());
      }
    }
    previous = static_cast<std::size_t>(step);
    ++rows;
  }
  input.close();
  if (truncate_at.has_value()) std::filesystem::resize_file(path, *truncate_at);
  return {previous, rows};
}

void verify_existing_force_row(const std::filesystem::path& path, double time,
                               const ForceCoefficients& force) {
  std::ifstream input(path);
  std::string line;
  std::string last;
  while (std::getline(input, line)) if (!line.empty()) last = line;
  if (last.empty() || last == forces_csv_header) {
    throw std::runtime_error("missing existing final force row");
  }
  std::vector<double> values;
  std::istringstream fields(last);
  std::string field;
  while (std::getline(fields, field, ',')) values.push_back(std::stod(field));
  if (values.size() != 9U) throw std::runtime_error("malformed existing final force row");
  const std::array<double, 8> expected{{time, force.cl, force.cd, force.cmz,
      force.cd_pressure, force.cd_viscous, force.cl_pressure, force.cl_viscous}};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const double tolerance = 1.0e-12 * std::max({1.0, std::abs(expected[i]),
                                                 std::abs(values[i + 1U])});
    if (std::abs(expected[i] - values[i + 1U]) > tolerance) {
      throw std::runtime_error("existing final force row does not match recomputed state");
    }
  }
}

}  // namespace

RunOutput::RunOutput(const std::filesystem::path& directory, int rank, bool resume,
                     std::size_t checkpoint_step,
                     std::optional<std::size_t> checkpoint_residual_step,
                     std::optional<std::size_t> checkpoint_force_step)
    : directory_(directory), rank_(rank) {
  if (rank_ != 0) return;
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) throw std::runtime_error("cannot create output directory '" + directory_.string() +
                                      "': " + error.message());
  for (const char* stale : {"metadata.json", "partition_diagnostics.csv", "surface.csv",
                            "field_final.vtu", "restart_final.bin", "run_status.json"}) {
    std::filesystem::remove(directory_ / stale, error);
    if (error) {
      throw std::runtime_error("cannot remove stale output '" +
                               (directory_ / stale).string() + "': " + error.message());
    }
  }
  if (resume) {
    const auto residual = validate_resume_csv(directory_ / "residuals.csv",
                                              residuals_csv_header, checkpoint_step,
                                              checkpoint_residual_step);
    const auto force = validate_resume_csv(directory_ / "forces.csv", forces_csv_header,
                                           checkpoint_step, checkpoint_force_step);
    last_residual_step_ = residual.first;
    residual_rows_ = residual.second;
    last_force_step_ = force.first;
    force_rows_ = force.second;
    truncate_partial_line(directory_ / "stdout.log");
  } else {
    std::filesystem::remove(directory_ / "restart_checkpoint.bin", error);
  }
  const auto mode = resume ? (std::ios::out | std::ios::app) : std::ios::out;
  residuals_.open(directory_ / "residuals.csv", mode);
  forces_.open(directory_ / "forces.csv", mode);
  log_.open(directory_ / "stdout.log", mode);
  if (!residuals_ || !forces_ || !log_) {
    throw std::runtime_error("cannot initialize output files in '" + directory_.string() + "'");
  }
  if (!resume) {
    residuals_ << residuals_csv_header << '\n';
    forces_ << forces_csv_header << '\n';
  }
  flush();
}

void RunOutput::log(const std::string& message) {
  if (rank_ != 0) return;
  std::cout << message << '\n';
  log_ << message << '\n';
  if (!log_) throw std::runtime_error("stdout.log write failed");
}

void RunOutput::write_residual(std::size_t step, double time, int inner_iter, double cfl,
                               double dt, const ResidualNorms& residual) {
  const std::array<double, 8> finite_values{time, cfl, dt, residual.component_l2[0],
      residual.component_l2[1], residual.component_l2[2], residual.component_l2[3],
      residual.total_l2};
  for (double value : finite_values) require_finite(value, "residual CSV value");
  require_finite(residual.linf, "residual CSV linf");
  if (rank_ != 0) return;
  if (last_residual_step_.has_value() && step <= *last_residual_step_) {
    if (step == *last_residual_step_) return;
    throw std::runtime_error("residual step moved backwards");
  }
  residuals_ << std::setprecision(17) << step << ',' << time << ',' << inner_iter << ','
             << cfl << ',' << dt << ',' << residual.component_l2[0] << ','
             << residual.component_l2[1] << ',' << residual.component_l2[2] << ','
             << residual.component_l2[3] << ',' << residual.total_l2 << ','
             << residual.linf << '\n';
  if (!residuals_) throw std::runtime_error("residuals.csv write failed");
  last_residual_step_ = step;
  ++residual_rows_;
}

void RunOutput::write_force(std::size_t step, double time, const ForceCoefficients& force) {
  const std::array<double, 9> values{time, force.cl, force.cd, force.cmz,
      force.cd_pressure, force.cd_viscous, force.cl_pressure, force.cl_viscous, 0.0};
  for (double value : values) require_finite(value, "forces CSV value");
  if (rank_ != 0) return;
  if (last_force_step_.has_value() && step <= *last_force_step_) {
    if (step == *last_force_step_) {
      forces_.flush();
      if (!forces_) throw std::runtime_error("forces.csv flush before final verification failed");
      verify_existing_force_row(directory_ / "forces.csv", time, force);
      return;
    }
    throw std::runtime_error("force step moved backwards");
  }
  forces_ << std::setprecision(17) << step << ',' << time << ',' << force.cl << ','
          << force.cd << ',' << force.cmz << ',' << force.cd_pressure << ','
          << force.cd_viscous << ',' << force.cl_pressure << ',' << force.cl_viscous << '\n';
  if (!forces_) throw std::runtime_error("forces.csv write failed");
  last_force_step_ = step;
  ++force_rows_;
}

void RunOutput::flush() {
  if (rank_ == 0) {
    residuals_.flush();
    forces_.flush();
    log_.flush();
    if (!residuals_ || !forces_ || !log_) {
      throw std::runtime_error("output stream flush failed");
    }
  }
}

void write_partition_diagnostics(const std::filesystem::path& path,
                                 const DistributedMesh& mesh,
                                 MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  const std::size_t local_boundary = static_cast<std::size_t>(std::count_if(
      mesh.faces.begin(), mesh.faces.end(),
      [](const LocalFace& face) { return face.right_cell < 0; }));
  const unsigned long long local = static_cast<unsigned long long>(local_boundary);
  std::vector<unsigned long long> boundaries;
  if (rank == 0) boundaries.resize(static_cast<std::size_t>(size));
  mpi_check(MPI_Gather(&local, 1, MPI_UNSIGNED_LONG_LONG, boundaries.data(), 1,
                       MPI_UNSIGNED_LONG_LONG, 0, communicator),
            "Gather boundary counts");
  if (rank != 0) return;
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open partition diagnostics '" + path.string() + "'");
  output << partition_csv_header << '\n';
  for (const RankPartitionDiagnostics& item : mesh.global_diagnostics.ranks) {
    const auto index = static_cast<std::size_t>(item.rank);
    output << item.rank << ',' << item.owned_cells << ',' << item.ghost_cells << ','
           << boundaries.at(index) << ',' << item.neighbor_ids.size() << ','
           << list(item.neighbor_ids) << ',' << list(item.send_counts) << ','
            << list(item.receive_counts) << '\n';
  }
  output.flush();
  if (!output) throw std::runtime_error("failed writing partition diagnostics '" + path.string() + "'");
  output.close();
  if (!output) throw std::runtime_error("failed closing partition diagnostics '" + path.string() + "'");
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open executable for SHA-256: " + path.string());
  Sha256 hash;
  std::array<char, 64U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0) hash.update(buffer.data(), static_cast<std::size_t>(count));
  }
  if (!input.eof()) throw std::runtime_error("failed reading executable for SHA-256");
  return hash.finish();
}

std::filesystem::path running_executable_path(const char* argv0) {
#ifdef __linux__
  std::error_code error;
  const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error && !path.empty()) return std::filesystem::canonical(path);
#endif
  if (argv0 == nullptr || std::string(argv0).empty()) {
    throw std::runtime_error("cannot identify running executable path");
  }
  std::filesystem::path candidate(argv0);
  if (candidate.has_parent_path()) return std::filesystem::canonical(candidate);
  const char* environment = std::getenv("PATH");
  if (environment != nullptr) {
    std::istringstream paths(environment);
    std::string entry;
    while (std::getline(paths, entry, ':')) {
      const auto trial = std::filesystem::path(entry) / candidate;
      if (std::filesystem::is_regular_file(trial)) return std::filesystem::canonical(trial);
    }
  }
  throw std::runtime_error("cannot resolve running executable path from argv[0]");
}

std::string build_git_revision() { return CFD_GIT_REVISION; }
bool build_source_dirty() { return CFD_SOURCE_DIRTY; }

std::string case_fingerprint(const CaseConfig& config) {
  std::ostringstream text;
  text << std::setprecision(17) << "schema=" << config.schema_version
       << "|physics=" << to_string(config.physics.mode) << ',' << config.physics.equations
       << ',' << config.physics.reynolds.value_or(-1.0) << ','
       << config.physics.viscosity_model.value_or("none")
       << "|gas=" << config.gas.model << ',' << config.gas.gamma << ','
       << config.gas.gas_constant << ',' << config.gas.prandtl
       << "|freestream=" << config.freestream.mach << ',' << config.freestream.aoa_degrees
       << ',' << config.freestream.rho << ',' << config.freestream.velocity_magnitude << ','
       << config.freestream.pressure
       << "|reference=" << config.reference.length << ',' << config.reference.area << ','
       << config.reference.moment_center.x << ',' << config.reference.moment_center.y << ','
       << config.reference.reynolds_length << "|bc=";
  for (const auto& boundary : config.boundary_conditions) {
    text << boundary.first << ':' << to_string(boundary.second) << ';';
  }
  text << "|numerics=" << config.numerics_required.spatial_order << ','
       << config.numerics_required.inviscid_flux << ',' << config.numerics_required.viscous_flux
       << ',' << config.numerics_required.main_time_method << ','
       << config.numerics_required.transient_order.value_or(-1) << ','
       << config.numerics_required.implicit_solver
       << "|run=" << to_string(config.run_control.type) << ','
       << config.run_control.max_steps.value_or(-1) << ','
       << config.run_control.residual_reduction_target.value_or(-1.0) << ','
       << config.run_control.time_integrator.value_or("none") << ','
       << config.run_control.time_step.value_or(-1.0) << ','
       << config.run_control.final_time.value_or(-1.0) << ','
       << config.run_control.min_inner_iterations << ',' << config.run_control.max_inner_iterations
       << ',' << config.run_control.inner_residual_reduction_target << ','
       << config.run_control.inner_residual_norm.value_or("none") << ','
       << config.run_control.bdf2_history_update.value_or("none") << ','
       << config.run_control.cfl_initial << ',' << config.run_control.cfl_max << ','
       << config.run_control.pseudo_cfl_ramp_steps << ','
       << config.run_control.rusanov_dissipation_scale.value_or(1.0);
  const std::string canonical = text.str();
  return "sha256:" + sha256_bytes(canonical.data(), canonical.size());
}

std::string mesh_fingerprint(const DistributedMesh& mesh, MPI_Comm communicator) {
  std::vector<std::uint8_t> local;
  append(local, static_cast<std::uint64_t>(mesh.owned_cell_count));
  for (std::size_t i = 0; i < mesh.owned_cell_count; ++i) {
    const LocalCell& cell = mesh.cells[i];
    append(local, cell.global_id);
    append(local, cell.center);
    append(local, cell.area);
    append(local, static_cast<std::uint64_t>(cell.vertices.size()));
    for (LocalIndex vertex_index : cell.vertices) {
      const LocalVertex& vertex = mesh.vertices.at(static_cast<std::size_t>(vertex_index));
      append(local, vertex.global_id);
      append(local, vertex.position.x);
      append(local, vertex.position.y);
    }
    std::vector<GlobalId> face_ids;
    for (LocalIndex face : cell.faces) {
      face_ids.push_back(mesh.faces.at(static_cast<std::size_t>(face)).global_id);
    }
    std::sort(face_ids.begin(), face_ids.end());
    append(local, static_cast<std::uint64_t>(face_ids.size()));
    for (GlobalId face : face_ids) append(local, face);
  }
  std::uint64_t owned_faces = 0;
  for (const LocalFace& face : mesh.faces) {
    if (face.left_cell >= 0 &&
        static_cast<std::size_t>(face.left_cell) < mesh.owned_cell_count) ++owned_faces;
  }
  append(local, owned_faces);
  for (const LocalFace& face : mesh.faces) {
    if (face.left_cell < 0 ||
        static_cast<std::size_t>(face.left_cell) >= mesh.owned_cell_count) continue;
    append(local, face.global_id);
    for (LocalIndex vertex : face.vertices) {
      append(local, mesh.vertices.at(static_cast<std::size_t>(vertex)).global_id);
    }
    append(local, mesh.cells.at(static_cast<std::size_t>(face.left_cell)).global_id);
    const GlobalId right = face.right_cell < 0
        ? invalid_global_id : mesh.cells.at(static_cast<std::size_t>(face.right_cell)).global_id;
    append(local, right);
    append(local, face.center);
    append(local, face.length);
    append(local, face.normal);
    append_string(local, face.boundary);
  }
  std::vector<int> counts;
  std::vector<int> displacements;
  const std::vector<std::uint8_t> global =
      gather_bytes(local, counts, displacements, communicator);
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  std::string result;
  if (rank == 0) {
    struct CanonicalRecord {
      GlobalId id{};
      std::vector<std::uint8_t> canonical;
    };
    std::vector<CanonicalRecord> geometry;
    std::vector<CanonicalRecord> faces;
    for (std::size_t source = 0; source < counts.size(); ++source) {
      const auto begin = global.begin() + displacements[source];
      std::vector<std::uint8_t> payload(begin, begin + counts[source]);
      ByteReader reader(payload);
      const std::uint64_t cell_count = reader.get<std::uint64_t>();
      for (std::uint64_t i = 0; i < cell_count; ++i) {
        CanonicalRecord item;
        item.id = reader.get<GlobalId>();
        append(item.canonical, item.id);
        append(item.canonical, reader.get<Vec2>());
        append(item.canonical, reader.get<double>());
        const std::uint64_t vertices = reader.get<std::uint64_t>();
        append(item.canonical, vertices);
        for (std::uint64_t j = 0; j < vertices; ++j) {
          append(item.canonical, reader.get<GlobalId>());
          append(item.canonical, reader.get<double>());
          append(item.canonical, reader.get<double>());
        }
        const std::uint64_t cell_faces = reader.get<std::uint64_t>();
        append(item.canonical, cell_faces);
        for (std::uint64_t j = 0; j < cell_faces; ++j) {
          append(item.canonical, reader.get<GlobalId>());
        }
        geometry.push_back(std::move(item));
      }
      const std::uint64_t face_count = reader.get<std::uint64_t>();
      for (std::uint64_t i = 0; i < face_count; ++i) {
        CanonicalRecord item;
        item.id = reader.get<GlobalId>();
        append(item.canonical, item.id);
        append(item.canonical, reader.get<GlobalId>());
        append(item.canonical, reader.get<GlobalId>());
        append(item.canonical, reader.get<GlobalId>());
        append(item.canonical, reader.get<GlobalId>());
        append(item.canonical, reader.get<Vec2>());
        append(item.canonical, reader.get<double>());
        append(item.canonical, reader.get<Vec2>());
        append_string(item.canonical, reader.string());
        faces.push_back(std::move(item));
      }
      if (!reader.done()) throw std::runtime_error("extra bytes in mesh fingerprint payload");
    }
    std::sort(geometry.begin(), geometry.end(),
              [](const CanonicalRecord& a, const CanonicalRecord& b) { return a.id < b.id; });
    std::sort(faces.begin(), faces.end(),
              [](const CanonicalRecord& a, const CanonicalRecord& b) { return a.id < b.id; });
    if (geometry.size() != mesh.global_cell_count || faces.size() != mesh.global_face_count) {
      throw std::runtime_error("mesh fingerprint gather did not cover every global cell and face");
    }
    Sha256 hash;
    const std::array<std::uint64_t, 3> totals{
        static_cast<std::uint64_t>(mesh.global_vertex_count),
        static_cast<std::uint64_t>(mesh.global_cell_count),
        static_cast<std::uint64_t>(mesh.global_face_count)};
    hash.update(totals.data(), sizeof(totals));
    for (const CanonicalRecord& item : geometry) hash.update(item.canonical.data(), item.canonical.size());
    for (const CanonicalRecord& item : faces) hash.update(item.canonical.data(), item.canonical.size());
    result = "sha256:" + hash.finish();
  }
  broadcast_string(result, 0, communicator);
  return result;
}

void write_restart(const std::filesystem::path& path, const std::string& fingerprint,
                   const std::string& executable_sha256,
                   const CaseConfig& config, const DistributedMesh& mesh,
                   const RestartableSolution& solution,
                   const ContinuationState& continuation, MPI_Comm communicator) {
  if (solution.U.size() != mesh.cells.size() * 4U ||
      solution.U_n.size() != solution.U.size() ||
      solution.U_nm1.size() != solution.U.size() ||
      solution.U_best.size() != solution.U.size()) {
    throw std::invalid_argument("restart write state dimensions do not match local mesh");
  }
  if (executable_sha256.size() != 64U ||
      executable_sha256.find_first_not_of("0123456789abcdef") != std::string::npos) {
    throw std::invalid_argument("restart executable SHA-256 must be 64 lowercase hex characters");
  }
  const int local_cells = mpi_count(mesh.owned_cell_count, "owned restart cells");
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  std::vector<int> counts;
  if (rank == 0) counts.resize(static_cast<std::size_t>(size));
  mpi_check(MPI_Gather(&local_cells, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, communicator),
            "Gather restart counts");
  std::vector<GlobalId> local_ids(mesh.owned_cell_count);
  std::vector<double> local_states(mesh.owned_cell_count * 16U);
  for (std::size_t i = 0; i < mesh.owned_cell_count; ++i) {
    local_ids[i] = mesh.cells[i].global_id;
    for (std::size_t k = 0; k < 4U; ++k) {
      local_states[i * 16U + k] = solution.U[i * 4U + k];
      local_states[i * 16U + 4U + k] = solution.U_n[i * 4U + k];
      local_states[i * 16U + 8U + k] = solution.U_nm1[i * 4U + k];
      local_states[i * 16U + 12U + k] = solution.U_best[i * 4U + k];
    }
  }
  std::vector<int> offsets;
  std::vector<int> state_counts;
  std::vector<int> state_offsets;
  std::vector<GlobalId> ids;
  std::vector<double> states;
  if (rank == 0) {
    offsets.resize(static_cast<std::size_t>(size));
    state_counts.resize(static_cast<std::size_t>(size));
    state_offsets.resize(static_cast<std::size_t>(size));
    int total = 0;
    int state_total = 0;
    for (int i = 0; i < size; ++i) {
      offsets[static_cast<std::size_t>(i)] = total;
      state_offsets[static_cast<std::size_t>(i)] = state_total;
      total += counts[static_cast<std::size_t>(i)];
      state_counts[static_cast<std::size_t>(i)] = 16 * counts[static_cast<std::size_t>(i)];
      state_total += state_counts[static_cast<std::size_t>(i)];
    }
    ids.resize(static_cast<std::size_t>(total));
    states.resize(static_cast<std::size_t>(state_total));
  }
  mpi_check(MPI_Gatherv(local_ids.data(), local_cells, MPI_INT64_T, ids.data(), counts.data(),
                        offsets.data(), MPI_INT64_T, 0, communicator), "Gatherv restart IDs");
  mpi_check(MPI_Gatherv(local_states.data(), 16 * local_cells, MPI_DOUBLE, states.data(),
                        state_counts.data(), state_offsets.data(), MPI_DOUBLE, 0, communicator),
            "Gatherv restart states");
  if (rank != 0) return;
  std::vector<RestartRecord> records(ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    records[i].id = ids[i];
    std::copy_n(states.begin() + static_cast<std::ptrdiff_t>(i * 16U), 16U,
                records[i].state.begin());
  }
  std::sort(records.begin(), records.end(),
            [](const RestartRecord& a, const RestartRecord& b) { return a.id < b.id; });
  if (records.size() != mesh.global_cell_count) {
    throw std::runtime_error("restart gather cell count does not match global mesh");
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  std::error_code remove_error;
  std::filesystem::remove(temporary, remove_error);
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("cannot open atomic restart temporary '" + temporary.string() + "'");
  const std::array<char, 8> magic{{'C','F','D','R','S','T','1','5'}};
  const std::uint32_t version = 15U;
  const std::uint64_t fingerprint_size = static_cast<std::uint64_t>(fingerprint.size());
  const std::uint64_t case_size = static_cast<std::uint64_t>(config.case_id.size());
  const std::uint64_t executable_size =
      static_cast<std::uint64_t>(executable_sha256.size());
  const std::uint64_t count = static_cast<std::uint64_t>(records.size());
  const std::uint64_t step = static_cast<std::uint64_t>(solution.physical_step);
  const std::vector<std::uint8_t> continuation_bytes = serialize_continuation(continuation);
  const std::uint64_t continuation_size =
      static_cast<std::uint64_t>(continuation_bytes.size());
  output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  output.write(reinterpret_cast<const char*>(&version), sizeof(version));
  output.write(reinterpret_cast<const char*>(&fingerprint_size), sizeof(fingerprint_size));
  output.write(fingerprint.data(), static_cast<std::streamsize>(fingerprint.size()));
  output.write(reinterpret_cast<const char*>(&case_size), sizeof(case_size));
  output.write(config.case_id.data(), static_cast<std::streamsize>(config.case_id.size()));
  output.write(reinterpret_cast<const char*>(&executable_size), sizeof(executable_size));
  output.write(executable_sha256.data(),
               static_cast<std::streamsize>(executable_sha256.size()));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&step), sizeof(step));
  output.write(reinterpret_cast<const char*>(&solution.time), sizeof(solution.time));
  output.write(reinterpret_cast<const char*>(&continuation_size), sizeof(continuation_size));
  output.write(reinterpret_cast<const char*>(continuation_bytes.data()),
               static_cast<std::streamsize>(continuation_bytes.size()));
  for (const RestartRecord& record : records) {
    output.write(reinterpret_cast<const char*>(&record.id), sizeof(record.id));
    output.write(reinterpret_cast<const char*>(record.state.data()),
                 static_cast<std::streamsize>(sizeof(double) * record.state.size()));
  }
  output.flush();
  if (!output) throw std::runtime_error("failed writing restart output '" + temporary.string() + "'");
  output.close();
  if (!output) throw std::runtime_error("failed closing restart output '" + temporary.string() + "'");
  std::error_code rename_error;
  std::filesystem::rename(temporary, path, rename_error);
  if (rename_error) {
    throw std::runtime_error("atomic restart publish failed: " + rename_error.message());
  }
}

RestartData read_restart(const std::filesystem::path& path,
                         const std::string& fingerprint,
                         const std::string& executable_sha256,
                         const CaseConfig& config,
                         const DistributedMesh& mesh,
                         MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  if (executable_sha256.size() != 64U ||
      executable_sha256.find_first_not_of("0123456789abcdef") != std::string::npos) {
    throw std::invalid_argument("running executable SHA-256 must be 64 lowercase hex characters");
  }
  std::unordered_map<GlobalId, RestartRecord> records;
  std::uint32_t version = 0U;
  std::uint64_t step = 0;
  double time = 0.0;
  std::vector<std::uint8_t> continuation_bytes;
  std::string error;
  if (rank == 0) {
    try {
      std::ifstream input(path, std::ios::binary);
      if (!input) throw std::runtime_error("cannot open restart file '" + path.string() + "'");
      auto read_exact = [&](void* data, std::size_t bytes, const char* field) {
        input.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
        if (!input) throw std::runtime_error(std::string("truncated restart ") + field);
      };
      std::array<char, 8> magic{};
      read_exact(magic.data(), magic.size(), "magic");
      const std::array<char, 8> expected_v8{{'C','F','D','R','S','T','8','\0'}};
      const std::array<char, 8> expected_v9{{'C','F','D','R','S','T','9','\0'}};
      const std::array<char, 8> expected_v10{{'C','F','D','R','S','T','1','0'}};
      const std::array<char, 8> expected_v11{{'C','F','D','R','S','T','1','1'}};
      const std::array<char, 8> expected_v12{{'C','F','D','R','S','T','1','2'}};
      const std::array<char, 8> expected_v13{{'C','F','D','R','S','T','1','3'}};
      const std::array<char, 8> expected_v14{{'C','F','D','R','S','T','1','4'}};
      const std::array<char, 8> expected_v15{{'C','F','D','R','S','T','1','5'}};
      if (magic != expected_v8 && magic != expected_v9 &&
          magic != expected_v10 && magic != expected_v11 &&
          magic != expected_v12 && magic != expected_v13 &&
          magic != expected_v14 && magic != expected_v15) {
        throw std::runtime_error("unsupported restart magic");
      }
      read_exact(&version, sizeof(version), "version");
      if ((magic == expected_v8 && version != 8U) ||
          (magic == expected_v9 && version != 9U) ||
           (magic == expected_v10 && version != 10U) ||
           (magic == expected_v11 && version != 11U) ||
            (magic == expected_v12 && version != 12U) ||
            (magic == expected_v13 && version != 13U) ||
            (magic == expected_v14 && version != 14U) ||
            (magic == expected_v15 && version != 15U)) {
        throw std::runtime_error("unsupported restart version " +
                                 std::to_string(version));
      }
      auto read_string = [&](const char* field) {
        std::uint64_t length = 0;
        read_exact(&length, sizeof(length), field);
        if (length > 1024U * 1024U) throw std::runtime_error(std::string("restart ") + field + " is too large");
        std::string value(static_cast<std::size_t>(length), '\0');
        read_exact(value.data(), value.size(), field);
        return value;
      };
      if (read_string("fingerprint") != fingerprint) throw std::runtime_error("restart mesh fingerprint mismatch");
      if (read_string("case ID") != config.case_id) throw std::runtime_error("restart case ID mismatch");
      const std::string stored_executable = read_string("executable SHA-256");
      if (stored_executable != executable_sha256) {
        throw std::runtime_error("restart executable provenance mismatch: checkpoint=" +
                                 stored_executable + " running=" + executable_sha256);
      }
      std::uint64_t count = 0;
      read_exact(&count, sizeof(count), "cell count");
      read_exact(&step, sizeof(step), "step");
      read_exact(&time, sizeof(time), "time");
      require_finite(time, "restart time");
      std::uint64_t continuation_size = 0;
      read_exact(&continuation_size, sizeof(continuation_size), "continuation size");
      if (continuation_size > 4U * 1024U * 1024U) {
        throw std::runtime_error("restart continuation is too large");
      }
      continuation_bytes.resize(static_cast<std::size_t>(continuation_size));
      read_exact(continuation_bytes.data(), continuation_bytes.size(), "continuation");
      const ContinuationState evidence =
          deserialize_continuation(continuation_bytes, version);
      if ((evidence.last_residual_output_step.has_value() &&
           *evidence.last_residual_output_step > step) ||
          (evidence.last_force_output_step.has_value() &&
           *evidence.last_force_output_step > step) ||
          (evidence.residual_output_rows == 0U) !=
              !evidence.last_residual_output_step.has_value() ||
          (evidence.force_output_rows == 0U) !=
              !evidence.last_force_output_step.has_value() ||
          (config.run_control.type == RunType::steady &&
           evidence.solver.nonlinear_steps > step) ||
          evidence.total_attempted_steps < step ||
          (evidence.history_complete && evidence.original_start_time_utc.empty())) {
        throw std::runtime_error("restart continuation evidence is inconsistent");
      }
      if (count != mesh.global_cell_count) throw std::runtime_error("restart global cell count mismatch");
      for (std::uint64_t i = 0; i < count; ++i) {
        RestartRecord record;
        read_exact(&record.id, sizeof(record.id), "cell ID");
        read_exact(record.state.data(), sizeof(double) * record.state.size(), "cell state");
        for (double value : record.state) require_finite(value, "restart state");
        if (!records.emplace(record.id, record).second) throw std::runtime_error("duplicate global cell ID in restart");
      }
      char extra = 0;
      if (input.read(&extra, 1)) throw std::runtime_error("restart has trailing data");
    } catch (const std::exception& exception) {
      error = exception.what();
    }
  }
  broadcast_string(error, 0, communicator);
  if (!error.empty()) throw std::runtime_error(error);
  mpi_check(MPI_Bcast(&version, 1, MPI_UINT32_T, 0, communicator),
            "Bcast restart version");
  std::uint64_t continuation_size = rank == 0
      ? static_cast<std::uint64_t>(continuation_bytes.size()) : 0U;
  mpi_check(MPI_Bcast(&continuation_size, 1, MPI_UINT64_T, 0, communicator),
            "Bcast continuation size");
  if (rank != 0) continuation_bytes.resize(static_cast<std::size_t>(continuation_size));
  mpi_check(MPI_Bcast(continuation_bytes.data(), mpi_count(continuation_bytes.size(),
                        "restart continuation"), MPI_BYTE, 0, communicator),
            "Bcast continuation");

  const int local_count = mpi_count(mesh.cells.size(), "local restart cells");
  std::vector<int> counts;
  if (rank == 0) counts.resize(static_cast<std::size_t>(size));
  mpi_check(MPI_Gather(&local_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, communicator),
            "Gather local restart counts");
  std::vector<GlobalId> local_ids(mesh.cells.size());
  for (std::size_t i = 0; i < mesh.cells.size(); ++i) local_ids[i] = mesh.cells[i].global_id;
  std::vector<int> offsets;
  std::vector<GlobalId> all_ids;
  if (rank == 0) {
    offsets.resize(static_cast<std::size_t>(size));
    int total = 0;
    for (int i = 0; i < size; ++i) {
      offsets[static_cast<std::size_t>(i)] = total;
      total += counts[static_cast<std::size_t>(i)];
    }
    all_ids.resize(static_cast<std::size_t>(total));
  }
  mpi_check(MPI_Gatherv(local_ids.data(), local_count, MPI_INT64_T, all_ids.data(), counts.data(),
                        offsets.data(), MPI_INT64_T, 0, communicator),
            "Gatherv requested restart IDs");
  std::vector<int> state_counts;
  std::vector<int> state_offsets;
  std::vector<double> all_states;
  if (rank == 0) {
    state_counts.resize(static_cast<std::size_t>(size));
    state_offsets.resize(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i) {
      state_counts[static_cast<std::size_t>(i)] = 16 * counts[static_cast<std::size_t>(i)];
      state_offsets[static_cast<std::size_t>(i)] = 16 * offsets[static_cast<std::size_t>(i)];
    }
    all_states.resize(all_ids.size() * 16U);
    try {
      for (std::size_t i = 0; i < all_ids.size(); ++i) {
        const auto found = records.find(all_ids[i]);
        if (found == records.end()) {
          throw std::runtime_error("restart is missing requested global cell ID " +
                                   std::to_string(all_ids[i]));
        }
        std::copy(found->second.state.begin(), found->second.state.end(),
                   all_states.begin() + static_cast<std::ptrdiff_t>(i * 16U));
      }
    } catch (const std::exception& exception) {
      error = exception.what();
    }
  }
  broadcast_string(error, 0, communicator);
  if (!error.empty()) throw std::runtime_error(error);
  std::vector<double> local_states(mesh.cells.size() * 16U);
  mpi_check(MPI_Scatterv(all_states.data(), state_counts.data(), state_offsets.data(), MPI_DOUBLE,
                          local_states.data(), 16 * local_count, MPI_DOUBLE, 0, communicator),
            "Scatterv restart states");
  mpi_check(MPI_Bcast(&step, 1, MPI_UINT64_T, 0, communicator), "Bcast restart step");
  mpi_check(MPI_Bcast(&time, 1, MPI_DOUBLE, 0, communicator), "Bcast restart time");
  RestartData result;
  RestartableSolution& solution = result.solution;
  solution.U.resize(mesh.cells.size() * 4U);
  solution.U_n.resize(solution.U.size());
  solution.U_nm1.resize(solution.U.size());
  solution.U_best.resize(solution.U.size());
  for (std::size_t i = 0; i < mesh.cells.size(); ++i) {
    for (std::size_t k = 0; k < 4U; ++k) {
      solution.U[i * 4U + k] = local_states[i * 16U + k];
      solution.U_n[i * 4U + k] = local_states[i * 16U + 4U + k];
      solution.U_nm1[i * 4U + k] = local_states[i * 16U + 8U + k];
      solution.U_best[i * 4U + k] = local_states[i * 16U + 12U + k];
    }
  }
  solution.physical_step = static_cast<std::size_t>(step);
  solution.time = time;
  result.continuation = deserialize_continuation(continuation_bytes, version);
  return result;
}

FinalEvaluation evaluate_final_state(const DistributedMesh& mesh,
                                     const CaseConfig& config,
                                     RestartableSolution& solution, double cfl,
                                     MPI_Comm communicator) {
  ResidualOperator residual(mesh, config, communicator);
  FinalEvaluation result;
  result.residual = residual.evaluate(solution.U, cfl);
  result.forces = residual.forces(result.residual);
  return result;
}

PhysicsGateResult evaluate_physics_gates(const DistributedMesh& mesh,
                                         const CaseConfig& config,
                                         const RestartableSolution& solution,
                                         const ResidualResult& residual,
                                         const ForceCoefficients& forces,
                                         MPI_Comm communicator) {
  CaloricallyPerfectGas gas(config.gas);
  double local_min_rho = std::numeric_limits<double>::infinity();
  double local_min_pressure = std::numeric_limits<double>::infinity();
  int local_owned_valid = 1;
  for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
    const Conservative state{solution.U[cell * 4U], solution.U[cell * 4U + 1U],
                             solution.U[cell * 4U + 2U], solution.U[cell * 4U + 3U]};
    if (!gas.admissible(state)) {
      local_owned_valid = 0;
      continue;
    }
    const Primitive primitive = gas.primitive(state);
    local_min_rho = std::min(local_min_rho, primitive.rho);
    local_min_pressure = std::min(local_min_pressure, primitive.p);
  }
  double local_surface_rho = std::numeric_limits<double>::infinity();
  double local_surface_pressure = std::numeric_limits<double>::infinity();
  double local_cp_min = std::numeric_limits<double>::infinity();
  double local_cp_max = -std::numeric_limits<double>::infinity();
  double local_no_slip_speed = 0.0;
  unsigned long long local_walls = 0;
  unsigned long long local_no_slip = 0;
  int local_surface_valid = 1;
  const double q = 0.5 * config.freestream.rho * config.freestream.velocity_magnitude *
                   config.freestream.velocity_magnitude;
  for (const SurfaceBoundaryState& face : residual.surface) {
    if (face.condition == BoundaryCondition::farfield) continue;
    ++local_walls;
    if (!(face.state.rho > 0.0) || !(face.state.p > 0.0) ||
        !std::isfinite(face.state.rho) || !std::isfinite(face.state.p)) {
      local_surface_valid = 0;
    }
    local_surface_rho = std::min(local_surface_rho, face.state.rho);
    local_surface_pressure = std::min(local_surface_pressure, face.state.p);
    const double cp = (face.state.p - config.freestream.pressure) / q;
    local_cp_min = std::min(local_cp_min, cp);
    local_cp_max = std::max(local_cp_max, cp);
    if (face.condition == BoundaryCondition::no_slip_adiabatic_wall) {
      ++local_no_slip;
      local_no_slip_speed = std::max(local_no_slip_speed,
          std::sqrt(face.state.u * face.state.u + face.state.v * face.state.v));
    }
  }
  PhysicsGateResult result;
  int global_owned_valid = 0;
  int global_surface_valid = 0;
  unsigned long long global_walls = 0;
  unsigned long long global_no_slip = 0;
  MPI_Allreduce(&local_owned_valid, &global_owned_valid, 1, MPI_INT, MPI_MIN, communicator);
  MPI_Allreduce(&local_surface_valid, &global_surface_valid, 1, MPI_INT, MPI_MIN, communicator);
  MPI_Allreduce(&local_min_rho, &result.minimum_owned_rho, 1, MPI_DOUBLE, MPI_MIN, communicator);
  MPI_Allreduce(&local_min_pressure, &result.minimum_owned_pressure, 1, MPI_DOUBLE, MPI_MIN,
                communicator);
  MPI_Allreduce(&local_surface_rho, &result.minimum_surface_rho, 1, MPI_DOUBLE, MPI_MIN,
                communicator);
  MPI_Allreduce(&local_surface_pressure, &result.minimum_surface_pressure, 1, MPI_DOUBLE,
                MPI_MIN, communicator);
  MPI_Allreduce(&local_cp_min, &result.wall_cp_minimum, 1, MPI_DOUBLE, MPI_MIN, communicator);
  MPI_Allreduce(&local_cp_max, &result.wall_cp_maximum, 1, MPI_DOUBLE, MPI_MAX, communicator);
  MPI_Allreduce(&local_no_slip_speed, &result.maximum_no_slip_speed, 1, MPI_DOUBLE, MPI_MAX,
                communicator);
  MPI_Allreduce(&local_walls, &global_walls, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM, communicator);
  MPI_Allreduce(&local_no_slip, &global_no_slip, 1, MPI_UNSIGNED_LONG_LONG, MPI_SUM,
                communicator);
  result.wall_cp_range = result.wall_cp_maximum - result.wall_cp_minimum;
  result.body_drag = forces.cd;
  result.finite_positive_owned = global_owned_valid != 0 && result.minimum_owned_rho > 0.0 &&
                                 result.minimum_owned_pressure > 0.0;
  result.finite_positive_surface = global_walls > 0U && global_surface_valid != 0 &&
                                   result.minimum_surface_rho > 0.0 &&
                                   result.minimum_surface_pressure > 0.0;
  result.nontrivial_wall_cp = global_walls > 0U && std::isfinite(result.wall_cp_range) &&
                              result.wall_cp_range > 1.0e-6;
  result.positive_body_drag = global_walls == 0U || forces.cd > 1.0e-8;
  result.inviscid_viscous_zero = config.physics.mode != PhysicsMode::inviscid ||
      (std::abs(forces.cd_viscous) <= 1.0e-12 && std::abs(forces.cl_viscous) <= 1.0e-12);
  result.no_slip_speed_zero = global_no_slip == 0U || result.maximum_no_slip_speed <= 1.0e-12;
  result.passed = result.finite_positive_owned && result.finite_positive_surface &&
                  result.nontrivial_wall_cp && result.positive_body_drag &&
                  result.inviscid_viscous_zero && result.no_slip_speed_zero;
  result.explanation =
      "require finite positive owned/surface rho,p; wall Cp range>1e-6; positive body "
      "drag when walls exist; inviscid viscous coefficients<=1e-12; no-slip speed<=1e-12";
  return result;
}

void write_surface(const std::filesystem::path& path, const CaseConfig& config,
                   const DistributedMesh& mesh, const ResidualResult& residual,
                   MPI_Comm communicator) {
  std::unordered_map<GlobalId, std::string> tags;
  for (const LocalFace& face : mesh.faces) tags.emplace(face.global_id, face.boundary);
  std::vector<std::uint8_t> local;
  std::uint64_t count = 0;
  for (const SurfaceBoundaryState& face : residual.surface) {
    if (face.condition != BoundaryCondition::farfield) ++count;
  }
  append(local, count);
  for (const SurfaceBoundaryState& face : residual.surface) {
    if (face.condition == BoundaryCondition::farfield) continue;
    append(local, face.face_id);
    append(local, static_cast<int>(face.condition));
    append(local, face.state);
    append(local, face.outward_fluid_normal);
    append(local, face.tangential_shear_force);
    append(local, face.center);
    append(local, face.length);
    append_string(local, tags.at(face.face_id));
  }
  std::vector<int> counts;
  std::vector<int> displacements;
  const auto global = gather_bytes(local, counts, displacements, communicator);
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  if (rank != 0) return;
  std::map<GlobalId, SurfaceRecord> records;
  for (std::size_t source = 0; source < counts.size(); ++source) {
    const auto begin = global.begin() + displacements[source];
    std::vector<std::uint8_t> payload(begin, begin + counts[source]);
    ByteReader reader(payload);
    const auto rows = reader.get<std::uint64_t>();
    for (std::uint64_t i = 0; i < rows; ++i) {
      SurfaceRecord row;
      row.id = reader.get<GlobalId>();
      row.condition = static_cast<BoundaryCondition>(reader.get<int>());
      row.state = reader.get<Primitive>();
      row.normal = reader.get<Vec2>();
      row.shear_force = reader.get<Vec2>();
      row.center = reader.get<Vec2>();
      row.length = reader.get<double>();
      row.tag = reader.string();
      if (!records.emplace(row.id, row).second) {
        throw std::runtime_error("duplicate boundary face in gathered surface output");
      }
    }
    if (!reader.done()) throw std::runtime_error("extra bytes in surface payload");
  }
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open surface output '" + path.string() + "'");
  output << surface_csv_header << '\n' << std::setprecision(17);
  const double q = 0.5 * config.freestream.rho * config.freestream.velocity_magnitude *
                   config.freestream.velocity_magnitude;
  for (const auto& item : records) {
    const SurfaceRecord& row = item.second;
    const double cp = (row.state.p - config.freestream.pressure) / q;
    const Vec2 tangent{-row.normal.y, row.normal.x};
    const double cf = row.condition == BoundaryCondition::no_slip_adiabatic_wall
                          ? dot(row.shear_force, tangent) / (q * row.length)
                          : 0.0;
    const double mach = std::sqrt(row.state.u * row.state.u + row.state.v * row.state.v) /
                        row.state.a;
    for (double value : {row.center.x, row.center.y, row.normal.x, row.normal.y,
                         row.state.p, cp, cf, row.state.rho, row.state.u, row.state.v, mach}) {
      require_finite(value, "surface CSV value");
    }
    output << row.center.x << ',' << row.center.y << ',' << row.normal.x << ','
           << row.normal.y << ',' << row.state.p << ',' << cp << ',' << cf << ','
           << row.state.rho << ',' << row.state.u << ',' << row.state.v << ',' << mach
            << ',' << row.tag << '\n';
  }
  output.flush();
  if (!output) throw std::runtime_error("failed writing surface output '" + path.string() + "'");
  output.close();
  if (!output) throw std::runtime_error("failed closing surface output '" + path.string() + "'");
}

void write_vtu(const std::filesystem::path& path, const CaseConfig& config,
               const DistributedMesh& mesh, const RestartableSolution& solution,
               MPI_Comm communicator) {
  std::vector<int> counts;
  std::vector<int> displacements;
  const auto global = gather_bytes(serialize_cells(mesh, solution), counts, displacements,
                                   communicator);
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  if (rank != 0) return;
  const std::vector<CellRecord> cells = deserialize_cells(global, counts, displacements);
  if (cells.size() != mesh.global_cell_count) throw std::runtime_error("VTU gathered cell count mismatch");
  CaloricallyPerfectGas gas(config.gas);
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open VTU output '" + path.string() + "'");
  std::size_t points = 0;
  for (const CellRecord& cell : cells) points += cell.points.size();
  output << std::setprecision(17)
         << "<?xml version=\"1.0\"?>\n"
         << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n"
         << " <UnstructuredGrid>\n"
         << "  <FieldData><DataArray type=\"Int64\" Name=\"step\" NumberOfTuples=\"1\" format=\"ascii\">"
         << solution.physical_step << "</DataArray><DataArray type=\"Float64\" Name=\"time\" NumberOfTuples=\"1\" format=\"ascii\">"
         << solution.time << "</DataArray></FieldData>\n"
         << "  <Piece NumberOfPoints=\"" << points << "\" NumberOfCells=\"" << cells.size() << "\">\n"
         << "   <Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const CellRecord& cell : cells) for (const Vec2& point : cell.points) {
    output << point.x << ' ' << point.y << " 0\n";
  }
  output << "   </DataArray></Points>\n   <Cells>\n"
         << "    <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">";
  std::size_t point_index = 0;
  for (const CellRecord& cell : cells) {
    for (std::size_t i = 0; i < cell.points.size(); ++i) output << point_index++ << ' ';
  }
  output << "</DataArray>\n    <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">";
  std::size_t offset = 0;
  for (const CellRecord& cell : cells) { offset += cell.points.size(); output << offset << ' '; }
  output << "</DataArray>\n    <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">";
  for (const CellRecord& cell : cells) output << (cell.points.size() == 3U ? 5 : 9) << ' ';
  output << "</DataArray>\n   </Cells>\n   <CellData>\n";
  std::array<std::vector<double>, 7> arrays;
  std::vector<int> owners;
  for (const CellRecord& cell : cells) {
    const Primitive primitive = gas.primitive(cell.state);
    const double mach = std::sqrt(primitive.u * primitive.u + primitive.v * primitive.v) / primitive.a;
    const std::array<double, 7> values{primitive.rho, primitive.u, primitive.v, primitive.p,
                                       mach, cell.state[3], primitive.T};
    for (std::size_t i = 0; i < arrays.size(); ++i) arrays[i].push_back(values[i]);
    owners.push_back(cell.owner);
  }
  const std::array<const char*, 7> names{{"rho", "u", "v", "pressure", "mach", "rhoE", "temperature"}};
  for (std::size_t i = 0; i < arrays.size(); ++i) {
    output << "    <DataArray type=\"Float64\" Name=\"" << names[i] << "\" format=\"ascii\">";
    for (double value : arrays[i]) { require_finite(value, "VTU cell value"); output << value << ' '; }
    output << "</DataArray>\n";
  }
  output << "    <DataArray type=\"Int32\" Name=\"owner_rank\" format=\"ascii\">";
  for (int owner : owners) output << owner << ' ';
  output << "</DataArray>\n   </CellData>\n  </Piece>\n </UnstructuredGrid>\n</VTKFile>\n";
  if (!output) throw std::runtime_error("failed writing VTU output '" + path.string() + "'");
}

void write_metadata_and_status(const std::filesystem::path& directory,
                               const CaseConfig& config,
                               const DistributedMesh& mesh,
                               const RunReport& report) {
  if (mesh.rank != 0) return;
  Json overrides = Json::object();
  for (const auto& item : report.diagnostic_overrides) overrides[item.first] = item.second;
  std::size_t min_owned = mesh.global_cell_count;
  std::size_t max_owned = 0;
  for (const auto& item : mesh.global_diagnostics.ranks) {
    min_owned = std::min(min_owned, item.owned_cells);
    max_owned = std::max(max_owned, item.owned_cells);
  }
  const double mean_owned = static_cast<double>(mesh.global_cell_count) / static_cast<double>(mesh.size);
  const Json periodicity{{"passed", report.periodicity.passed},
                         {"samples", report.periodicity.samples},
                         {"first_half_upcrossings", report.periodicity.first_half_upcrossings},
                         {"second_half_upcrossings", report.periodicity.second_half_upcrossings},
                         {"relative_drag_mean_drift", report.periodicity.relative_drag_mean_drift},
                         {"relative_lift_rms_drift", report.periodicity.relative_lift_rms_drift},
                         {"period_coefficient_of_variation", report.periodicity.period_coefficient_of_variation},
                         {"mean_drag", report.periodicity.mean_drag},
                         {"lift_rms", report.periodicity.lift_rms},
                         {"test", report.periodicity.explanation}};
  const Json physics_gates{
      {"passed", report.physics_gates.passed},
      {"finite_positive_owned", report.physics_gates.finite_positive_owned},
      {"finite_positive_surface", report.physics_gates.finite_positive_surface},
      {"nontrivial_wall_cp", report.physics_gates.nontrivial_wall_cp},
      {"positive_body_drag", report.physics_gates.positive_body_drag},
      {"inviscid_viscous_zero", report.physics_gates.inviscid_viscous_zero},
      {"no_slip_speed_zero", report.physics_gates.no_slip_speed_zero},
      {"minimum_owned_rho", report.physics_gates.minimum_owned_rho},
      {"minimum_owned_pressure", report.physics_gates.minimum_owned_pressure},
      {"minimum_surface_rho", report.physics_gates.minimum_surface_rho},
      {"minimum_surface_pressure", report.physics_gates.minimum_surface_pressure},
      {"wall_cp_minimum", report.physics_gates.wall_cp_minimum},
      {"wall_cp_maximum", report.physics_gates.wall_cp_maximum},
      {"wall_cp_range", report.physics_gates.wall_cp_range},
      {"body_drag", report.physics_gates.body_drag},
      {"maximum_no_slip_speed", report.physics_gates.maximum_no_slip_speed},
      {"test", report.physics_gates.explanation}};
  Json metadata{
      {"case_id", config.case_id}, {"solver_name", "cfd_solver"}, {"solver_version", "3.1"},
      {"git_revision", report.git_revision}, {"source_dirty", report.source_dirty},
      {"executable_sha256", report.executable_sha256},
      {"mesh_fingerprint", report.mesh_fingerprint},
      {"case_config_fingerprint", report.case_fingerprint},
      {"mpi_ranks", mesh.size}, {"mesh_file", config.mesh.file.string()},
      {"num_cells_global", mesh.global_cell_count}, {"num_faces_global", mesh.global_face_count},
      {"num_cells_owned_local", mesh.owned_cell_count},
      {"num_cells_ghost_local", mesh.cells.size() - mesh.owned_cell_count},
      {"partitioner", "metis_kway"}, {"partition_edge_cut", mesh.global_diagnostics.edge_cut},
      {"halo_exchange", "neighbor_isend_irecv"},
      {"full_state_replication_during_iterations", false},
      {"full_mesh_replication_during_iterations", false},
      {"equation_set", "compressible_navier_stokes_2d"},
      {"inviscid_flux", config.numerics_required.inviscid_flux},
      {"entropy_fix", nullptr}, {"viscous_flux", config.numerics_required.viscous_flux},
      {"time_integrator", config.run_control.type == RunType::steady
                               ? config.numerics_required.main_time_method
                               : config.run_control.time_integrator.value_or("BDF2")},
      {"steady_initial_residual_scale",
       report.steady_spatial_order.original_initial_residual},
      {"steady_full_order_initial_residual",
       report.steady_spatial_order.full_order_initial_residual},
      {"residual_reduction_target",
       config.run_control.residual_reduction_target.value_or(-1.0)},
      {"residual_reduction_orders", report.residual_reduction_orders},
      {"full_order_residual_reduction_orders",
       report.full_order_residual_reduction_orders},
      {"steady_convergence_gate",
       {{"residual_baseline", "original_run_global_initial_residual"},
        {"residual_condition",
         "current_global_l2 <= original_run_global_initial_residual * 10^(-residual_reduction_target)"},
        {"full_order_condition", "reconstruction_blend == 1"},
        {"full_order_hold_formula",
         "max(50,min(250,pseudo_cfl_ramp_steps/10)) accepted full-order implicit updates"},
        {"required_full_order_accepted_steps",
         report.steady_spatial_order.minimum_full_order_steps},
        {"rejected_attempts_count_toward_hold", false},
        {"physics_and_positivity_gates_required", true},
        {"full_order_initial_residual_role", "diagnostic_only"}}},
      {"implicit_solver", config.run_control.type == RunType::steady
                               ? steady_linear_solver_name
                               : transient_linear_solver_name},
       {"steady_acceptance",
       {{"scope", "cumulative_run_including_resume"},
          {"primary_method", steady_linear_solver_name},
          {"preconditioner", "frozen_first_order_full_block_rusanov_lu_sgs"},
          {"preconditioner_face_blocks", "complete_conservative_4x4"},
          {"preconditioner_ordering", "ascending_global_cell_id"},
          {"preconditioner_cross_rank_coupling", "block_lagged_halo_exchange_each_half_sweep"},
          {"preconditioner_minimum_symmetric_sweeps", 3},
          {"preconditioner_defect", "mpi_global_true_frozen_linear_defect"},
         {"rescue_method", steady_rescue_solver_name},
          {"rescue_reconstruction_blend", "active_spatial_continuation_blend"},
          {"rescue_can_promote_spatial_order", false},
         {"rescue_gmres_relative_tolerance", 1.0e-2},
         {"rescue_gmres_restart", 50},
         {"rescue_gmres_maximum_iterations", 50},
         {"rescue_maximum_cfl", config.run_control.cfl_max},
         {"rescue_acceptance", "strict_actual_residual_decrease"},
        {"fallback_method", steady_fallback_solver_name},
        {"fallback_maximum_cfl", 0.1},
         {"fallback_acceptance",
          "positive_strict_actual_residual_decrease"},
         {"fallback_minimum_cfl", steady_fallback_minimum_cfl},
          {"jfnk_epsilon_policy",
           "clamp(sqrt(current_global_l2/original_run_global_initial_residual),1e-3,1)"},
         {"jfnk_epsilon_reference_policy",
           "persisted_original_run_global_initial_residual"},
         {"jfnk_epsilon_minimum_multiplier",
          steady_jfnk_minimum_epsilon_multiplier},
         {"jfnk_epsilon_reference_residual",
          report.steady_acceptance.jfnk_epsilon_reference_residual},
         {"jfnk_epsilon_multiplier",
          report.steady_acceptance.jfnk_epsilon_multiplier},
         {"jfnk_last_absolute_epsilon",
          report.steady_acceptance.jfnk_last_epsilon},
         {"jfnk_last_epsilon_halvings",
          report.steady_acceptance.jfnk_last_epsilon_halvings},
           {"nonmonotone_scope",
            "steady_full_order_implicit_after_strict_globalization_stagnation_independent_of_shock_fallback"},
          {"nonmonotone_window_capacity",
           steady_nonmonotone_window_capacity},
          {"nonmonotone_reference",
           "fixed_activation_seed_bounded_by_best_times_1.0001"},
          {"nonmonotone_envelope_seed_policy",
           "once_on_activation_and_after_meaningful_strict_best_only"},
          {"nonmonotone_armijo_reference",
           "bounded_activation_reference_merit_with_predicted_reduction"},
          {"nonmonotone_best_residual_cap",
           steady_nonmonotone_best_residual_cap},
          {"nonmonotone_stagnation_attempts",
           steady_nonmonotone_stagnation_attempts},
           {"nonmonotone_watchdog_steps",
            steady_nonmonotone_watchdog_steps},
           {"nonmonotone_meaningful_best_relative_decrease",
            steady_meaningful_best_relative_decrease},
           {"nonmonotone_meaningful_best_roundoff_multiplier", 256.0},
          {"nonmonotone_window_samples",
           report.steady_acceptance.nonmonotone_window_samples},
          {"strict_decrease_stagnation_streak",
           report.steady_acceptance.strict_decrease_stagnation_streak},
          {"nonmonotone_bridge_active",
           report.steady_acceptance.nonmonotone_bridge_active},
          {"nonmonotone_bridge_disabled",
           report.steady_acceptance.nonmonotone_bridge_disabled},
          {"nonmonotone_steps_since_strict_best",
           report.steady_acceptance.nonmonotone_steps_since_strict_best},
          {"nonmonotone_accepted_steps",
           report.steady_acceptance.nonmonotone_accepted_steps},
          {"nonmonotone_max_relative_increase",
           report.steady_acceptance.nonmonotone_max_relative_increase},
          {"nonmonotone_strict_best_improvements",
           report.steady_acceptance.nonmonotone_strict_best_improvements},
           {"nonmonotone_watchdog_resets",
            report.steady_acceptance.nonmonotone_watchdog_resets},
           {"nonmonotone_descent_bypass_attempts",
            report.steady_acceptance.nonmonotone_bypass_attempts},
           {"nonmonotone_descent_bypass_accepted_steps",
            report.steady_acceptance.nonmonotone_bypass_accepted_steps},
           {"nonmonotone_descent_bypass_trial_evaluations",
            report.steady_acceptance.nonmonotone_bypass_trial_evaluations},
           {"nonmonotone_descent_bypass_last_actual_trial_residual",
            report.steady_acceptance
                .nonmonotone_bypass_last_actual_trial_residual},
           {"nonmonotone_descent_bypass_last_gmres_ratio",
            report.steady_acceptance.nonmonotone_bypass_last_gmres_ratio},
           {"nonmonotone_activation_envelope_fraction",
            steady_nonmonotone_activation_envelope_fraction},
           {"nonmonotone_envelope_reference",
            report.steady_acceptance.nonmonotone_envelope_reference},
           {"nonmonotone_envelope_accepted_steps",
            report.steady_acceptance.nonmonotone_envelope_accepted_steps},
           {"nonmonotone_envelope_max_relative_increase",
            report.steady_acceptance
                .nonmonotone_envelope_max_relative_increase},
           {"nonmonotone_envelope_production_enabled", false},
           {"implicit_pseudo_transient_bridge_method",
            steady_implicit_bridge_solver_name},
           {"implicit_pseudo_transient_bridge_operator",
            "sigma_over_cfl_identity_plus_frozen_full_block_rusanov_jacobian"},
           {"implicit_pseudo_transient_bridge_scope",
            "every_fixed_spatial_order_phase_after_strict_globalization_stagnation"},
           {"implicit_pseudo_transient_bridge_acceptance",
            "finite_positive_largest_safe_scale_with_entry_best_cap_no_per_step_decrease"},
           {"implicit_pseudo_transient_bridge_entry_best_cap",
            steady_implicit_bridge_residual_cap},
           {"implicit_pseudo_transient_bridge_growth_contraction_threshold",
            steady_implicit_bridge_growth_contraction_threshold},
           {"implicit_pseudo_transient_bridge_watchdog_steps",
            steady_implicit_bridge_watchdog_steps},
           {"implicit_pseudo_transient_bridge_maximum_steps",
            steady_implicit_bridge_maximum_steps},
           {"implicit_pseudo_transient_bridge_active",
            report.steady_acceptance.implicit_bridge.active},
           {"implicit_pseudo_transient_bridge_disabled",
            report.steady_acceptance.implicit_bridge.disabled},
           {"implicit_pseudo_transient_bridge_cfl",
            report.steady_acceptance.implicit_bridge.cfl},
           {"implicit_pseudo_transient_bridge_entry_best_residual",
            report.steady_acceptance.implicit_bridge.entry_best_residual},
           {"implicit_pseudo_transient_bridge_attempts",
            report.steady_acceptance.implicit_bridge.attempts},
           {"implicit_pseudo_transient_bridge_accepted_steps",
            report.steady_acceptance.implicit_bridge.accepted_steps},
           {"implicit_pseudo_transient_bridge_rejected_steps",
            report.steady_acceptance.implicit_bridge.rejected_steps},
           {"implicit_pseudo_transient_bridge_steps_since_best",
            report.steady_acceptance.implicit_bridge
                .accepted_steps_since_best},
           {"implicit_pseudo_transient_bridge_residual_minimum",
            report.steady_acceptance.implicit_bridge.residual_minimum},
           {"implicit_pseudo_transient_bridge_residual_maximum",
            report.steady_acceptance.implicit_bridge.residual_maximum},
           {"implicit_pseudo_transient_bridge_maximum_relative_growth",
            report.steady_acceptance.implicit_bridge
                .maximum_relative_growth},
           {"implicit_pseudo_transient_bridge_meaningful_best_improvements",
            report.steady_acceptance.implicit_bridge
                .meaningful_best_improvements},
           {"implicit_pseudo_transient_bridge_watchdog_stops",
            report.steady_acceptance.implicit_bridge.watchdog_stops},
           {"implicit_pseudo_transient_bridge_linear_sweeps",
            report.steady_acceptance.implicit_bridge.linear_sweeps},
          {"best_state_role", "diagnostic_only_never_restored_or_finalized"},
         {"jfnk_attempts", report.steady_acceptance.jfnk_attempts},
         {"jfnk_accepted_steps",
          report.steady_acceptance.jfnk_accepted_steps},
         {"rescue_attempts", report.steady_acceptance.rescue_attempts},
         {"rescue_accepted_steps",
          report.steady_acceptance.rescue_accepted_steps},
         {"rescue_total_gmres_iterations",
          report.steady_acceptance.rescue_total_gmres_iterations},
         {"rescue_last_gmres_iterations",
          report.steady_acceptance.rescue_last_gmres_iterations},
         {"rescue_max_gmres_iterations",
          report.steady_acceptance.rescue_max_gmres_iterations},
         {"rescue_last_gmres_ratio",
          report.steady_acceptance.rescue_last_gmres_ratio},
         {"rescue_last_cfl", report.steady_acceptance.rescue_last_cfl},
         {"rescue_last_line_scale",
          report.steady_acceptance.rescue_last_line_scale},
         {"rescue_cooldown_attempts",
          report.steady_acceptance.rescue_cooldown_attempts},
          {"rescue_retry_interval", steady_rescue_retry_interval},
          {"trust_region_retry_method", steady_trust_region_retry_solver_name},
          {"trust_region_retry_scope", "all_steady_spatial_order_phases_after_primary_jfnk_failure"},
          {"trust_region_retry_cfl_schedule",
           "decades_strictly_between_current_cfl_and_cfl_max"},
          {"trust_region_retry_selection",
           "minimum_finite_actual_residual_with_strict_decrease"},
          {"trust_region_retry_gmres_relative_tolerance", 1.0e-2},
          {"trust_region_retry_gmres_restart", 50},
          {"trust_region_retry_gmres_maximum_iterations", 50},
          {"trust_region_retry_cooldown_interval",
           steady_trust_region_retry_interval},
          {"trust_region_retry_batches",
           report.steady_acceptance.trust_region_retry_batches},
          {"trust_region_retry_candidates",
           report.steady_acceptance.trust_region_retry_candidates},
          {"trust_region_retry_accepted_steps",
           report.steady_acceptance.trust_region_retry_accepted_steps},
          {"trust_region_retry_total_gmres_iterations",
           report.steady_acceptance
               .trust_region_retry_total_gmres_iterations},
          {"trust_region_retry_last_candidate_count",
           report.steady_acceptance
               .trust_region_retry_last_candidate_count},
          {"trust_region_retry_last_total_gmres_iterations",
           report.steady_acceptance
               .trust_region_retry_last_total_gmres_iterations},
          {"trust_region_retry_last_accepted_gmres_iterations",
           report.steady_acceptance
               .trust_region_retry_last_accepted_gmres_iterations},
          {"trust_region_retry_last_accepted_cfl",
           report.steady_acceptance
               .trust_region_retry_last_accepted_cfl},
          {"trust_region_retry_last_line_scale",
           report.steady_acceptance.trust_region_retry_last_line_scale},
          {"trust_region_retry_persisted_candidate",
           "most_recent_successful_candidate"},
          {"trust_region_retry_last_initial_residual",
           report.steady_acceptance
               .trust_region_retry_last_initial_residual},
          {"trust_region_retry_last_final_residual",
           report.steady_acceptance
               .trust_region_retry_last_final_residual},
          {"trust_region_retry_last_accepted_initial_residual",
           report.steady_acceptance
               .trust_region_retry_last_initial_residual},
          {"trust_region_retry_last_accepted_final_residual",
           report.steady_acceptance
               .trust_region_retry_last_final_residual},
          {"trust_region_retry_cooldown_attempts",
           report.steady_acceptance
               .trust_region_retry_cooldown_attempts},
         {"fallback_accepted_steps",
         report.steady_acceptance.fallback_accepted_steps},
        {"fallback_attempts", report.steady_acceptance.fallback_attempts},
        {"fallback_rejected_steps",
         report.steady_acceptance.fallback_rejected_steps},
        {"fallback_cfl_halvings",
         report.steady_acceptance.fallback_cfl_halvings},
        {"final_fallback_cfl",
         report.steady_acceptance.last_fallback_cfl},
         {"fallback_mode_active", report.steady_acceptance.fallback_mode},
         {"fallback_disabled", report.steady_acceptance.fallback_disabled},
          {"fallback_growth_disables",
           report.steady_acceptance.fallback_growth_disables},
          {"lusgs_preconditioner_applications",
           report.steady_acceptance.lusgs_preconditioner_applications},
          {"lusgs_preconditioner_sweeps",
           report.steady_acceptance.lusgs_preconditioner_sweeps},
          {"lusgs_last_defect_ratio",
           report.steady_acceptance.lusgs_last_defect_ratio},
         {"fallback_maximum_consecutive_bridge_steps",
          steady_fallback_maximum_bridge_steps},
         {"fallback_allowed_at_full_order", true},
        {"operating_fallback_cfl",
         report.steady_acceptance.operating_fallback_cfl},
        {"fallback_window_samples",
         report.steady_acceptance.fallback_window_samples},
         {"fallback_steps_since_jfnk",
          report.steady_acceptance.fallback_steps_since_jfnk}}},
       {"spatial_order_continuation",
        {{"enabled", config.run_control.type == RunType::steady},
         {"schedule_source", "pseudo_cfl_ramp_steps"},
         {"first_order_target_steps",
          report.steady_spatial_order.first_order_target_steps},
         {"ramp_target_steps", report.steady_spatial_order.ramp_target_steps},
         {"first_order_accepted_steps",
          report.steady_spatial_order.first_order_accepted_steps},
         {"ramp_accepted_steps",
          report.steady_spatial_order.ramp_accepted_steps},
         {"full_order_accepted_steps",
          report.steady_spatial_order.full_order_accepted_steps},
         {"minimum_full_order_steps",
          report.steady_spatial_order.minimum_full_order_steps},
          {"final_reconstruction_blend",
           report.steady_spatial_order.final_blend},
          {"original_initial_residual",
           report.steady_spatial_order.original_initial_residual},
          {"original_initial_residual_role",
           "convergence_and_OUTPUT_CONTRACT_reporting_baseline"},
          {"full_order_initial_residual",
           report.steady_spatial_order.full_order_initial_residual},
          {"full_order_initial_residual_role", "diagnostic_only"},
         {"full_order_best_residual",
          report.steady_spatial_order.full_order_best_residual},
         {"full_order_reduction_available",
          report.steady_spatial_order.full_order_initial_residual >= 0.0 &&
              report.steady_spatial_order.full_order_accepted_steps > 0U},
         {"full_order_residual_descended",
          report.steady_spatial_order.full_order_initial_residual >= 0.0 &&
              report.steady_spatial_order.full_order_best_residual >= 0.0 &&
              report.steady_spatial_order.full_order_best_residual <
                  report.steady_spatial_order.full_order_initial_residual},
          {"promoted_by_newton_rescue",
           report.steady_spatial_order.promoted_by_newton_rescue},
          {"accepted_step_counts_required_before_full_order", true},
          {"completion_requires_sustained_full_order", true},
          {"rejected_attempts_count_toward_full_order_hold", false},
         {"final_outputs_use_full_order_reconstruction",
          report.steady_spatial_order.final_outputs_full_order}}},
      {"reconstruction", "cell_centered_weighted_least_squares"},
      {"limiter", "Venkatakrishnan with Barth-Jespersen positivity/shock fallback"},
      {"limiter_diagnostics",
       {{"venkatakrishnan_limited_face_components",
         report.final_reconstruction_diagnostics.venkatakrishnan_limited_face_components},
        {"shock_fallback_cells",
         report.final_reconstruction_diagnostics.shock_fallback_cells},
        {"positivity_barth_fallbacks",
         report.final_reconstruction_diagnostics.positivity_barth_fallbacks},
        {"positivity_scaled_faces",
         report.final_reconstruction_diagnostics.positivity_scaled},
        {"first_order_fallback_faces",
         report.final_reconstruction_diagnostics.first_order_fallbacks}}},
      {"spatial_order_claimed", 2},
      {"positivity_preservation",
       "Barth-Jespersen shock/positivity fallback, face scaling, and admissibility line search"},
      {"wall_boundary_output_semantics", "boundary_value"},
      {"true_bdf2_inner_loop", config.run_control.type == RunType::transient},
      {"typical_inner_iterations", report.inner.mean},
      {"min_inner_iterations", config.run_control.min_inner_iterations},
      {"max_inner_iterations", config.run_control.max_inner_iterations},
      {"observed_min_inner_iterations", report.inner.minimum},
      {"observed_max_inner_iterations", report.inner.maximum},
      {"inner_residual_reduction_target", config.run_control.inner_residual_reduction_target},
      {"inner_target_misses", report.inner.target_misses},
      {"inner_target_converged_fraction", report.inner.target_converged_fraction},
      {"last_inner_residual_ratio", report.inner.last_ratio},
      {"start_time_utc", report.start_time_utc}, {"end_time_utc", report.end_time_utc},
      {"completed", report.completed}, {"convergence_status", report.convergence_status},
      {"command", report.command}, {"wall_time_seconds", report.wall_time_seconds},
      {"diagnostic_overrides", overrides}, {"periodicity_test", periodicity},
      {"physics_gates", physics_gates}, {"resumed_output", report.resumed_output},
      {"history_complete", report.history_complete},
      {"global_partition_summary", {{"total_ghost_cells", mesh.global_diagnostics.total_ghost_cells},
          {"min_owned_cells", min_owned}, {"max_owned_cells", max_owned},
          {"mean_owned_cells", mean_owned},
          {"load_balance_ratio", mean_owned == 0.0 ? 0.0 : static_cast<double>(max_owned) / mean_owned}}}};
  Json status{{"case_id", config.case_id}, {"command", report.command},
              {"mpi_ranks", mesh.size}, {"wall_time_seconds", report.wall_time_seconds},
              {"final_step", report.final_step}, {"final_physical_time", report.final_physical_time},
               {"convergence_status", report.convergence_status},
               {"steady_initial_residual_scale",
                report.steady_spatial_order.original_initial_residual},
               {"steady_full_order_initial_residual",
                report.steady_spatial_order.full_order_initial_residual},
               {"residual_reduction_orders", report.residual_reduction_orders},
               {"full_order_residual_reduction_orders",
                report.full_order_residual_reduction_orders},
              {"notes", report.notes}};
  write_json(directory / "metadata.json", metadata);
  write_json(directory / "run_status.json", status);
}

}  // namespace cfd
