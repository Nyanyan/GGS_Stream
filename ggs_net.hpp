/*
    GGS Stream

    @file ggs_net.hpp
        Telnet connection to GGS running on its own thread (login, framing, reconnection)
*/
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI // GDI's RoundRect / Ellipse would clash with Siv3D
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include "ggs_protocol.hpp"
#include "app_log.hpp"
#pragma comment(lib, "ws2_32.lib")

/*
    @brief where GGS messages come from (real server or the demo simulator)
*/
class MessageSource {
public:
    virtual ~MessageSource() = default;
    virtual void send(const std::string& command) = 0;
    virtual std::vector<ggs::Message> poll(uint64_t now_ms) = 0;
    virtual bool online() const = 0;
    virtual int epoch() const = 0; // increases on every (re)connection
    virtual std::string status() const = 0;
};

class GGSConnection : public MessageSource {
    std::string host;
    int port;
    std::string username;
    std::string password;
    std::vector<std::string> init_commands;

    std::thread worker;
    std::atomic<bool> stopping{ false };
    std::atomic<bool> is_online{ false };
    std::atomic<int> epoch_{ 0 };
    mutable std::mutex mtx;
    std::deque<std::string> out_queue;
    std::vector<ggs::Message> in_queue;
    std::string status_text = "not connected";

public:
    GGSConnection(std::string host_, int port_, std::string username_, std::string password_, std::vector<std::string> init_commands_)
        : host(std::move(host_)), port(port_), username(std::move(username_)), password(std::move(password_)), init_commands(std::move(init_commands_)) {}

    ~GGSConnection() override {
        stop();
    }

    void start() {
        stopping = false;
        worker = std::thread([this] { run(); });
    }

    void stop() {
        stopping = true;
        if (worker.joinable()) worker.join();
    }

    void send(const std::string& command) override {
        std::lock_guard<std::mutex> lock(mtx);
        out_queue.push_back(command);
    }

    std::vector<ggs::Message> poll(uint64_t) override {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<ggs::Message> res;
        res.swap(in_queue);
        return res;
    }

    bool online() const override { return is_online; }
    int epoch() const override { return epoch_; }

