/*
    GGS Stream

    @file ggs_protocol.hpp
        Message framing and parsing for the Generic Game Server (GGS) text protocol.
        Formats follow the GGS service sources (GameLib/Game.C, Match.C, TD/Tournament.C).
        This file does not depend on Siv3D.
*/
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include "common.hpp"

namespace ggs {

/*
    text helpers
*/
inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> res;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !std::isspace((unsigned char)s[j])) ++j;
        if (j > i) res.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return res;
}

inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

inline bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = (int)v;
    return true;
}

// remove vt100 escape sequences (in case vt100 mode is on)
inline std::string strip_ansi(const std::string& s) {
    if (s.find('\x1b') == std::string::npos) return s;
    std::string res;
    res.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            ++i;
            if (i < s.size() && s[i] == '[') {
                ++i;
                while (i < s.size() && !std::isalpha((unsigned char)s[i])) ++i;
            }
            continue;
        }
        res += s[i];
    }
    return res;
}

/*
    @brief one GGS message

    GGS sends a message as a header line followed by continuation lines starting with '|'.
    The leading '|' is removed from body lines.
*/
struct Message {
    std::string head;
    std::vector<std::string> body;

    std::string text() const {
        std::string res = head;
        for (const auto& line : body) {
            res += "\n|";
            res += line;
        }
        return res;
    }
};

/*
    @brief split a raw TCP stream into messages

    A message is complete when the next non-continuation line (or the READY prompt) arrives,
    or when no data has arrived for a while.
*/
class MessageFramer {
    std::string partial;
    Message current;
    bool has_current = false;
    std::vector<Message> ready;
    uint64_t last_feed_ms = 0;

public:
    void feed(const std::string& data, uint64_t now_ms) {
        last_feed_ms = now_ms;
        partial += data;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            on_line(line);
        }
    }

    void tick(uint64_t now_ms, uint64_t idle_ms = 500) {
        if (now_ms - last_feed_ms < idle_ms) return;
        if (!partial.empty()) {
            std::string line = partial;
            partial.clear();
            on_line(line);
        }
        flush();
    }

    std::vector<Message> take() {
        std::vector<Message> res;
        res.swap(ready);
        return res;
    }

private:
    void on_line(std::string line) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\0')) line.pop_back();
        line = strip_ansi(line);
        if (!line.empty() && line[0] == '|') {
            if (has_current) current.body.emplace_back(line.substr(1));
            return;
        }
        flush();
        std::string t = trim(line);
        if (t.empty() || t == "READY" || t == "ALERT") return;
        current = Message{ line, {} };
        has_current = true;
    }

    void flush() {
        if (has_current) {
            ready.emplace_back(std::move(current));
            current = Message{};
            has_current = false;
        }
    }
};

/*
    board position: bit (63 - cell) where cell = y * 8 + x (a1 = 0, h8 = 63)
    same layout as Egaroucid's Board
*/
struct Position {
    uint64_t black = 0;
    uint64_t white = 0;
    int to_move = -1; // BLACK / WHITE / -1 unknown
};

inline uint64_t cell_bit(int cell) {
    return 1ULL << (HW2_M1 - cell);
}

inline bool operator==(const Position& a, const Position& b) {
    return a.black == b.black && a.white == b.white && a.to_move == b.to_move;
}

/*
    @brief time in GGS HHMMSS format: [-][days.]HH:MM:SS / MM:SS / SS
*/
inline bool parse_hhmmss(std::string s, int& seconds) {
    s = trim(s);
    if (s.empty()) return false;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        s = s.substr(1);
    }
    long long days = 0;
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        int d;
        if (!parse_int(s.substr(0, dot), d)) return false;
        days = d;
        s = s.substr(dot + 1);
    }
    std::vector<int> parts;
    size_t start = 0;
    while (true) {
        size_t colon = s.find(':', start);
        std::string part = s.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        int v;
        if (!parse_int(part, v) || v < 0) return false;
        parts.push_back(v);
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    if (parts.empty() || parts.size() > 3) return false;
    long long total = 0;
    for (int v : parts) total = total * 60 + v;
    total += days * 86400;
    seconds = (int)(negative ? -total : total);
    return true;
}

