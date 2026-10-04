#include "keyboard_configurator/chroma_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>

namespace kb::cfg {

namespace {

constexpr std::size_t kMaxHeaderBytes = 8 * 1024;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;
constexpr std::size_t kMaxConnections = 32;
constexpr std::size_t kMaxSessions = 64;
constexpr std::size_t kMaxStoredEffects = 512;
constexpr auto kIdleConnection = std::chrono::seconds(30);

// RZRESULT values the REST API reports in its "result" field.
constexpr int kResultSuccess = 0;
constexpr int kResultAccessDenied = 5;
constexpr int kResultInvalidParameter = 87;
constexpr int kResultNotFound = 1168;

// --- JSON --------------------------------------------------------------------
//
// Request bodies are small and come from local processes, but a parser that
// recursed without limit would let any of them crash the daemon, so depth is
// capped. Strings are kept only to read an app's title; \u escapes become '?'.

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type{Type::Null};
    double number{0.0};
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    [[nodiscard]] const Json* get(const char* key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        for (const auto& [name, value] : fields) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool parse(Json& out) {
        skip();
        if (!value(out, 0)) {
            return false;
        }
        skip();
        return i_ == s_.size();
    }

private:
    static constexpr int kMaxDepth = 16;

    void skip() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0) {
            ++i_;
        }
    }

    bool literal(const char* word) {
        const std::size_t n = std::strlen(word);
        if (s_.compare(i_, n, word) == 0) {
            i_ += n;
            return true;
        }
        return false;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth || i_ >= s_.size()) {
            return false;
        }
        const char c = s_[i_];
        if (c == '{') {
            return object(out, depth);
        }
        if (c == '[') {
            return array(out, depth);
        }
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.text);
        }
        if (literal("true")) {
            out.type = Json::Type::Bool;
            out.number = 1.0;
            return true;
        }
        if (literal("false")) {
            out.type = Json::Type::Bool;
            return true;
        }
        if (literal("null")) {
            out.type = Json::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            const char* begin = s_.c_str() + i_;
            char* end = nullptr;
            out.number = std::strtod(begin, &end);
            if (end == begin) {
                return false;
            }
            i_ += static_cast<std::size_t>(end - begin);
            out.type = Json::Type::Number;
            return true;
        }
        return false;
    }

    bool string(std::string& out) {
        ++i_;  // Opening quote.
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) {
                return false;
            }
            const char escaped = s_[i_++];
            switch (escaped) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u':
                    if (i_ + 4 > s_.size()) {
                        return false;
                    }
                    i_ += 4;
                    out.push_back('?');
                    break;
                default: out.push_back(escaped); break;
            }
        }
        return false;
    }

    bool array(Json& out, int depth) {
        ++i_;
        out.type = Json::Type::Array;
        skip();
        if (i_ < s_.size() && s_[i_] == ']') {
            ++i_;
            return true;
        }
        while (true) {
            out.items.emplace_back();
            skip();
            if (!value(out.items.back(), depth + 1)) {
                return false;
            }
            skip();
            if (i_ >= s_.size()) {
                return false;
            }
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == ']') {
                ++i_;
                return true;
            }
            return false;
        }
    }

    bool object(Json& out, int depth) {
        ++i_;
        out.type = Json::Type::Object;
        skip();
        if (i_ < s_.size() && s_[i_] == '}') {
            ++i_;
            return true;
        }
        while (true) {
            skip();
            if (i_ >= s_.size() || s_[i_] != '"') {
                return false;
            }
            std::string key;
            if (!string(key)) {
                return false;
            }
            skip();
            if (i_ >= s_.size() || s_[i_] != ':') {
                return false;
            }
            ++i_;
            skip();
            out.fields.emplace_back(std::move(key), Json{});
            if (!value(out.fields.back().second, depth + 1)) {
                return false;
            }
            skip();
            if (i_ >= s_.size()) {
                return false;
            }
            if (s_[i_] == ',') {
                ++i_;
                continue;
            }
            if (s_[i_] == '}') {
                ++i_;
                return true;
            }
            return false;
        }
    }

    const std::string& s_;
    std::size_t i_{0};
};

