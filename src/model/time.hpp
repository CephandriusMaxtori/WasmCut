#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace wasmcut::model {

using TimeUs = std::int64_t;

inline constexpr TimeUs microseconds_per_second = 1'000'000;
inline constexpr TimeUs max_time_us = std::numeric_limits<TimeUs>::max();

[[nodiscard]] inline TimeUs seconds_to_time(double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return 0;
  }
  const double maximum_seconds = static_cast<double>(max_time_us) / static_cast<double>(microseconds_per_second);
  if (seconds >= maximum_seconds) {
    return max_time_us;
  }
  return static_cast<TimeUs>(seconds * static_cast<double>(microseconds_per_second));
}

[[nodiscard]] inline double time_to_seconds(TimeUs time) noexcept {
  return static_cast<double>(time) / static_cast<double>(microseconds_per_second);
}

[[nodiscard]] inline TimeUs add_time_saturated(TimeUs left, TimeUs right) noexcept {
  if (left < 0 || right < 0) {
    return 0;
  }
  if (left > max_time_us - right) {
    return max_time_us;
  }
  return left + right;
}

[[nodiscard]] inline TimeUs clamp_time(TimeUs value) noexcept {
  if (value < 0) {
    return 0;
  }
  return value;
}

}
