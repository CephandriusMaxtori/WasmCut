#include "model/json.hpp"
#include "model/project.hpp"
#include "model/project_json.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int g_checks = 0;

void require(bool condition, const char* message) {
  ++g_checks;
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void require_equal_int(long long actual, long long expected, const char* message) {
  ++g_checks;
  if (actual != expected) {
    std::cerr << "FAILED: " << message << " (expected " << expected << ", got " << actual << ")\n";
    std::exit(1);
  }
}

void require_equal_str(const std::string& actual, const char* expected, const char* message) {
  ++g_checks;
  if (actual != expected) {
    std::cerr << "FAILED: " << message << " (expected \"" << expected << "\", got \"" << actual << "\")\n";
    std::exit(1);
  }
}

void test_time() {
  using namespace wasmcut::model;
  require(seconds_to_time(1.5) == 1'500'000, "time conversion failed");
  require(seconds_to_time(-1.0) == 0, "negative time should clamp to zero");
  require(seconds_to_time(std::nan("1")) == 0, "NaN time should clamp to zero");
  require_equal_str(format_timecode(0, 30.0), "00:00:00:00", "timecode at origin");
  require_equal_str(format_timecode(1'500'000, 30.0), "00:00:01:15", "timecode with frames at 30fps");
  require_equal_str(format_timecode(3'661'500'000, 30.0), "01:01:01:15", "timecode with hours");
  require_equal_str(format_timecode(90'000'000, 25.0), "00:01:30:00", "timecode uses frames at 25fps");
  require_equal_str(format_timecode(1'500'000, 10.0), "00:00:01.500", "low frame rates fall back to milliseconds");
  require_equal_int(frame_duration(30.0), 33'333, "frame duration at 30fps");
  require(floor_to_frame(1'040'000, 30.0) == 1'033'333, "floor to frame boundary");
  require(floor_to_frame(0, 30.0) == 0, "floor to frame at the origin");
  require_equal_str(format_bytes(1536), "1.5 KB", "byte formatting");
  require_equal_str(format_bytes(900), "900 B", "small byte formatting");
}

void test_json() {
  using wasmcut::json::Value;
  Value parsed;
  std::string error;
  require(wasmcut::json::parse(R"({"a":1,"b":[true,null,-2.5],"c":{"d":"e\n\"f\""}})", parsed, error), "json parse failed");
  require(parsed.is_object(), "json root should be an object");
  require_equal_int(parsed.find("a")->as_integer(), 1, "json number lookup");
  require_equal_str(parsed.find("c")->find("d")->as_string(), "e\n\"f\"", "json escape decoding");
  const auto& items = parsed.find("b")->items();
  require_equal_int(static_cast<long long>(items.size()), 3, "json array size");
  require(items[0].as_bool(), "json true value");
  require(items[1].is_null(), "json null value");
  require(items[2].as_number() == -2.5, "json negative number");

  Value built = Value::object();
  built.set("name", "a\"b");
  built.set("count", 3.0);
  require_equal_str(built.dump(), R"({"name":"a\"b","count":3})", "json dump round trip");

  require(!wasmcut::json::parse("{", parsed, error), "malformed json should fail");
  require(!wasmcut::json::parse("{} junk", parsed, error), "trailing garbage should fail");
  require(error.empty() == false, "parse errors should be reported");
}

void test_project_basics() {
  using namespace wasmcut::model;
  require(Project().is_valid(), "default project is invalid");

  Project project;
  require_equal_int(static_cast<long long>(project.tracks.size()), 2, "default tracks were not created");
  require(project.find_track("track-video") != nullptr, "video track is missing");
  require(project.find_track("track-audio") != nullptr, "audio track is missing");

  MediaAsset asset;
  asset.id = "media-1";
  asset.name = "Sample";
  asset.file_name = "sample.webm";
  asset.type = MediaType::Video;
  asset.duration = 4'000'000;
  asset.width = 1280;
  asset.height = 720;
  require(project.add_media(asset) != nullptr, "media asset was not added");
  require(project.add_media(asset) == nullptr, "duplicate media asset was accepted");
  require(project.find_media("media-1") != nullptr, "media asset lookup failed");
  require(asset.has_video() && asset.has_audio(), "video media should report both streams");

  Clip first;
  first.id = "clip-1";
  first.media_id = asset.id;
  first.name = "First";
  first.timeline_start = 2'000'000;
  first.set_source_range(0, 1'000'000);
  require(project.create_clip(first, "track-video") != nullptr, "first clip was not created");

  Clip second;
  second.id = "clip-2";
  second.media_id = asset.id;
  second.name = "Second";
  second.timeline_start = 0;
  second.set_source_range(1'000'000, 2'000'000);
  require(project.create_clip(second, "track-video") != nullptr, "second clip was not created");

  Track* video = project.find_track("track-video");
  require(video != nullptr, "video track lookup failed");
  require_equal_int(static_cast<long long>(video->clips.size()), 2, "clips were not stored");
  require_equal_str(video->clips.front().id, "clip-2", "clips were not sorted");
  require(video->clip_at(500'000) != nullptr, "clip hit testing failed");
  require(video->clip_at(1'500'000) == nullptr, "unexpected clip hit");
  require(project.duration() == 3'000'000, "project duration is incorrect");
  require(project.is_valid(), "project became invalid");

  require(!project.remove_media(asset.id), "referenced media was removed");
  require(project.remove_clip("clip-1"), "clip removal failed");
  require(project.remove_clip("clip-2"), "second clip removal failed");
  require(project.remove_media(asset.id), "unreferenced media was not removed");
  require(!project.remove_clip("missing"), "missing clip removal reported success");
}

void test_clip_properties() {
  using namespace wasmcut::model;
  Clip clip;
  clip.id = "clip-speed";
  clip.media_id = "media-1";
  clip.timeline_start = 0;
  require(clip.set_source_range(0, 4'000'000), "source range rejected");

  require(clip.set_speed_preserving_duration(2.0), "speed change failed");
  require(clip.speed == 2.0, "speed was not applied");
  require(clip.timeline_duration() == 4'000'000, "speed change should keep the timeline duration");
  require(clip.source_duration() == 8'000'000, "source range should grow when slowing down");

  require(clip.set_speed_preserving_duration(0.5), "speed change failed");
  require(clip.timeline_duration() == 4'000'000, "speed change should keep the timeline duration");
  require(clip.source_duration() == 2'000'000, "source range should shrink when speeding up");
  require(!clip.set_speed_preserving_duration(0.0), "zero speed should be rejected");
  require(!clip.set_volume(-1.0f), "negative volume should be rejected");
  require(clip.set_volume(1.5f) && clip.volume == 1.5f, "volume should be applied");
  require(clip.is_valid(), "clip should remain valid after edits");
}

void test_serialization() {
  using namespace wasmcut::model;

  Project project;
  project.name = "Round Trip";
  project.set_frame_rate(24.0);
  project.set_sample_rate(44100);

  MediaAsset asset;
  asset.id = "media-1";
  asset.name = "Take 1";
  asset.file_name = "take-1.mp4";
  asset.storage_key = "take-1.mp4";
  asset.mime_type = "video/mp4";
  asset.type = MediaType::Video;
  asset.duration = 8'000'000;
  asset.width = 1920;
  asset.height = 1080;
  require(project.add_media(asset) != nullptr, "media add failed");

  Clip clip;
  clip.id = "clip-1";
  clip.media_id = asset.id;
  clip.name = "Take 1";
  clip.timeline_start = 1'000'000;
  clip.set_source_range(2'000'000, 6'000'000);
  clip.set_speed_preserving_duration(2.0);
  clip.set_opacity(0.5f);
  require(clip.set_volume(0.25f), "volume rejected");
  require(project.create_clip(clip, "track-video") != nullptr, "clip add failed");

  project.find_track("track-audio")->muted = true;
  project.find_track("track-video")->locked = true;

  const std::string text = serialize_project(project);
  require(!text.empty(), "serialisation produced nothing");

  Project restored;
  std::string error;
  require(deserialize_project(text, restored, error), std::string("deserialisation failed: ").append(error).c_str());
  require_equal_str(restored.name, "Round Trip", "project name round trip");
  require(restored.frame_rate == 24.0, "frame rate round trip");
  require_equal_int(restored.sample_rate, 44100, "sample rate round trip");
  require_equal_int(static_cast<long long>(restored.media_assets.size()), 1, "media count round trip");
  require_equal_int(static_cast<long long>(restored.tracks.size()), 2, "track count round trip");
  require_equal_str(restored.find_media("media-1")->storage_key, "take-1.mp4", "storage key round trip");
  const Clip* restored_clip = restored.find_clip("clip-1");
  require(restored_clip != nullptr, "clip round trip lookup failed");
  require(restored_clip->source_in == 2'000'000, "clip source in round trip");
  require(restored_clip->source_out == 10'000'000, "clip source out round trip");
  require(restored_clip->timeline_start == 1'000'000, "clip start round trip");
  require(restored_clip->speed == 2.0, "clip speed round trip");
  require(restored_clip->opacity == 0.5f, "clip opacity round trip");
  require(restored_clip->volume == 0.25f, "clip volume round trip");
  require(restored.find_track("track-audio")->muted, "track mute round trip");
  require(restored.find_track("track-video")->locked, "track lock round trip");
  require(serialize_project(restored) == text, "serialisation is not stable");

  Project untouched;
  require(!deserialize_project("{ not json", untouched, error), "malformed json should be rejected");
  require(!error.empty(), "malformed json should explain itself");
  require(!deserialize_project(R"({"schema":99})", untouched, error), "future schema should be rejected");
  require(!deserialize_project(R"({"schema":1})", untouched, error), "project without tracks should be rejected");
  require(
      !deserialize_project(
          R"({"schema":1,"media":[],"tracks":[{"id":"t","clips":[{"id":"c","mediaId":"missing"}]}]})",
          untouched,
          error),
      "clip with unknown media should be rejected");
  require(untouched.find_track("track-video") != nullptr, "a failed load must not disturb the target project");
}

void test_render_plan() {
  using namespace wasmcut::model;

  Project project;
  MediaAsset asset;
  asset.id = "media-1";
  asset.name = "Source";
  asset.file_name = "source.webm";
  asset.storage_key = "source.webm";
  asset.type = MediaType::Video;
  asset.duration = 10'000'000;
  require(project.add_media(asset) != nullptr, "media add failed");

  auto append = [&project, &asset](const char* id, const char* track, TimeUs start, TimeUs source_in, TimeUs source_out) {
    Clip clip;
    clip.id = id;
    clip.media_id = asset.id;
    clip.name = id;
    clip.timeline_start = start;
    clip.set_source_range(source_in, source_out);
    return project.create_clip(clip, track) != nullptr;
  };

  require(append("clip-late", "track-video", 5'000'000, 0, 1'000'000), "late clip add failed");
  require(append("clip-early", "track-video", 0, 0, 2'000'000), "early clip add failed");

  const std::string plan = serialize_render_plan(project);
  require(plan.find("clip-early") < plan.find("clip-late"), "render plan should be ordered by start time");

  project.find_track("track-video")->enabled = false;
  const std::string empty_plan = serialize_render_plan(project);
  require(empty_plan.find("segments\":[]") != std::string::npos, "disabled tracks should contribute no segments");
}

}

int main() {
  test_time();
  test_json();
  test_project_basics();
  test_clip_properties();
  test_serialization();
  test_render_plan();
  std::cout << "model tests passed (" << g_checks << " checks)\n";
  return 0;
}