/*
    @brief player / clock line in join & update messages
        "egrcd    (2556.5 *) 00:02,25:0//00:00,25:0"
*/
struct ClockInfo {
    bool valid = false;
    std::string name;
    double rating = 0.0;
    int color = -1;
    int seconds = 0;
};

inline bool parse_clock_line(const std::string& line, ClockInfo& out) {
    static const std::regex re(R"(^\s*(\S+)\s+\(\s*(-?[0-9.]+)\s+([*O])\s*\)\s+(\S+))");
    std::smatch m;
    if (!std::regex_search(line, m, re)) return false;
    ClockInfo res;
    res.name = m[1].str();
    parse_double(m[2].str(), res.rating);
    res.color = m[3].str() == "*" ? BLACK : WHITE;
    std::string clock = m[4].str();
    size_t cut = clock.find_first_of(",/");
    if (!parse_hhmmss(clock.substr(0, cut), res.seconds)) return false;
    res.valid = true;
    out = res;
    return true;
}

/*
    @brief move with optional evaluation and time: "f5/1.23/4.56", "PA/-2.00", "d3//0.51", "pass"
    an evaluation of exactly 0 is not sent by the server, so has_eval == false can also mean 0
*/
struct MoveInfo {
    bool valid = false;
    int ply = 0;
    bool pass = false;
    int cell = -1;
    bool has_eval = false;
    double eval = 0.0;
    std::string raw;
};

