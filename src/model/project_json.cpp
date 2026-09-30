#include "model/project_json.hpp"

#include "model/json.hpp"

#include <algorithm>
#include <cmath>

namespace wasmcut::model {
namespace {

json::Value serialize_media(const MediaAsset& asset) {
  json::Value value = json::Value::object();
  value.set("id", asset.id);
  value.set("name", asset.name);
  value.set("fileName", asset.file_name);
  value.set("mimeType", asset.mime_type);
  value.set("storageKey", asset.storage_key.empty() ? asset.file_name : asset.storage_key);
  value.set("type", media_type_name(asset.type));
  value.set("bytes", static_cast<unsigned long long>(asset.file_size));
  value.set("durationUs", static_cast<long long>(asset.duration));
  value.set("width", asset.width);
  value.set("height", asset.height);
  if (asset.frame_rate > 0.0) {
    value.set("frameRate", asset.frame_rate);
  }
  return value;
}

json::Value serialize_clip(const Clip& clip) {
  json::Value value = json::Value::object();
  value.set("id", clip.id);
  value.set("mediaId", clip.media_id);
  value.set("name", clip.name);
  value.set("startUs", static_cast<long long>(clip.timeline_start));
  value.set("inUs", static_cast<long long>(clip.source_in));
  value.set("outUs", static_cast<long long>(clip.source_out));
  value.set("speed", clip.speed);
  value.set("opacity", static_cast<double>(clip.opacity));
  value.set("volume", static_cast<double>(clip.volume));
  return value;
}

json::Value serialize_track(const Track& track) {
  json::Value clips = json::Value::array();
  for (const Clip& clip : track.clips) {
    clips.push_back(serialize_clip(clip));
  }
  json::Value value = json::Value::object();
  value.set("id", track.id);
  value.set("name", track.name);
  value.set("type", track_type_name(track.type));
  value.set("enabled", track.enabled);
  value.set("locked", track.locked);
  value.set("muted", track.muted);
  value.set("solo", track.solo);
  value.set("clips", std::move(clips));
  return value;
}

std::string text_at(const json::Value& parent, const char* key, std::string fallback) {
  const json::Value* found = parent.find(key);
  return found == nullptr ? std::move(fallback) : found->as_string(std::move(fallback));
}

long long integer_at(const json::Value& parent, const char* key, long long fallback) {
  const json::Value* found = parent.find(key);
  return found == nullptr ? fallback : found->as_integer(fallback);
}

double number_at(const json::Value& parent, const char* key, double fallback) {
  const json::Value* found = parent.find(key);
  return found == nullptr ? fallback : found->as_number(fallback);
}

bool bool_at(const json::Value& parent, const char* key, bool fallback) {
  const json::Value* found = parent.find(key);
  return found == nullptr ? fallback : found->as_bool(fallback);
}

bool read_media(const json::Value& value, MediaAsset& out) {
  if (!value.is_object()) {
    return false;
  }
  MediaAsset asset;
  asset.id = text_at(value, "id", "");
  if (asset.id.empty()) {
    return false;
  }
  asset.name = text_at(value, "name", asset.id);
  asset.file_name = text_at(value, "fileName", asset.name);
  asset.mime_type = text_at(value, "mimeType", "");
  asset.storage_key = text_at(value, "storageKey", asset.file_name);
  asset.type = parse_media_type(text_at(value, "type", "unknown"));
  const auto bytes = integer_at(value, "bytes", 0);
  asset.file_size = bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0;
  const auto duration = integer_at(value, "durationUs", 0);
  asset.duration = duration > 0 ? static_cast<TimeUs>(duration) : 0;
  asset.width = static_cast<int>(integer_at(value, "width", 0));
  asset.height = static_cast<int>(integer_at(value, "height", 0));
  asset.frame_rate = number_at(value, "frameRate", 0.0);
  if (!asset.is_valid()) {
    return false;
  }
  out = std::move(asset);
  return true;
}

bool read_clip(const json::Value& value, const Project& project, Clip& out, std::string& error) {
  if (!value.is_object()) {
    error = "a clip entry is not an object";
    return false;
  }
  Clip clip;
  clip.id = text_at(value, "id", "");
  clip.media_id = text_at(value, "mediaId", "");
  if (clip.id.empty() || clip.media_id.empty()) {
    error = "a clip is missing its id or media reference";
    return false;
  }
  if (project.find_media(clip.media_id) == nullptr) {
    error = "clip '" + clip.id + "' refers to unknown media '" + clip.media_id + "'";
    return false;
  }
  clip.name = text_at(value, "name", "");
  if (clip.name.empty()) {
    const MediaAsset* asset = project.find_media(clip.media_id);
    clip.name = asset != nullptr ? asset->name : clip.id;
  }
  clip.timeline_start = static_cast<TimeUs>(std::max<long long>(0, integer_at(value, "startUs", 0)));
  const TimeUs source_in = static_cast<TimeUs>(std::max<long long>(0, integer_at(value, "inUs", 0)));
  const TimeUs source_out = static_cast<TimeUs>(std::max<long long>(0, integer_at(value, "outUs", 0)));
  if (source_out <= source_in) {
    error = "clip '" + clip.id + "' has an empty source range";
    return false;
  }
  clip.source_in = source_in;
  clip.source_out = source_out;
  if (!clip.set_speed(number_at(value, "speed", 1.0))) {
    error = "clip '" + clip.id + "' has an invalid speed";
    return false;
  }
  clip.set_opacity(static_cast<float>(number_at(value, "opacity", 1.0)));
  if (!clip.set_volume(static_cast<float>(number_at(value, "volume", 1.0)))) {
    error = "clip '" + clip.id + "' has an invalid volume";
    return false;
  }
  if (!clip.is_valid()) {
    error = "clip '" + clip.id + "' is not valid";
    return false;
  }
  out = std::move(clip);
  return true;
}

bool read_track(const json::Value& value, Project& project, std::string& error) {
  if (!value.is_object()) {
    error = "a track entry is not an object";
    return false;
  }
  Track track;
  track.id = text_at(value, "id", "");
  if (track.id.empty()) {
    error = "a track is missing its id";
    return false;
  }
  track.name = text_at(value, "name", track.id);
  track.type = parse_track_type(text_at(value, "type", "video"));
  track.enabled = bool_at(value, "enabled", true);
  track.locked = bool_at(value, "locked", false);
  track.muted = bool_at(value, "muted", false);
  track.solo = bool_at(value, "solo", false);

  const json::Value* clips = value.find("clips");
  if (clips != nullptr && clips->is_array()) {
    for (const json::Value& entry : clips->items()) {
      Clip clip;
      if (!read_clip(entry, project, clip, error)) {
        return false;
      }
      track.clips.push_back(std::move(clip));
    }
  }
  if (project.add_track(std::move(track)) == nullptr) {
    error = "duplicate or invalid track id";
    return false;
  }
  return true;
}

}

std::string serialize_project(const Project& project) {
  json::Value media = json::Value::array();
  for (const MediaAsset& asset : project.media_assets) {
    media.push_back(serialize_media(asset));
  }
  json::Value tracks = json::Value::array();
  for (const Track& track : project.tracks) {
    tracks.push_back(serialize_track(track));
  }

  json::Value root = json::Value::object();
  root.set("schema", static_cast<long long>(project.schema_version));
  root.set("id", project.id);
  root.set("name", project.name);
  root.set("frameRate", project.frame_rate);
  root.set("sampleRate", static_cast<long long>(project.sample_rate));
  root.set("durationUs", static_cast<long long>(project.duration_us));
  root.set("media", std::move(media));
  root.set("tracks", std::move(tracks));
  return root.dump();
}

bool deserialize_project(std::string_view text, Project& out, std::string& error) {
  error.clear();
  json::Value root;
  if (!json::parse(text, root, error)) {
    return false;
  }
  if (!root.is_object()) {
    error = "the project file is not a JSON object";
    return false;
  }

  const json::Value* schema_value = root.find("schema");
  const auto schema = static_cast<int>(schema_value != nullptr ? schema_value->as_integer(0) : 0);
  if (schema <= 0 || schema > current_schema_version) {
    error = "unsupported project schema version";
    return false;
  }

  Project project;
  project.schema_version = schema;
  project.tracks.clear();
  const json::Value* id = root.find("id");
  if (id != nullptr && !id->as_string().empty()) {
    project.id = id->as_string();
  }
  const json::Value* name = root.find("name");
  if (name != nullptr && !name->as_string().empty()) {
    project.name = name->as_string();
  }
  const json::Value* frame_rate = root.find("frameRate");
  if (!project.set_frame_rate(frame_rate != nullptr ? frame_rate->as_number(30.0) : 30.0)) {
    error = "invalid frame rate";
    return false;
  }
  const json::Value* sample_rate = root.find("sampleRate");
  project.set_sample_rate(static_cast<int>(sample_rate != nullptr ? sample_rate->as_integer(48000) : 48000));

  const json::Value* media = root.find("media");
  if (media != nullptr && media->is_array()) {
    for (const json::Value& entry : media->items()) {
      MediaAsset asset;
      if (!read_media(entry, asset)) {
        error = "a media entry is missing required fields";
        return false;
      }
      if (project.find_media(asset.id) == nullptr && project.add_media(std::move(asset)) == nullptr) {
        error = "a media entry is invalid";
        return false;
      }
    }
  }

  const json::Value* tracks = root.find("tracks");
  if (tracks == nullptr || !tracks->is_array() || tracks->items().empty()) {
    error = "the project has no tracks";
    return false;
  }
  for (const json::Value& entry : tracks->items()) {
    if (!read_track(entry, project, error)) {
      return false;
    }
  }

  project.ensure_default_tracks();
  project.recalculate_duration();
  if (!project.is_valid()) {
    error = "the project failed validation";
    return false;
  }
  out = std::move(project);
  return true;
}

std::string serialize_render_plan(const Project& project) {
  std::vector<json::Value> ordered;

  const auto any_solo = std::any_of(project.tracks.begin(), project.tracks.end(), [](const Track& track) {
    return track.solo;
  });

  for (const Track& track : project.tracks) {
    if (!track.enabled) {
      continue;
    }
    if (track.is_audio() && track.muted) {
      continue;
    }
    if (any_solo && !track.solo) {
      continue;
    }
    for (const Clip& clip : track.clips) {
      const MediaAsset* asset = project.find_media(clip.media_id);
      if (asset == nullptr) {
        continue;
      }
      json::Value segment = json::Value::object();
      segment.set("clipId", clip.id);
      segment.set("mediaId", asset->id);
      segment.set("storageKey", asset->storage_key.empty() ? asset->file_name : asset->storage_key);
      segment.set("fileName", asset->file_name);
      segment.set("mediaType", media_type_name(asset->type));
      segment.set("trackId", track.id);
      segment.set("track", track_type_name(track.type));
      segment.set("start", time_to_seconds(clip.timeline_start));
      segment.set("sourceIn", time_to_seconds(clip.source_in));
      segment.set("duration", time_to_seconds(clip.timeline_duration()));
      segment.set("sourceDuration", time_to_seconds(clip.source_duration()));
      segment.set("speed", clip.speed);
      segment.set("opacity", static_cast<double>(clip.opacity));
      segment.set("volume", static_cast<double>(clip.volume));
      ordered.push_back(std::move(segment));
    }
  }

  std::sort(ordered.begin(), ordered.end(), [](const json::Value& left, const json::Value& right) {
    const json::Value* left_start = left.find("start");
    const json::Value* right_start = right.find("start");
    return (left_start != nullptr ? left_start->as_number() : 0.0) < (right_start != nullptr ? right_start->as_number() : 0.0);
  });

  json::Value segments = json::Value::array();
  for (json::Value& segment : ordered) {
    segments.push_back(std::move(segment));
  }

  json::Value root = json::Value::object();
  root.set("frameRate", project.frame_rate);
  root.set("duration", time_to_seconds(project.duration()));
  root.set("segments", std::move(segments));
  return root.dump();
}

}
