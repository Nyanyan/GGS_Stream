/*
    GGS Stream

    @file app_log.hpp
        Thread-safe logger (file + recent lines for the debug overlay)
*/
#pragma once
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>

class AppLog {
    std::mutex mtx;
    std::ofstream ofs;
    std::deque<std::string> recent;
    static constexpr size_t MAX_RECENT = 400;

    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tm;
        localtime_s(&tm, &t);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
        return buf;
    }

public:
    void open(const std::string& path) {
        std::lock_guard<std::mutex> lock(mtx);
        ofs.open(path, std::ios::app);
    }

    void write(const std::string& tag, const std::string& text) {
        std::lock_guard<std::mutex> lock(mtx);
        std::string ts = timestamp();
        size_t start = 0;
        while (start <= text.size()) {
            size_t nl = text.find('\n', start);
            std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            std::string entry = ts + " " + tag + " " + line;
            if (ofs) ofs << entry << '\n';
            recent.push_back(entry);
            if (recent.size() > MAX_RECENT) recent.pop_front();
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        if (ofs) ofs.flush();
    }

    std::vector<std::string> tail(size_t n) {
        std::lock_guard<std::mutex> lock(mtx);
        size_t from = recent.size() > n ? recent.size() - n : 0;
        return std::vector<std::string>(recent.begin() + from, recent.end());
    }
};

inline AppLog& app_log() {
    static AppLog log;
    return log;
}
