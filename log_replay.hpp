/*
    GGS Stream

    @file log_replay.hpp
        Replays a log written by AppLog (logs/*.log) as if the messages came from GGS,
        e.g. to reproduce what happened during a tournament.
        This file does not depend on Siv3D.
*/
#pragma once
#include <fstream>
#include "ggs_net.hpp"

struct LogLine {
    uint64_t t_ms = 0; // time of day in milliseconds (continues past midnight)
    std::string text;
};

// "HH:MM:SS.mmm TAG text"
inline bool parse_log_line(const std::string& line, std::string& tag, LogLine& out) {
    if (line.size() < 13 || line[2] != ':' || line[5] != ':' || line[8] != '.' || line[12] != ' ') return false;
    int h, m, s, ms;
    if (!ggs::parse_int(line.substr(0, 2), h) || !ggs::parse_int(line.substr(3, 2), m) ||
        !ggs::parse_int(line.substr(6, 2), s) || !ggs::parse_int(line.substr(9, 3), ms)) return false;
    size_t sp = line.find(' ', 13);
    tag = line.substr(13, sp == std::string::npos ? std::string::npos : sp - 13);
    out.text = sp == std::string::npos ? std::string() : line.substr(sp + 1);
    out.t_ms = (((uint64_t)h * 60 + m) * 60 + s) * 1000 + ms;
    return true;
}

// received lines of a log file in time order
inline std::vector<LogLine> load_log(const std::string& path) {
    constexpr uint64_t DAY_MS = 24ULL * 3600 * 1000;
    std::vector<LogLine> res;
    std::ifstream ifs(path);
    std::string line, tag;
    uint64_t day = 0, last = 0;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        LogLine l;
        if (!parse_log_line(line, tag, l) || tag != "RECV") continue;
        if (!res.empty() && l.t_ms + DAY_MS / 2 < last) day += DAY_MS; // the log went past midnight
        last = l.t_ms;
        l.t_ms += day;
        res.push_back(l);
    }
    return res;
}

class LogReplaySource : public MessageSource {
    std::vector<LogLine> lines;
    size_t next = 0;
    uint64_t start_ms;
    double speed;
    ggs::MessageFramer framer;

public:
    LogReplaySource(const std::string& path, double speed_, uint64_t now_ms)
        : lines(load_log(path)), start_ms(now_ms), speed(std::max(0.1, speed_)) {}

    bool empty() const { return lines.empty(); }

    void send(const std::string&) override {} // nothing to answer requests

    std::vector<ggs::Message> poll(uint64_t now_ms) override {
        if (!lines.empty()) {
            uint64_t elapsed = (uint64_t)((now_ms - start_ms) * speed);
            uint64_t t0 = lines.front().t_ms;
            std::string chunk;
            while (next < lines.size() && lines[next].t_ms - t0 <= elapsed) {
                chunk += lines[next].text;
                chunk += '\n';
                ++next;
            }
            if (!chunk.empty()) framer.feed(chunk, now_ms);
        }
        framer.tick(now_ms, 300);
        return framer.take();
    }

    bool online() const override { return true; }
    int epoch() const override { return 1; }

    std::string status() const override {
        return "log replay " + std::to_string(next) + " / " + std::to_string(lines.size()) + " lines";
    }
};