    std::string status() const override {
        std::lock_guard<std::mutex> lock(mtx);
        return status_text;
    }

private:
    static uint64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void set_status(const std::string& s) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            status_text = s;
        }
        app_log().write("INFO", s);
    }

    void sleep_interruptible(int ms) {
        for (int t = 0; t < ms && !stopping; t += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    bool open_socket(SOCKET& sock) {
        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0 || result == nullptr) {
            set_status("failed to resolve " + host + " (" + std::to_string(WSAGetLastError()) + ")");
            return false;
        }
        bool connected = false;
        for (addrinfo* ai = result; ai != nullptr && !connected && !stopping; ai = ai->ai_next) {
            sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (sock == INVALID_SOCKET) continue;
            u_long non_blocking = 1;
            ioctlsocket(sock, FIONBIO, &non_blocking);
            int r = connect(sock, ai->ai_addr, (int)ai->ai_addrlen);
            if (r == 0) {
                connected = true;
            } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                // wait up to 10 s in small steps so that stop() is not blocked
                for (int waited = 0; waited < 10000 && !stopping && !connected; waited += 200) {
                    fd_set wfds, efds;
                    FD_ZERO(&wfds);
                    FD_ZERO(&efds);
                    FD_SET(sock, &wfds);
                    FD_SET(sock, &efds);
                    timeval tv{ 0, 200 * 1000 };
                    int r = select(0, nullptr, &wfds, &efds, &tv);
                    if (r < 0 || FD_ISSET(sock, &efds)) break;
                    if (r > 0 && FD_ISSET(sock, &wfds)) connected = true;
                }
            }
            if (connected) {
                non_blocking = 0;
                ioctlsocket(sock, FIONBIO, &non_blocking);
                BOOL keepalive = TRUE;
                setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&keepalive, sizeof(keepalive));
            } else {
                closesocket(sock);
                sock = INVALID_SOCKET;
            }
        }
        freeaddrinfo(result);
        if (!connected) set_status("failed to connect to " + host + ":" + std::to_string(port));
        return connected;
    }

    // 1: data received, 0: timeout, -1: closed or error
    static int recv_some(SOCKET sock, int timeout_ms, std::string& out) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        int r = select(0, &rfds, nullptr, nullptr, &tv);
        if (r == 0) return 0;
        if (r < 0) return -1;
        char buf[8192];
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) return -1;
        out.assign(buf, n);
        return 1;
    }

    bool send_line(SOCKET sock, const std::string& line, bool secret = false) {
        std::string data = line + "\n"; // same line ending as the original client
        size_t sent = 0;
        while (sent < data.size()) {
            int n = ::send(sock, data.c_str() + sent, (int)(data.size() - sent), 0);
            if (n == SOCKET_ERROR) return false;
            sent += n;
        }
        app_log().write("SEND", secret ? "********" : line);
        return true;
    }

    // wait until the received text contains the keyword (or timeout); everything received is framed
    bool wait_for(SOCKET sock, ggs::MessageFramer& framer, const std::string& keyword, int timeout_ms, std::string& seen) {
        uint64_t deadline = now_ms() + timeout_ms;
        while (!stopping && now_ms() < deadline) {
            std::string chunk;
            int r = recv_some(sock, 100, chunk);
            if (r < 0) return false;
            if (r == 0) continue;
            framer.feed(chunk, now_ms());
            seen += ggs::to_lower(chunk);
            if (seen.size() > 8192) seen.erase(0, seen.size() - 8192);
            if (!keyword.empty() && seen.find(keyword) != std::string::npos) return true;
        }
        return true; // timeout is not fatal: continue as the original client did
    }

    void deliver(ggs::MessageFramer& framer) {
        std::vector<ggs::Message> msgs = framer.take();
        if (msgs.empty()) return;
        for (const auto& m : msgs) app_log().write("RECV", m.text());
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& m : msgs) in_queue.push_back(std::move(m));
    }

    void session(SOCKET sock) {
        ggs::MessageFramer framer;
        std::string seen;
        set_status("logging in as " + username);
        if (!wait_for(sock, framer, "login", 10000, seen)) return;
        if (!send_line(sock, username)) return;
        seen.clear();
        if (!wait_for(sock, framer, "password", 10000, seen)) return;
        if (!send_line(sock, password, true)) return;
        seen.clear();
        if (!wait_for(sock, framer, "ready", 3000, seen)) return;
        framer.take(); // greeting messages are not needed
        for (const auto& cmd : init_commands) {
            if (!send_line(sock, cmd)) return;
        }
        {
            std::lock_guard<std::mutex> lock(mtx);
            out_queue.clear(); // requests made while offline are re-sent by the state on reconnection
        }
        is_online = true;
        ++epoch_;
        set_status("online as " + username);
        while (!stopping) {
            std::deque<std::string> pending;
            {
                std::lock_guard<std::mutex> lock(mtx);
                pending.swap(out_queue);
            }
            for (const auto& cmd : pending) {
                if (!send_line(sock, cmd)) return;
            }
            std::string chunk;
            int r = recv_some(sock, 50, chunk);
            if (r < 0) {
                set_status("connection closed by server");
                return;
            }
            if (r > 0) framer.feed(chunk, now_ms());
            framer.tick(now_ms());
            deliver(framer);
        }
    }

    void run() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            set_status("failed to initialize Winsock");
            return;
        }
        int backoff_ms = 3000;
        while (!stopping) {
            set_status("connecting to " + host + ":" + std::to_string(port));
            SOCKET sock = INVALID_SOCKET;
            if (open_socket(sock)) {
                uint64_t started = now_ms();
                session(sock);
                is_online = false;
                shutdown(sock, SD_BOTH);
                closesocket(sock);
                if (now_ms() - started > 60000) backoff_ms = 3000;
            }
            if (stopping) break;
            set_status("disconnected, retrying in " + std::to_string(backoff_ms / 1000) + " s");
            sleep_interruptible(backoff_ms);
            backoff_ms = std::min(backoff_ms * 2, 30000);
        }
        WSACleanup();
    }
};

/*
    @brief the offline simulator as a message source
*/
#include "demo_server.hpp"

class DemoSource : public MessageSource {
    demo::DemoServer server;

public:
    DemoSource(int players, double speed, const std::string& tournament_id, uint64_t seed, uint64_t now_ms)
        : server(players, speed, tournament_id, seed) {
        server.start(now_ms);
    }

    void send(const std::string& command) override {
        app_log().write("SEND", command);
        server.send(command);
    }

    std::vector<ggs::Message> poll(uint64_t now_ms) override {
        std::vector<ggs::Message> msgs = server.poll(now_ms);
        for (const auto& m : msgs) app_log().write("RECV", m.text());
        return msgs;
    }

    bool online() const override { return true; }
    int epoch() const override { return 1; }
    std::string status() const override { return "demo mode (offline simulation)"; }
};