std::uint32_t toColorRef(double value) {
    if (!(value >= 0.0)) {
        return 0;
    }
    if (value >= 4294967295.0) {
        return 0xFFFFFFFFu;
    }
    return static_cast<std::uint32_t>(value);
}

// A 2D array of colours, read into the grid. Rows or columns beyond Chroma's
// 6x22 are ignored, and missing ones stay dark.
bool readGrid(const Json& rows, ChromaState::Grid& out) {
    if (rows.type != Json::Type::Array || rows.items.empty()) {
        return false;
    }
    out.fill(0);
    const std::size_t row_count = std::min<std::size_t>(rows.items.size(), ChromaState::kRows);
    for (std::size_t r = 0; r < row_count; ++r) {
        const Json& row = rows.items[r];
        if (row.type != Json::Type::Array) {
            return false;
        }
        const std::size_t col_count = std::min<std::size_t>(row.items.size(), ChromaState::kCols);
        for (std::size_t c = 0; c < col_count; ++c) {
            if (row.items[c].type == Json::Type::Number) {
                out[r * ChromaState::kCols + c] = toColorRef(row.items[c].number);
            }
        }
    }
    return true;
}

// A keyboard effect body -> the grid it describes. False for effects that
// carry no frame (breathing, wave and the other animated ones), which apps are
// told succeeded and which then simply change nothing.
bool keyboardGrid(const Json& body, ChromaState::Grid& out) {
    const Json* effect = body.get("effect");
    if (effect == nullptr || effect->type != Json::Type::String) {
        return false;
    }
    const std::string& name = effect->text;
    const Json* param = body.get("param");

    if (name == "CHROMA_NONE") {
        out.fill(0);
        return true;
    }
    if (name == "CHROMA_STATIC") {
        const Json* color = param != nullptr ? param->get("color") : nullptr;
        if (color == nullptr || color->type != Json::Type::Number) {
            return false;
        }
        out.fill(toColorRef(color->number) & 0x00FFFFFFu);
        return true;
    }
    if (name == "CHROMA_CUSTOM") {
        if (param == nullptr || !readGrid(*param, out)) {
            return false;
        }
        for (auto& value : out) {
            value &= 0x00FFFFFFu;
        }
        return true;
    }
    if (name == "CHROMA_CUSTOM_KEY") {
        // `color` is the background grid; a `key` entry with its high byte set
        // addresses one key and wins over the background there.
        const Json* color = param != nullptr ? param->get("color") : nullptr;
        const Json* key = param != nullptr ? param->get("key") : nullptr;
        if (color == nullptr || !readGrid(*color, out)) {
            return false;
        }
        ChromaState::Grid keys{};
        if (key != nullptr && readGrid(*key, keys)) {
            for (std::size_t i = 0; i < out.size(); ++i) {
                if ((keys[i] & 0xFF000000u) != 0) {
                    out[i] = keys[i];
                }
            }
        }
        for (auto& value : out) {
            value &= 0x00FFFFFFu;
        }
        return true;
    }
    return false;
}

std::string result(int code) {
    return "{\"result\":" + std::to_string(code) + "}";
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            out.push_back(c);
        }
    }
    return out;
}

bool isDigits(const std::string& text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool isOtherDevice(const std::string& name) {
    return name == "mouse" || name == "mousepad" || name == "headset" || name == "keypad" ||
           name == "chromalink";
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

// Pulls one complete request off the front of `buffer`. 1 when one was taken,
// 0 when more bytes are needed, -1 when the bytes can never become a request
// this server accepts.
int extractRequest(std::string& buffer, std::string& method, std::string& path, std::string& body,
                   std::string& host, bool& has_origin, bool& keep_alive) {
    const auto header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return buffer.size() > kMaxHeaderBytes ? -1 : 0;
    }

    std::istringstream head(buffer.substr(0, header_end));
    std::string request_line;
    std::getline(head, request_line);
    std::istringstream line(request_line);
    std::string version;
    line >> method >> path >> version;
    if (method.empty() || path.empty() || version.rfind("HTTP/", 0) != 0) {
        return -1;
    }

    keep_alive = (version == "HTTP/1.1");
    has_origin = false;
    host.clear();
    std::size_t content_length = 0;

    std::string header;
    while (std::getline(head, header)) {
        const auto colon = header.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = lower(trim(header.substr(0, colon)));
        const std::string value = trim(header.substr(colon + 1));
        if (name == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoul(value));
            } catch (...) {
                return -1;
            }
        } else if (name == "connection") {
            const std::string v = lower(value);
            if (v == "close") {
                keep_alive = false;
            } else if (v == "keep-alive") {
                keep_alive = true;
            }
        } else if (name == "host") {
            host = value;
        } else if (name == "origin") {
            has_origin = true;
        } else if (name == "transfer-encoding") {
            return -1;  // Chunked bodies are not something any Chroma client sends.
        }
    }
    if (content_length > kMaxBodyBytes) {
        return -1;
    }

    const std::size_t body_start = header_end + 4;
    if (buffer.size() < body_start + content_length) {
        return 0;
    }
    body = buffer.substr(body_start, content_length);
    buffer.erase(0, body_start + content_length);

    const auto query = path.find('?');
    if (query != std::string::npos) {
        path.resize(query);
    }
    return 1;
}

