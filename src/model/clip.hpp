#pragma once

#include "model/time.hpp"

#include <cmath>
#include <string>

namespace wasmcut::model {

struct Clip {
  std::string id;
  std::string media_id;
  std::string name;
  TimeUs timeline_start = 0;
  TimeUs source_in = 0;
  TimeUs source_out = 0;
  double speed = 1.0;
  float opacity = 1.0f;

  [[nodiscard]] TimeUs source_duration() const noexcept {
    if (source_out <= source_in) {
      return 0;
    }
    return source_out - source_in;
  }

  [[nodiscard]] TimeUs timeline_duration() const noexcept {
    const TimeUs source_duration_value = source_duration();
    if (source_duration_value == 0 || !std::isfinite(speed) || speed <= 0.0) {
      return 0;
    }
    const long double scaled = static_cast<long double>(source_duration_value) / static_cast<long double>(speed);
    if (scaled >= static_cast<long double>(max_time_us)) {
      return max_time_us;
    }
    return static_cast<TimeUs>(scaled);
  }

  [[nodiscard]] TimeUs timeline_end() const noexcept {
    return add_time_saturated(timeline_start, timeline_duration());
  }

  [[nodiscard]] bool contains_time(TimeUs timeline_time) const noexcept {
    return timeline_time >= timeline_start && timeline_time < timeline_end();
  }

  [[nodiscard]] TimeUs source_time_at(TimeUs timeline_time) const noexcept {
    if (!contains_time(timeline_time)) {
      return 0;
    }
    const TimeUs elapsed = timeline_time - timeline_start;
    const long double scaled = static_cast<long double>(elapsed) * static_cast<long double>(speed);
    if (scaled >= static_cast<long double>(source_duration())) {
      return source_out;
    }
    return static_cast<TimeUs>(scaled) + source_in;
  }

  [[nodiscard]] bool is_valid() const noexcept {
    return !id.empty() && !media_id.empty() && timeline_start >= 0 && source_in >= 0 && source_out > source_in &&
           std::isfinite(speed) && speed > 0.0 && std::isfinite(opacity) && opacity >= 0.0f && opacity <= 1.0f;
  }

  bool set_source_range(TimeUs new_source_in, TimeUs new_source_out) noexcept {
    if (new_source_in < 0 || new_source_out <= new_source_in) {
      return false;
    }
    source_in = new_source_in;
    source_out = new_source_out;
    return true;
  }

  bool set_timeline_start(TimeUs new_timeline_start) noexcept {
    if (new_timeline_start < 0) {
      return false;
    }
    timeline_start = new_timeline_start;
    return true;
  }

  bool set_speed(double new_speed) noexcept {
    if (!std::isfinite(new_speed) || new_speed <= 0.0) {
      return false;
    }
    speed = new_speed;
    return true;
  }

  void set_opacity(float new_opacity) noexcept {
    if (!std::isfinite(new_opacity)) {
      return;
    }
    opacity = new_opacity < 0.0f ? 0.0f : (new_opacity > 1.0f ? 1.0f : new_opacity);
  }
};

}
