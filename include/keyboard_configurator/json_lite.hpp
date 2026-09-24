#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Just enough JSON to read a handful of fields out of a compositor's IPC
// events. A full parser would be a new dependency for a job this small, but a
// naive substring search is wrong -- window titles routinely contain braces and
// quotes, and the keys we want ("name", "id") also appear inside nested
// objects. These helpers track string escapes and object depth so neither case
// fools them.
//
// Every accessor below takes the offset of the '{' opening the object the field
// belongs to, and only considers members of *that* object.
namespace kb::cfg::json_lite {

// Offset of the '{' that opens the object stored under `key`, or npos. Unlike
// the accessors below this searches forward from `from` rather than being
// scoped to one object; the sway backend relies on that.
[[nodiscard]] std::size_t findObject(const std::string& text, const std::string& key,
                                     std::size_t from = 0);

// Offset of the value of `key` within the object at `object_start`, or npos.
// The offset points at the first character of the value, so the caller can tell
// an object from an array from a literal by looking at it.
[[nodiscard]] std::size_t memberValue(const std::string& text, const std::string& key,
                                      std::size_t object_start);

// A key and the offset of its value.
struct Member {
    std::string key;
    std::size_t value_start;
};

// The first member of the object at `object_start`. This is how an externally
// tagged enum is read -- niri sends {"WindowFocusChanged":{...}}, where the
// variant name is the key. Searching for the name instead would let a window
// title containing it pose as the tag.
[[nodiscard]] std::optional<Member> firstMember(const std::string& text,
                                                std::size_t object_start);

// Offsets of the '{' of each object directly inside the array at `array_start`
// (which must be its '['). Non-object elements are skipped.
[[nodiscard]] std::vector<std::size_t> arrayObjects(const std::string& text,
                                                    std::size_t array_start);

// Values of fields belonging directly to the object at `object_start` (which
// must be its '{'). Fields of nested objects are ignored, and a field that is
// present but JSON null, or is of the wrong type, reads as absent.
[[nodiscard]] std::optional<std::string> stringField(const std::string& text, const std::string& key,
                                                     std::size_t object_start);
[[nodiscard]] std::optional<long long> intField(const std::string& text, const std::string& key,
                                                std::size_t object_start);
[[nodiscard]] std::optional<bool> boolField(const std::string& text, const std::string& key,
                                            std::size_t object_start);

}  // namespace kb::cfg::json_lite