// A Host header naming anything but this machine means a page on another
// origin reached us through DNS rebinding; nothing legitimate does that.
bool hostIsLocal(const std::string& host) {
    if (host.empty()) {
        return true;  // HTTP/1.0 clients may omit it.
    }
    std::string name = lower(host);
    if (!name.empty() && name.front() == '[') {
        const auto close = name.find(']');
        name = (close == std::string::npos) ? name : name.substr(0, close + 1);
    } else {
        const auto colon = name.find(':');
        if (colon != std::string::npos) {
            name.resize(colon);
        }
    }
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

const char* statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default: return "Error";
    }
}

bool sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Replies are a few dozen bytes, so a full send buffer means the
            // client is not reading at all. Give it a moment, then give up.
            pollfd pfd{fd, POLLOUT, 0};
            if (::poll(&pfd, 1, 200) > 0) {
                continue;
            }
        }
        return false;
    }
    return true;
}

}  // namespace

ChromaServer::ChromaServer(ChromaStatePtr state, int port, std::chrono::milliseconds session_timeout,
                           Callbacks callbacks)
    : state_(std::move(state)),
      port_(port),
      session_timeout_(session_timeout),
      callbacks_(std::move(callbacks)) {
    // Session ids only need to differ from those of a previous run, so an app
    // still holding an old id is not confused with a newly registered one.
    std::random_device seed;
    next_session_ = 1000 + (static_cast<std::uint64_t>(seed()) % 9000000);
}

ChromaServer::~ChromaServer() {
    stop();
}

bool ChromaServer::start() {
    if (running_.load()) {
        return true;
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        std::string reason = std::strerror(errno);
        if (errno == EAFNOSUPPORT || errno == EPERM || errno == EACCES) {
            // Not a missing kernel feature: a sandbox forbids IPv4 sockets.
            // The usual culprit is a systemd unit restricting address families.
            reason += " -- IPv4 sockets are blocked for this process; under systemd the unit "
                      "needs RestrictAddressFamilies=AF_UNIX AF_INET";
        }
        state_->setListenError(port_, reason);
        std::cerr << "[Chroma] Not listening: " << reason << '\n';
        return false;
    }
    const int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        std::string reason = std::strerror(errno);
        if (errno == EADDRINUSE) {
            reason = "port " + std::to_string(port_) +
                     " is already in use -- another Chroma server (chroma_mock_server.py?) is running";
        }
        state_->setListenError(port_, reason);
        std::cerr << "[Chroma] Not listening: " << reason << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::pipe2(wake_pipe_, O_NONBLOCK | O_CLOEXEC) != 0) {
        state_->setListenError(port_, std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    state_->setListening(port_);
    running_.store(true);
    thread_ = std::thread(&ChromaServer::loop, this);
    std::cout << "[Chroma] Listening on 127.0.0.1:" << port_ << ".\n";
    return true;
}

void ChromaServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    const char byte = 1;
    [[maybe_unused]] const ssize_t ignored = ::write(wake_pipe_[1], &byte, 1);
    if (thread_.joinable()) {
        thread_.join();
    }
    for (auto& connection : connections_) {
        ::close(connection.fd);
    }
    connections_.clear();
    ::close(listen_fd_);
    ::close(wake_pipe_[0]);
    ::close(wake_pipe_[1]);
    listen_fd_ = -1;
    wake_pipe_[0] = wake_pipe_[1] = -1;
}