inline bool parse_move_token(const std::string& token, MoveInfo& out) {
    MoveInfo res;
    res.raw = token;
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t slash = token.find('/', start);
        parts.emplace_back(token.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    std::string mv = to_lower(parts[0]);
    if (mv == "pa" || mv == "pass" || mv == "ps") {
        res.pass = true;
    } else if (mv.size() == 2 && mv[0] >= 'a' && mv[0] <= 'h' && mv[1] >= '1' && mv[1] <= '8') {
        res.cell = (mv[1] - '1') * HW + (mv[0] - 'a');
    } else {
        return false;
    }
    if (parts.size() >= 2 && !parts[1].empty()) {
        double v;
        if (parse_double(parts[1], v)) {
            res.has_eval = true;
            res.eval = v;
        }
    }
    res.valid = true;
    out = res;
    return true;
}

inline bool parse_move_line(const std::string& line, MoveInfo& out) {
    static const std::regex re(R"(^\s*(\d+)\s*:\s*(\S+))");
    std::smatch m;
    if (!std::regex_search(line, m, re)) return false;
    MoveInfo res;
    if (!parse_move_token(m[2].str(), res)) return false;
    parse_int(m[1].str(), res.ply);
    out = res;
    return true;
}

/*
    @brief split ".12.0" into match id ".12" and game number 0
*/
inline bool split_game_id(const std::string& game_id, std::string& match_id, int& sub) {
    if (game_id.size() < 2 || game_id[0] != '.') return false;
    size_t dot = game_id.find('.', 1);
    if (dot == std::string::npos) {
        match_id = game_id;
        sub = -1;
        return true;
    }
    match_id = game_id.substr(0, dot);
    return parse_int(game_id.substr(dot + 1), sub);
}

inline long match_number(const std::string& match_id) {
    int v = 0;
    if (match_id.size() >= 2 && match_id[0] == '.' && parse_int(match_id.substr(1), v)) return v;
    return -1;
}

/*
    @brief /os: join / update message with a board
*/
struct BoardMessage {
    bool is_join = false;
    std::string game_id;
    std::string match_id;
    int sub = -1;
    std::string type;
    int move_count = -1;
    std::vector<Position> boards; // in message order; join with moves: [start, current]
    std::vector<MoveInfo> moves;  // ply >= 1, in message order
    ClockInfo clock[2];           // by color (last one in the message)
};

inline bool is_board_header(const std::vector<std::string>& words) {
    if (words.size() != 8) return false;
    for (int i = 0; i < 8; ++i) {
        if (words[i].size() != 1 || words[i][0] != 'A' + i) return false;
    }
    return true;
}

inline bool parse_board_message(const Message& msg, BoardMessage& out) {
    std::vector<std::string> head = tokenize(msg.head);
    if (head.size() < 3 || head[0] != "/os:" || (head[1] != "join" && head[1] != "update")) return false;
    BoardMessage res;
    res.is_join = head[1] == "join";
    res.game_id = head[2];
    if (!split_game_id(res.game_id, res.match_id, res.sub)) return false;
    if (head.size() >= 4) res.type = head[3];

    bool in_board = false;
    Position board;
    int rows_found = 0;
    bool pending_to_move = false;
    for (const std::string& line : msg.body) {
        std::vector<std::string> words = tokenize(line);
        if (words.empty()) continue;
        if (is_board_header(words)) {
            if (!in_board) {
                in_board = true;
                board = Position{};
                rows_found = 0;
            } else {
                in_board = false;
                if (rows_found == HW) {
                    res.boards.push_back(board);
                    pending_to_move = true;
                }
            }
            continue;
        }
        if (in_board) {
            int row;
            if (words.size() >= 9 && parse_int(words[0], row) && 1 <= row && row <= HW) {
                bool ok = true;
                for (int x = 0; x < HW; ++x) {
                    const std::string& c = words[1 + x];
                    int cell = (row - 1) * HW + x;
                    if (c == "*") board.black |= cell_bit(cell);
                    else if (c == "O") board.white |= cell_bit(cell);
                    else if (c != "-") ok = false;
                }
                if (ok) ++rows_found;
            }
            continue;
        }
        if (words.size() >= 3 && words[1] == "to" && words[2] == "move" && (words[0] == "*" || words[0] == "O")) {
            if (pending_to_move && !res.boards.empty()) {
                res.boards.back().to_move = words[0] == "*" ? BLACK : WHITE;
                pending_to_move = false;
            }
            continue;
        }
        if (words.size() >= 2 && words[1] == "move(s)") {
            parse_int(words[0], res.move_count);
            continue;
        }
        ClockInfo ci;
        if (parse_clock_line(line, ci)) {
            res.clock[ci.color] = ci;
            continue;
        }
        MoveInfo mi;
        if (parse_move_line(line, mi)) {
            if (mi.ply >= 1) res.moves.push_back(mi);
            continue;
        }
    }
    if (res.boards.empty() || res.boards.back().to_move == -1) return false;
    out = res;
    return true;
}

/*
    @brief /os: match list row
        "|  .2 2664 nyanyan  2562 egrcd       s8r14  R 0"
*/
struct MatchRow {
    std::string id;
    std::string player[2];
    std::string type;
    std::string flag; // R / U / T
};

inline bool is_match_id_token(const std::string& s) {
    if (s.size() < 2 || s[0] != '.') return false;
    for (size_t i = 1; i < s.size(); ++i) {
        if (!std::isdigit((unsigned char)s[i])) return false;
    }
    return true;
}

inline bool parse_match_row_words(const std::vector<std::string>& words, size_t i, MatchRow& out) {
    // i: index of the match id
    double tmp;
    MatchRow row;
    row.id = words[i];
    if (words.size() >= i + 7 && parse_double(words[i + 1], tmp)) {
        // .id rating name rating name type flag
        row.player[0] = words[i + 2];
        row.player[1] = words[i + 4];
        row.type = words[i + 5];
        row.flag = words[i + 6];
    } else if (words.size() >= i + 7) {
        // .id type flag rating name rating name (documented format)
        row.type = words[i + 1];
        row.flag = words[i + 2];
        row.player[0] = words[i + 4];
        row.player[1] = words[i + 6];
    } else {
        return false;
    }
    out = row;
    return true;
}

inline bool parse_match_list(const Message& msg, std::vector<MatchRow>& rows) {
    std::vector<std::string> head = tokenize(msg.head);
    if (head.size() < 2 || head[0] != "/os:" || head[1] != "match") return false;
    rows.clear();
    for (const std::string& line : msg.body) {
        std::vector<std::string> words = tokenize(line);
        for (size_t i = 0; i < words.size(); ++i) {
            if (is_match_id_token(words[i])) {
                MatchRow row;
                if (parse_match_row_words(words, i, row)) rows.push_back(row);
                break;
            }
        }
    }
    return true;
}

/*
    @brief "/os: + match .9 2544 lynx 2505 kitty s8r20 T"
           "/os: - match .65 2628 nyanyan 2616 egrcd s8r14 R +0.00  .83353"
*/
struct MatchEvent {
    bool start = false;
    MatchRow row;
    bool has_result = false;
    double result0 = 0.0; // result for row.player[0] (synchro: average of both games)
};

inline bool parse_match_event(const Message& msg, MatchEvent& out) {
    std::vector<std::string> words = tokenize(msg.head);
    if (words.size() < 4 || words[0] != "/os:" || (words[1] != "+" && words[1] != "-") || words[2] != "match") return false;
    if (!is_match_id_token(words[3])) return false;
    MatchEvent ev;
    ev.start = words[1] == "+";
    if (!parse_match_row_words(words, 3, ev.row)) return false;
    if (!ev.start && words.size() >= 11) {
        ev.has_result = parse_double(words[10], ev.result0);
    }
    out = ev;
    return true;
}

/*
    @brief "/os: end .31.1 ( nyanyan vs. egrcd ) +2.00"
*/
struct GameEnd {
    std::string game_id;
    std::string match_id;
    int sub = -1;
    std::string player[2];
    bool has_result = false;
    double result0 = 0.0; // result for player[0]
};

inline bool parse_game_end(const Message& msg, GameEnd& out) {
    std::vector<std::string> words = tokenize(msg.head);
    if (words.size() < 3 || words[0] != "/os:" || words[1] != "end") return false;
    GameEnd ge;
    ge.game_id = words[2];
    if (!split_game_id(ge.game_id, ge.match_id, ge.sub)) return false;
    if (words.size() >= 8 && words[3] == "(" && words[7] == ")") {
        ge.player[0] = words[4];
        ge.player[1] = words[6];
    }
    if (words.size() >= 9) ge.has_result = parse_double(words[8], ge.result0);
    out = ge;
    return true;
}

/*
    @brief tournament director messages
        ".tourney /td: starting round 3 of tournament 6"
        ".tourney /td: ending round 3 of tournament 6"
        ".tourney /td: tournament 6 is over. "
*/
inline int parse_round_event(const Message& msg, const std::string& tournament_id, bool starting) {
    static const std::regex re_start(R"(/td:\s+starting round (\d+) of tournament (\S+))");
    static const std::regex re_end(R"(/td:\s+ending round (\d+) of tournament (\S+))");
    std::smatch m;
    if (!std::regex_search(msg.head, m, starting ? re_start : re_end)) return -1;
    if (m[2].str() != tournament_id) return -1;
    int round;
    if (!parse_int(m[1].str(), round)) return -1;
    return round;
}

inline bool parse_tournament_over(const Message& msg, const std::string& tournament_id) {
    static const std::regex re(R"(/td:\s+tournament (\S+) is over)");
    std::smatch m;
    return std::regex_search(msg.head, m, re) && m[1].str() == tournament_id;
}

/*
    @brief rankings: "/td: rankings: tournament 6"
        "|  6.0 ( 6  0  0) { 12.34 } nyanyan  [0.1234]"
*/
struct RankRow {
    std::string name;
    double points = 0.0;
    int win = 0, draw = 0, loss = 0;
    bool has_record = false;
    double discs = 0.0;
    bool has_discs = false;
};

// "79.0 (62 34  4) {   1.48 } ymatioun [0.4768]"
inline bool parse_rank_row(const std::string& line, RankRow& out) {
    static const std::regex re_row(R"(^\s*(-?[0-9.]+)\s*\(\s*(\d+)\s+(\d+)\s+(\d+)\s*\)\s*(?:\{\s*([-+0-9.]+)\s*\}\s*)?(\S+))");
    std::smatch r;
    if (!std::regex_search(line, r, re_row)) return false;
    RankRow row;
    parse_double(r[1].str(), row.points);
    parse_int(r[2].str(), row.win);
    parse_int(r[3].str(), row.draw);
    parse_int(r[4].str(), row.loss);
    row.has_record = true;
    if (r[5].matched) row.has_discs = parse_double(r[5].str(), row.discs);
    row.name = r[6].str();
    out = row;
    return true;
}

inline bool parse_rankings(const Message& msg, const std::string& tournament_id, std::vector<RankRow>& rows) {
    static const std::regex re_head(R"(^/td:\s+rankings:\s+tournament\s+(\S+))");
    std::smatch m;
    if (!std::regex_search(msg.head, m, re_head) || m[1].str() != tournament_id) return false;
    rows.clear();
    for (const std::string& line : msg.body) {
        RankRow row;
        if (parse_rank_row(line, row)) {
            rows.push_back(row);
        } else {
            std::vector<std::string> words = tokenize(line);
            if (words.size() >= 4 && parse_double(words[0], row.points)) {
                row.name = words[words.size() - 2];
                rows.push_back(row);
            }
        }
    }
    return true;
}

/*
    @brief finger: "/td: finger: " + "|id        : 6" "|rounds    : 2/5" ... "|round 3 is being played"
*/
struct TournamentInfo {
    std::string id;
    int rounds_played = -1;
    int rounds_total = -1;
    int current_round = -1;
    bool playing = false;
    bool in_break = false;
    bool finished = false;
    std::vector<std::string> players;
    std::string type;
    std::string clock;
    int break_seconds = -1; // break between rounds
    int begins_in = -1;     // seconds until the next round (during a break)
};

inline bool parse_finger(const Message& msg, const std::string& tournament_id, TournamentInfo& out) {
    if (msg.head.find("/td:") == std::string::npos || msg.head.find("finger:") == std::string::npos) return false;
    TournamentInfo info;
    static const std::regex re_play(R"(round (\d+) is being played)");
    static const std::regex re_next(R"(round (\d+) begins in\s*(\S*))");
    for (const std::string& line : msg.body) {
        std::smatch m;
        if (std::regex_search(line, m, re_play)) {
            parse_int(m[1].str(), info.current_round);
            info.playing = true;
            continue;
        }
        if (std::regex_search(line, m, re_next)) {
            parse_int(m[1].str(), info.current_round);
            info.in_break = true;
            int sec;
            if (parse_hhmmss(m[2].str(), sec)) info.begins_in = sec;
            continue;
        }
        if (line.find("tournament has finished") != std::string::npos) {
            info.finished = true;
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (key == "id") {
            info.id = value;
        } else if (key == "rounds") {
            size_t slash = value.find('/');
            if (slash != std::string::npos) {
                parse_int(trim(value.substr(0, slash)), info.rounds_played);
                parse_int(trim(value.substr(slash + 1)), info.rounds_total);
            }
        } else if (key == "players") {
            // "<n> login login (swing) (bye) ..."
            int n;
            for (std::string w : tokenize(value)) {
                if (parse_int(w, n)) continue;
                if (w.size() >= 2 && w.front() == '(' && w.back() == ')') w = w.substr(1, w.size() - 2);
                if (w.empty() || w == "bye") continue;
                info.players.push_back(w);
            }
        } else if (key == "type") {
            info.type = value;
        } else if (key == "clock") {
            info.clock = value;
        } else if (key == "breaks") {
            int sec;
            if (parse_hhmmss(value, sec)) info.break_seconds = sec;
        }
    }
    if (info.id != tournament_id) return false;
    out = info;
    return true;
}

} // namespace ggs
