#include "keyboard_configurator/json_lite.hpp"

#include <cctype>
#include <string>

namespace kb::cfg::json_lite {

namespace {

// Advances past a JSON string starting at `pos` (which must index the opening
// quote), honouring backslash escapes so a quote inside a string does not end
// the scan.
std::size_t skipString(const std::string& text, std::size_t pos) {
    ++pos;  // opening quote
    while (pos < text.size()) {
        if (text[pos] == '\\') {
            pos += 2;
            continue;
        }
        if (text[pos] == '"') {
            return pos + 1;
        }
        ++pos;
    }
    return text.size();
}

// Given the offset just past a key's closing quote, returns the offset of the
// first character of its value, or npos when what follows is not ": <value>".
std::size_t valueAfterKey(const std::string& text, std::size_t after_key) {
    while (after_key < text.size() &&
           std::isspace(static_cast<unsigned char>(text[after_key]))) {
        ++after_key;
    }
    if (after_key >= text.size() || text[after_key] != ':') {
        return std::string::npos;
    }
    ++after_key;
    while (after_key < text.size() &&
           std::isspace(static_cast<unsigned char>(text[after_key]))) {
        ++after_key;
    }
    return after_key < text.size() ? after_key : std::string::npos;
}

std::string decodeEscapes(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out.push_back(raw[i]);
            continue;
        }
        switch (raw[++i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'u': {
                // Enough for the Latin-1 range that shows up in window titles;
                // anything higher is replaced rather than mangled.
                if (i + 4 < raw.size()) {
                    const std::string hex = raw.substr(i + 1, 4);
                    i += 4;
                    try {
                        const auto code = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                        if (code < 0x80) {
                            out.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                    } catch (...) {
                        out.push_back('?');
                    }
                }
                break;
            }
            default: out.push_back(raw[i]); break;
        }
    }
    return out;
}

}  // namespace

std::size_t findObject(const std::string& text, const std::string& key, std::size_t from) {
    const std::string needle = '"' + key + '"';
    std::size_t pos = from;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t after = pos + needle.size();
        while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) {
            ++after;
        }
        if (after < text.size() && text[after] == ':') {
            ++after;
            while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) {
                ++after;
            }
            if (after < text.size() && text[after] == '{') {
                return after;
            }
        }
        pos = after;
    }
    return std::string::npos;
}

std::size_t memberValue(const std::string& text, const std::string& key,
                        std::size_t object_start) {
    if (object_start >= text.size() || text[object_start] != '{') {
        return std::string::npos;
    }

    const std::string needle = '"' + key + '"';
    int depth = 0;
    std::size_t pos = object_start;

    while (pos < text.size()) {
        const char ch = text[pos];

        if (ch == '"') {
            // Only keys sitting directly in this object count, so one of the
            // same name inside a child cannot be mistaken for ours.
            if (depth == 1 && text.compare(pos, needle.size(), needle) == 0) {
                const std::size_t value = valueAfterKey(text, pos + needle.size());
                if (value != std::string::npos) {
                    return value;
                }
            }
            pos = skipString(text, pos);
            continue;
        }

        if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
            if (depth == 0) {
                break;  // end of the object we were asked about
            }
        }
        ++pos;
    }
    return std::string::npos;
}

std::optional<Member> firstMember(const std::string& text, std::size_t object_start) {
    if (object_start >= text.size() || text[object_start] != '{') {
        return std::nullopt;
    }

    std::size_t pos = object_start + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (pos >= text.size() || text[pos] != '"') {
        return std::nullopt;  // empty object, or malformed
    }

    const std::size_t key_start = pos + 1;
    const std::size_t key_end = skipString(text, pos) - 1;
    const std::size_t value = valueAfterKey(text, key_end + 1);
    if (value == std::string::npos) {
        return std::nullopt;
    }
    return Member{decodeEscapes(text.substr(key_start, key_end - key_start)), value};
}

std::vector<std::size_t> arrayObjects(const std::string& text, std::size_t array_start) {
    std::vector<std::size_t> objects;
    if (array_start >= text.size() || text[array_start] != '[') {
        return objects;
    }

    int depth = 0;
    std::size_t pos = array_start;

    while (pos < text.size()) {
        const char ch = text[pos];

        if (ch == '"') {
            pos = skipString(text, pos);
            continue;
        }

        if (ch == '{' || ch == '[') {
            // Only elements of this array, not of a nested one.
            if (depth == 1 && ch == '{') {
                objects.push_back(pos);
            }
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        ++pos;
    }
    return objects;
}

std::optional<std::string> stringField(const std::string& text, const std::string& key,
                                       std::size_t object_start) {
    const std::size_t value = memberValue(text, key, object_start);
    if (value == std::string::npos || text[value] != '"') {
        return std::nullopt;  // absent, null, or not a string
    }
    const std::size_t end = skipString(text, value) - 1;
    return decodeEscapes(text.substr(value + 1, end - value - 1));
}

std::optional<long long> intField(const std::string& text, const std::string& key,
                                  std::size_t object_start) {
    const std::size_t value = memberValue(text, key, object_start);
    if (value == std::string::npos) {
        return std::nullopt;
    }
    if (text[value] != '-' && std::isdigit(static_cast<unsigned char>(text[value])) == 0) {
        return std::nullopt;  // null, or not a number
    }
    try {
        std::size_t consumed = 0;
        const long long parsed = std::stoll(text.substr(value, 32), &consumed);
        // A float would parse as its integer part and silently look fine; ids
        // are integers, so refuse anything that does not end cleanly.
        const std::size_t after = value + consumed;
        if (after < text.size() && (text[after] == '.' || text[after] == 'e' || text[after] == 'E')) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> boolField(const std::string& text, const std::string& key,
                              std::size_t object_start) {
    const std::size_t value = memberValue(text, key, object_start);
    if (value == std::string::npos) {
        return std::nullopt;
    }
    if (text.compare(value, 4, "true") == 0) {
        return true;
    }
    if (text.compare(value, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

}  // namespace kb::cfg::json_lite