int ChromaServer::pollTimeoutMs(Clock::time_point now) const {
    if (sessions_.empty() && connections_.empty()) {
        return -1;  // Nothing can expire; sleep until a client arrives.
    }
    auto soonest = Clock::time_point::max();
    for (const auto& [id, session] : sessions_) {
        soonest = std::min(soonest, session.last_seen + session_timeout_);
    }
    for (const auto& connection : connections_) {
        soonest = std::min(soonest, connection.last_active + kIdleConnection);
    }
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(soonest - now).count();
    return static_cast<int>(std::clamp<long long>(wait + 1, 50, 60000));
}

void ChromaServer::loop() {
    std::vector<pollfd> fds;
    while (running_.load()) {
        const auto now = Clock::now();
        expire(now);

        fds.clear();
        fds.push_back({wake_pipe_[0], POLLIN, 0});
        fds.push_back({listen_fd_, POLLIN, 0});
        for (const auto& connection : connections_) {
            fds.push_back({connection.fd, POLLIN, 0});
        }

        const int ready = ::poll(fds.data(), fds.size(), pollTimeoutMs(now));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[Chroma] poll failed: " << std::strerror(errno) << '\n';
            break;
        }
        if (!running_.load() || (fds[0].revents & POLLIN) != 0) {
            break;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            acceptClients();
        }

        // Walk the connections that existed when poll() was called; any just
        // accepted are past the end of `fds` and get their turn next time.
        for (std::size_t i = 2, c = 0; i < fds.size() && c < connections_.size(); ++i) {
            Connection& connection = connections_[c];
            const bool ready_fd = fds[i].fd == connection.fd && fds[i].revents != 0;
            const bool keep = !ready_fd || serviceConnection(connection);
            if (!keep) {
                ::close(connection.fd);
                connections_.erase(connections_.begin() + static_cast<std::ptrdiff_t>(c));
            } else {
                ++c;
            }
        }

        // Reap connections a client opened and then left idle.
        const auto cutoff = Clock::now() - kIdleConnection;
        for (auto it = connections_.begin(); it != connections_.end();) {
            if (it->last_active < cutoff) {
                ::close(it->fd);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void ChromaServer::acceptClients() {
    while (true) {
        const int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return;  // EAGAIN: the backlog is drained.
        }
        if (connections_.size() >= kMaxConnections) {
            ::close(fd);
            continue;
        }
        connections_.push_back({fd, {}, Clock::now()});
    }
}

bool ChromaServer::serviceConnection(Connection& connection) {
    char buffer[4096];
    bool peer_closed = false;
    while (true) {
        const ssize_t n = ::recv(connection.fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            connection.in.append(buffer, static_cast<std::size_t>(n));
            if (connection.in.size() > kMaxHeaderBytes + kMaxBodyBytes) {
                return false;
            }
            continue;
        }
        if (n == 0) {
            // The client closed its side. Anything complete still gets an
            // answer below, then the connection goes: EOF stays readable, so
            // keeping it would wake poll() forever.
            peer_closed = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        return false;
    }
    connection.last_active = Clock::now();

    while (true) {
        Request request;
        const int got = extractRequest(connection.in, request.method, request.path, request.body,
                                       request.host, request.has_origin, request.keep_alive);
        if (got == 0) {
            return !peer_closed;
        }
        if (got < 0) {
            const std::string body = result(kResultInvalidParameter);
            sendAll(connection.fd, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
                                   "Content-Length: " + std::to_string(body.size()) +
                                       "\r\nConnection: close\r\n\r\n" + body);
            return false;
        }

        const Reply reply = handle(request, Clock::now());
        std::string response = "HTTP/1.1 " + std::to_string(reply.status) + ' ' +
                               statusText(reply.status) +
                               "\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(reply.body.size()) + "\r\nConnection: " +
                               (request.keep_alive ? "keep-alive" : "close") + "\r\n\r\n" + reply.body;
        if (!sendAll(connection.fd, response) || !request.keep_alive) {
            return false;
        }
    }
}

ChromaServer::Reply ChromaServer::handleForTest(const std::string& method, const std::string& path,
                                                const std::string& body) {
    Request request;
    request.method = method;
    request.path = path;
    request.body = body;
    return handle(request, Clock::now());
}

ChromaServer::Reply ChromaServer::handle(const Request& request, Clock::time_point now) {
    if (request.has_origin || !hostIsLocal(request.host)) {
        return {403, result(kResultAccessDenied)};
    }

    std::vector<std::string> parts;
    {
        std::size_t start = 0;
        const std::string& path = request.path;
        while (start < path.size()) {
            const auto slash = path.find('/', start);
            const auto end = (slash == std::string::npos) ? path.size() : slash;
            if (end > start) {
                parts.push_back(lower(path.substr(start, end - start)));
            }
            start = end + 1;
        }
    }
    if (parts.size() < 2 || parts[0] != "razer" || parts[1] != "chromasdk") {
        return {404, result(kResultNotFound)};
    }

    const std::string& method = request.method;
    if (parts.size() == 2) {
        if (method == "GET") {
            return {200, "{\"core\":\"3.36.0\",\"device\":\"3.36.0\",\"version\":\"3.36.0\"}"};
        }
        if (method == "POST") {
            return registerApp(request.body, now);
        }
        return {405, result(kResultInvalidParameter)};
    }

    // /razer/chromasdk/<session>/<what>. A path with no session -- what the
    // shim falls back to when registration failed -- is session "0".
    std::size_t next = 2;
    std::string session = "0";
    if (isDigits(parts[2])) {
        session = parts[2];
        next = 3;
    }

    // Unknown sessions are adopted rather than refused: a daemon restarted
    // while a game runs should keep receiving that game's frames, not reject
    // every one because the session it registered went with the old process.
    touch(session, now);

    if (next == parts.size()) {
        if (method == "DELETE") {
            sessions_.erase(session);
            updateActivity();
        }
        return {200, result(kResultSuccess)};
    }

    const std::string& what = parts[next];
    if (what == "heartbeat") {
        const auto tick = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();
        return {200, "{\"tick\":" + std::to_string(tick) + ",\"result\":0}"};
    }
    if (what == "keyboard") {
        return keyboard(method, request.body, session);
    }
    if (what == "effect") {
        return effect(method, request.body);
    }
    if (isOtherDevice(what)) {
        // Accepted so the app carries on, but there is nothing to light.
        if (method == "POST") {
            return {200, "{\"result\":0,\"id\":\"" + newEffectId() + "\"}"};
        }
        return {200, result(kResultSuccess)};
    }
    return {404, result(kResultNotFound)};
}

ChromaServer::Reply ChromaServer::registerApp(const std::string& body, Clock::time_point now) {
    Json parsed;
    std::string title = "unnamed app";
    if (JsonParser(body).parse(parsed)) {
        if (const Json* t = parsed.get("title"); t != nullptr && t->type == Json::Type::String &&
                                                 !t->text.empty()) {
            title = t->text.substr(0, 80);
        }
    }

    if (sessions_.size() >= kMaxSessions) {
        // Drop the quietest, so a client registering in a loop cannot grow
        // the table without bound.
        auto oldest = std::min_element(sessions_.begin(), sessions_.end(), [](const auto& a, const auto& b) {
            return a.second.last_seen < b.second.last_seen;
        });
        sessions_.erase(oldest);
    }

    const std::string id = std::to_string(next_session_++);
    Session& session = sessions_[id];
    session.title = title;
    session.last_seen = now;
    state_->setSessions(sessions_.size());
    std::cout << "[Chroma] " << title << " registered (session " << id << ").\n";

    return {200, "{\"sessionid\":" + id + ",\"uri\":\"http://127.0.0.1:" + std::to_string(port_) +
                     "/razer/chromasdk/" + id + "\"}"};
}

ChromaServer::Reply ChromaServer::keyboard(const std::string& method, const std::string& body,
                                           const std::string& session) {
    Json parsed;
    if (!JsonParser(body).parse(parsed)) {
        return {400, result(kResultInvalidParameter)};
    }
    ChromaState::Grid grid{};
    const bool has_frame = keyboardGrid(parsed, grid);

    if (method == "POST") {
        // Create without applying; the app sends PUT /effect with this id.
        const std::string id = newEffectId();
        if (has_frame) {
            if (stored_effects_.size() >= kMaxStoredEffects && !stored_order_.empty()) {
                stored_effects_.erase(stored_order_.front());
                stored_order_.erase(stored_order_.begin());
            }
            stored_effects_[id] = grid;
            stored_order_.push_back(id);
        }
        return {200, "{\"result\":0,\"id\":\"" + id + "\"}"};
    }

    if (has_frame) {
        apply(grid, session);
    }
    return {200, result(kResultSuccess)};
}

ChromaServer::Reply ChromaServer::effect(const std::string& method, const std::string& body) {
    Json parsed;
    if (!JsonParser(body).parse(parsed)) {
        return {400, result(kResultInvalidParameter)};
    }
    std::vector<std::string> ids;
    if (const Json* id = parsed.get("id"); id != nullptr && id->type == Json::Type::String) {
        ids.push_back(id->text);
    }
    if (const Json* list = parsed.get("ids"); list != nullptr && list->type == Json::Type::Array) {
        for (const Json& item : list->items) {
            if (item.type == Json::Type::String) {
                ids.push_back(item.text);
            }
        }
    }

    if (method == "DELETE") {
        for (const auto& id : ids) {
            stored_effects_.erase(id);
            stored_order_.erase(std::remove(stored_order_.begin(), stored_order_.end(), id),
                                stored_order_.end());
        }
        return {200, result(kResultSuccess)};
    }

    // PUT: apply. Effects for other devices were never stored, so the last
    // keyboard effect named is the one shown.
    const ChromaState::Grid* chosen = nullptr;
    for (const auto& id : ids) {
        if (auto it = stored_effects_.find(id); it != stored_effects_.end()) {
            chosen = &it->second;
        }
    }
    if (chosen != nullptr) {
        // Which session it came from is not in the request; credit the most
        // recently active one for `chroma status`.
        std::string session = "0";
        Clock::time_point latest{};
        for (const auto& [sid, s] : sessions_) {
            if (s.last_seen >= latest) {
                latest = s.last_seen;
                session = sid;
            }
        }
        apply(*chosen, session);
    }
    return {200, result(kResultSuccess)};
}

void ChromaServer::apply(const ChromaState::Grid& grid, const std::string& session) {
    Session& s = touch(session, Clock::now());
    s.sent_frame = true;
    active_ = true;
    state_->setGrid(grid);
    state_->noteFrameFrom(s.title);
    if (callbacks_.on_frame) {
        callbacks_.on_frame();
    }
}

ChromaServer::Session& ChromaServer::touch(const std::string& session, Clock::time_point now) {
    auto [it, inserted] = sessions_.try_emplace(session);
    if (inserted) {
        it->second.title = (session == "0") ? "unregistered app" : "app (session " + session + ")";
        state_->setSessions(sessions_.size());
    }
    it->second.last_seen = now;
    return it->second;
}

void ChromaServer::expire(Clock::time_point now) {
    bool changed = false;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now - it->second.last_seen > session_timeout_) {
            std::cout << "[Chroma] " << it->second.title << " went quiet; releasing it.\n";
            it = sessions_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        updateActivity();
    }
}

void ChromaServer::updateActivity() {
    state_->setSessions(sessions_.size());
    const bool any = std::any_of(sessions_.begin(), sessions_.end(),
                                 [](const auto& entry) { return entry.second.sent_frame; });
    if (active_ && !any) {
        active_ = false;
        state_->clear();
        if (callbacks_.on_idle) {
            callbacks_.on_idle();
        }
    }
}

std::string ChromaServer::newEffectId() {
    // Not a real UUID, but shaped like one, which is what clients expect.
    char buffer[40];
    const std::uint64_t n = next_effect_++;
    std::snprintf(buffer, sizeof(buffer), "%08x-0000-4000-8000-%012llx",
                  static_cast<unsigned>(next_session_ & 0xFFFFFFFFu), static_cast<unsigned long long>(n));
    return buffer;
}

}  // namespace kb::cfg
