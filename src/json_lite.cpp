#include "keyboard_configurator/json_lite.hpp"

#include <cctype>

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

std::optional<std::string> stringField(const std::string& text, const std::string& key,
                                       std::size_t object_start) {
    if (object_start >= text.size() || text[object_start] != '{') {
        return std::nullopt;
    }

    const std::string needle = '"' + key + '"';
    int depth = 0;
    std::size_t pos = object_start;

    while (pos < text.size()) {
        const char ch = text[pos];

        if (ch == '"') {
            // Only consider keys sitting directly in this object, so a "name"
            // nested in a child container cannot be mistaken for ours.
            if (depth == 1 && text.compare(pos, needle.size(), needle) == 0) {
                std::size_t after = pos + needle.size();
                while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) {
                    ++after;
                }
                if (after < text.size() && text[after] == ':') {
                    ++after;
                    while (after < text.size() &&
                           std::isspace(static_cast<unsigned char>(text[after]))) {
                        ++after;
                    }
                    if (after < text.size() && text[after] == '"') {
                        const std::size_t value_start = after + 1;
                        const std::size_t value_end = skipString(text, after) - 1;
                        return decodeEscapes(text.substr(value_start, value_end - value_start));
                    }
                    // Present but null or a number: report it as absent.
                    return std::nullopt;
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
    return std::nullopt;
}

}  // namespace kb::cfg::json_lite
