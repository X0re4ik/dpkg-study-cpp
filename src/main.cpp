#include <libpq-fe.h>
#include <yaml-cpp/yaml.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}

    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int get() const {
        return fd_;
    }

private:
    int fd_;
};

std::uint16_t load_port(const std::string& config_path) {
    const YAML::Node config = YAML::LoadFile(config_path);

    if (!config.IsMap()) {
        throw std::runtime_error(
            "Config must be a YAML mapping"
        );
    }

    if (!config["port"] || !config["port"].IsScalar()) {
        throw std::runtime_error(
            "Config must contain a scalar 'port' field"
        );
    }

    const int port = config["port"].as<int>();

    if (port < 1 || port > 65535) {
        throw std::runtime_error(
            "Port must be between 1 and 65535"
        );
    }

    return static_cast<std::uint16_t>(port);
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;

    while (sent < data.size()) {
        const auto n = ::send(
            fd,
            data.data() + sent,
            data.size() - sent,
            MSG_NOSIGNAL
        );

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(n);
    }

    return true;
}

int run_server(std::uint16_t port) {
    Socket listener(::socket(AF_INET, SOCK_STREAM, 0));

    if (listener.get() < 0) {
        std::perror("socket");
        return 1;
    }

    int reuse = 1;

    if (::setsockopt(
            listener.get(),
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        ) < 0) {
        std::perror("setsockopt");
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(
            listener.get(),
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) < 0) {
        std::perror("bind");
        return 1;
    }

    if (::listen(listener.get(), 16) < 0) {
        std::perror("listen");
        return 1;
    }

    std::cout
        << "Listening on http://127.0.0.1:"
        << port << '\n'
        << std::flush;

    for (;;) {
        Socket client(
            ::accept(listener.get(), nullptr, nullptr)
        );

        if (client.get() < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::perror("accept");
            return 1;
        }

        timeval timeout{5, 0};

        if (::setsockopt(
                client.get(),
                SOL_SOCKET,
                SO_RCVTIMEO,
                &timeout,
                sizeof(timeout)
            ) < 0 ||
            ::setsockopt(
                client.get(),
                SOL_SOCKET,
                SO_SNDTIMEO,
                &timeout,
                sizeof(timeout)
            ) < 0) {
            std::perror("setsockopt timeout");
            continue;
        }

        std::string request;
        char buffer[1024];
        bool complete = false;

        while (request.size() < 8192) {
            const auto n = ::recv(
                client.get(),
                buffer,
                sizeof(buffer),
                0
            );

            if (n < 0 && errno == EINTR) {
                continue;
            }

            if (n <= 0) {
                break;
            }

            request.append(
                buffer,
                static_cast<std::size_t>(n)
            );

            if (request.find("\r\n\r\n") != std::string::npos) {
                complete = true;
                break;
            }
        }

        if (!complete) {
            continue;
        }

        const bool get = request.rfind("GET ", 0) == 0;

        const std::string body = get
            ? "PostgreSQL client library (libpq) version: "
                + std::to_string(PQlibVersion()) + "\n"
            : "Only GET is supported\n";

        const std::string response =
            std::string("HTTP/1.1 ") +
            (get
                ? "200 OK\r\n"
                : "405 Method Not Allowed\r\nAllow: GET\r\n") +
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(body.size()) +
            "\r\n\r\n" +
            body;

        if (!send_all(client.get(), response)) {
            std::cerr << "Response could not be sent\n";
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3 ||
        std::string_view(argv[1]) != "--config") {
        std::cerr
            << "Usage: " << argv[0]
            << " --config <path/to/config.yaml>\n";

        return 2;
    }

    try {
        const auto port = load_port(argv[2]);
        return run_server(port);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}