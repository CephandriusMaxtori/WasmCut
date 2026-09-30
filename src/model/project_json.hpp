#pragma once

#include "model/project.hpp"

#include <string>
#include <string_view>

namespace wasmcut::model {

// Bumped whenever the on-disk layout changes in a way older builds cannot read.
inline constexpr int current_schema_version = 1;

// Serialises the whole project, including media metadata. Media blobs themselves
// live in the browser; `storage_key` is what links the two sides together.
[[nodiscard]] std::string serialize_project(const Project& project);

// Parses a project document. On failure the output is left untouched and `error`
// explains what went wrong.
[[nodiscard]] bool deserialize_project(std::string_view text, Project& out, std::string& error);

// Describes the timeline as an ordered list of render segments, used by the
// export pipeline on the host side. Times are seconds; `mediaId` is resolved to
// a browser File by the host.
[[nodiscard]] std::string serialize_render_plan(const Project& project);

}
