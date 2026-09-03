// sinoctl -- command line client for the Sinodragon daemon's control socket.
//
//   sinoctl profile magma
//   sinoctl brightness 40
//   sinoctl set 3 speed 0.9
//   sinoctl status
//
// Bind it to a compositor shortcut, call it from a script, or use it to poke
// a daemon that is running under systemd with no terminal attached.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "keyboard_configurator/control_server.hpp"

namespace {

std::string usage() {
    return "Usage: sinoctl [--socket <path>] <command> [args...]\n"
           "\n"
           "Commands are passed straight to the daemon. Try `sinoctl help`\n"
           "for the list it supports.\n"
           "\n"
           "Options:\n"
           "  -s, --socket <path>   Control socket (default: $XDG_RUNTIME_DIR/sinodragon.sock)\n"
           "  -h, --help            Show this help";
}

int connectTo(const std::string& path) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "sinoctl: socket path too long\n";
        return -1;
    }

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "sinoctl: socket(): " << std::strerror(errno) << '\n';
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "sinoctl: cannot reach the daemon at " << path << ": " << std::strerror(errno)
                  << "\nIs it running? Try: kb_configurator --daemon\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

bool writeAll(int fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
        } else if (!(n < 0 && errno == EINTR)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string socket_path = kb::cfg::defaultControlSocketPath();
    std::vector<std::string> words;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!words.empty()) {
            words.push_back(arg);  // everything after the command is its own
        } else if (arg == "-h" || arg == "--help") {
            std::cout << usage() << '\n';
            return 0;
        } else if (arg == "-s" || arg == "--socket") {
            if (i + 1 >= argc) {
                std::cerr << "sinoctl: " << arg << " needs a path\n";
                return 2;
            }
            socket_path = argv[++i];
        } else {
            words.push_back(arg);
        }
    }

    if (words.empty()) {
        std::cerr << usage() << '\n';
        return 2;
    }

    std::string command;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            command += ' ';
        }
        command += words[i];
    }

    const int fd = connectTo(socket_path);
    if (fd < 0) {
        return 1;
    }

    if (!writeAll(fd, command + "\n")) {
        std::cerr << "sinoctl: failed to send command\n";
        ::close(fd);
        return 1;
    }
    ::shutdown(fd, SHUT_WR);

    std::string reply;
    char chunk[1024];
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n > 0) {
            reply.append(chunk, chunk + static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(fd);

    if (!reply.empty()) {
        std::cout << reply;
        if (reply.back() != '\n') {
            std::cout << '\n';
        }
    }

    // Let scripts branch on failure without parsing prose.
    if (reply.rfind("Unknown command", 0) == 0 || reply.rfind("Usage:", 0) == 0 ||
        reply.rfind("No such profile", 0) == 0 || reply.rfind("Invalid ", 0) == 0) {
        return 1;
    }
    return 0;
}
