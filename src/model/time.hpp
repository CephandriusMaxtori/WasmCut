#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

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

// Rounds a time value down to the nearest frame boundary for the given rate.
[[nodiscard]] inline TimeUs floor_to_frame(TimeUs time, double frame_rate) noexcept {
  if (time <= 0) {
    return 0;
  }
  if (!std::isfinite(frame_rate) || frame_rate <= 0.0) {
    return time;
  }
  const double frame_seconds = 1.0 / frame_rate;
  const double seconds = time_to_seconds(time);
  return seconds_to_time(std::floor(seconds / frame_seconds + 1e-6) * frame_seconds);
}

[[nodiscard]] inline TimeUs frame_duration(double frame_rate) noexcept {
  if (!std::isfinite(frame_rate) || frame_rate <= 0.0) {
    return 0;
  }
  const double frame_seconds = 1.0 / frame_rate;
  return seconds_to_time(frame_seconds);
}

// Renders SMPTE-style HH:MM:SS:FF for the frame rates an editor realistically
// uses, and HH:MM:SS.mmm for anything exotic.
inline constexpr double timecode_frame_rate_threshold = 24.0;

// Renders SMPTE-style HH:MM:SS:FF (or HH:MM:SS.mmm at low frame rates).
[[nodiscard]] inline std::string format_timecode(TimeUs time, double frame_rate) {
  const TimeUs clamped = clamp_time(time);
  const double seconds_total = time_to_seconds(clamped);
  const auto total_seconds = static_cast<long long>(seconds_total);
  const auto hours = total_seconds / 3600;
  const auto minutes = (total_seconds / 60) % 60;
  const auto seconds = total_seconds % 60;

  std::string result;
  result.reserve(11);
  if (hours < 10) {
    result += '0';
  }
  result += std::to_string(hours);
  result += ':';
  if (minutes < 10) {
    result += '0';
  }
  result += std::to_string(minutes);
  result += ':';
  if (seconds < 10) {
    result += '0';
  }
  result += std::to_string(seconds);

  if (std::isfinite(frame_rate) && frame_rate >= timecode_frame_rate_threshold) {
    const double frames = seconds_total * frame_rate;
    auto whole_frames = static_cast<long long>(frames) - total_seconds * static_cast<long long>(frame_rate);
    if (whole_frames < 0) {
      whole_frames = 0;
    }
    result += ':';
    if (whole_frames < 10) {
      result += '0';
    }
    result += std::to_string(whole_frames);
  } else {
    auto millis = static_cast<long long>((seconds_total - static_cast<double>(total_seconds)) * 1000.0);
    if (millis < 0) {
      millis = 0;
    }
    if (millis > 999) {
      millis = 999;
    }
    result += '.';
    if (millis < 100) {
      result += '0';
    }
    if (millis < 10) {
      result += '0';
    }
    result += std::to_string(millis);
  }
  return result;
}

// Human readable byte counts such as "12.4 MB".
[[nodiscard]] inline std::string format_bytes(std::uint64_t bytes) {
  static const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  std::string result;
  if (unit == 0) {
    result = std::to_string(static_cast<long long>(value));
  } else {
    // One decimal place, assembled by hand so the output never picks up the
    // six-decimal default of std::to_string(double).
    const auto tenths = static_cast<long long>(value * 10.0);
    result = std::to_string(tenths / 10);
    result += '.';
    result += static_cast<char>('0' + static_cast<int>(tenths % 10));
  }
  result += ' ';
  result += units[unit];
  return result;
}

}
