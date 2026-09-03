#pragma once

#include <cstddef>
#include <optional>
#include <string>

// Just enough JSON to read two fields out of a sway/i3 IPC window event. A full
// parser would be a new dependency for a job this small, but a naive substring
// search is wrong -- window titles routinely contain braces and quotes, and the
// key we want ("name") also appears inside nested containers. These helpers
// track string escapes and object depth so neither case fools them.
namespace kb::cfg::json_lite {

// Offset of the '{' that opens the object stored under `key`, or npos.
[[nodiscard]] std::size_t findObject(const std::string& text, const std::string& key,
                                     std::size_t from = 0);

// Value of a string field belonging directly to the object at `object_start`
// (which must be its '{'). Fields of nested objects are ignored.
[[nodiscard]] std::optional<std::string> stringField(const std::string& text, const std::string& key,
                                                     std::size_t object_start);

}  // namespace kb::cfg::json_lite
