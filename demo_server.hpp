/*
    GGS Stream

    @file demo_server.hpp
        Offline simulator of a GGS round-robin synchro tournament.
        It produces raw text in the same format as GGS (/os and /td), so the real parser is exercised
        without connecting to the server.
        This file does not depend on Siv3D.
*/
#pragma once
#include <random>
#include <cstdio>
#include <bit>
#include "stream_model.hpp"

namespace demo {

using ggs::Position;

template <class... Args>
std::string format(const char* f, Args... args) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), f, args...);
    return buf;
}

// GGS HHMMSS with width 5
inline std::string hhmmss(int s) {
    bool negative = s < 0;
    if (negative) s = -s;
    int h = s / 3600, m = s / 60 % 60, sec = s % 60;
    std::string res = negative ? "-" : "";
    if (h) res += format("%02d:", h);
    res += format("%02d:%02d", m, sec);
    while (res.size() < 5) res = " " + res;
    return res;
}

inline int final_score(uint64_t me, uint64_t op) {
    int b = std::popcount(me), w = std::popcount(op);
    int e = HW2 - b - w;
    int diff = b - w;
    if (diff > 0) diff += e;
    else if (diff < 0) diff -= e;
    return diff;
}

// exact endgame search (negamax, alpha-beta)
inline int solve(uint64_t me, uint64_t op, int alpha, int beta, bool passed) {
    uint64_t legal = calc_legal(me, op);
    if (legal == 0) {
        if (passed) return final_score(me, op);
        return -solve(op, me, -beta, -alpha, true);
    }
    int best = -HW2 - 1;
    Flip flip;
    for (uint64_t l = legal; l; l &= l - 1) {
        int place = std::countr_zero(l);
        flip.calc_flip(me, op, (uint_fast8_t)place);
        int v = -solve(op ^ flip.flip, me ^ flip.flip ^ (1ULL << place), -beta, -alpha, false);
        if (v > best) {
            best = v;
            if (v > alpha) {
                alpha = v;
                if (alpha >= beta) break;
            }
        }
    }
    return best;
}

// rough evaluation in disc units from color's point of view
inline double heuristic(const Position& p, int color) {
    uint64_t me = color == BLACK ? p.black : p.white;
    uint64_t op = color == BLACK ? p.white : p.black;
    constexpr uint64_t corners = 0x8100000000000081ULL;
    constexpr uint64_t x_squares = 0x0042000000004200ULL;
    double phase = std::popcount(me | op) / 64.0;
    double c = std::popcount(me & corners) - std::popcount(op & corners);
    double x = std::popcount(me & x_squares) - std::popcount(op & x_squares);
    double mob = std::popcount(calc_legal(me, op)) - std::popcount(calc_legal(op, me));
    double disc = std::popcount(me) - std::popcount(op);
    return 4.0 * c - 1.5 * x + 0.8 * mob + disc * phase * phase * 0.7;
}

inline Position initial_position() {
    Position p;
    p.white = ggs::cell_bit(27) | ggs::cell_bit(36); // d4 e5
    p.black = ggs::cell_bit(28) | ggs::cell_bit(35); // e4 d5
    p.to_move = BLACK;
    return p;
}

class DemoServer {
    struct Player {
        std::string name;
        double rating;
        double temperature; // move choice noise
        double noise;       // evaluation noise
        bool upper;         // sends moves in upper case
        double points = 0.0;
        int win = 0, draw = 0, loss = 0;
        double discs = 0.0;
    };

    struct Game {
        std::string id;
        int player[2]; // by color
        Position start, pos;
        std::vector<std::string> tokens;
        double clock[2] = { 0.0, 0.0 };
        double bias[2] = { 0.0, 0.0 };
        double think = 0.0;
        bool finished = false;
        double result_black = 0.0;
    };

    struct Match {
        int number;
        int p[2];
        Game g[2];
        bool finished = false;
        bool watched = false;
        double step_end = 0.0;
        double result_p0 = 0.0;
    };

    enum class Phase { Playing, Break, Over };

    std::mt19937 rng;
    std::string tid;
    double speed;
    std::vector<Player> players;
    std::vector<std::vector<std::pair<int, int>>> schedule;
    int round = 0; // 1-based, current or last played
    int rounds_played = 0;
    Phase phase = Phase::Playing;
    double phase_end = 0.0;
    double all_finished_at = -1.0;
    std::vector<Match> matches;
    int next_match_number = 117;
    uint64_t t0_ms = 0;
    double now = 0.0;
    std::string outgoing;
    ggs::MessageFramer framer;
    static constexpr double CLOCK_SECONDS = 10 * 60;
    static constexpr int RANDOM_DISCS = 18;

public:
    DemoServer(int n_players, double speed_, const std::string& tournament_id, uint64_t seed)
        : rng((uint32_t)seed), tid(tournament_id), speed(std::max(0.1, speed_)) {
        static const char* names[] = {
            "Kurogane", "shiro", "Edgewise", "parity", "Mobility", "zebra42", "corner", "Tempo",
            "Frontier", "quiet", "stable", "Wedge", "Swindle", "Gambit", "Xsquare", "Kifu",
            "Nyanko", "Hikari", "Yami", "Tobi", "Kirin", "Sora", "Umi", "Kaze"
        };
        n_players = std::max(2, n_players + (n_players & 1));
        std::vector<int> idx(std::size(names));
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
        std::shuffle(idx.begin(), idx.end(), rng);
        for (int i = 0; i < n_players; ++i) {
            Player p;
            p.name = i < (int)idx.size() ? names[idx[i]] : format("bot%d", i);
            p.rating = 1800 + uniform(0.0, 900.0);
            p.temperature = uniform(0.3, 2.5);
            p.noise = uniform(0.5, 2.5);
            p.upper = uniform(0.0, 1.0) < 0.5;
            players.push_back(p);
        }
        // circle method round robin
        int n = n_players;
        std::vector<int> circle(n);
        for (int i = 0; i < n; ++i) circle[i] = i;
        for (int r = 0; r < n - 1; ++r) {
            std::vector<std::pair<int, int>> pairs;
            for (int i = 0; i < n / 2; ++i) {
                int a = circle[i], b = circle[n - 1 - i];
                if (r % 2) std::swap(a, b);
                pairs.emplace_back(a, b);
            }
            schedule.push_back(pairs);
            std::rotate(circle.begin() + 1, circle.end() - 1, circle.end());
        }
    }

    void start(uint64_t now_ms) {
        t0_ms = now_ms;
        now = 0.0;
        start_round(false);
        // start in the middle of the round so that the client has to replay the joined games
        for (auto& m : matches) {
            int steps = (int)uniform(0.0, 18.0);
            for (int i = 0; i < steps && !m.finished; ++i) step_match(m, false);
            schedule_step(m);
        }
    }

    void send(const std::string& line) {
        std::vector<std::string> words = ggs::tokenize(line);
        if (words.empty()) return;
        if (words[0] == "ts" && words.size() >= 2 && words[1] == "match") {
            emit_match_list();
        } else if ((words[0] == "tell" || words[0] == "t") && words.size() >= 4 && words[1] == "/os" && words[2] == "watch" && words[3] == "+") {
            for (size_t i = 4; i < words.size(); ++i) watch(words[i]);
        } else if (words[0] == "t" && words.size() >= 4 && words[1] == "/td") {
            if (words[3] != tid) return;
            if (words[2] == "r") emit_rankings();
            if (words[2] == "f") emit_finger();
        }
    }

    std::vector<ggs::Message> poll(uint64_t now_ms) {
        now = (now_ms - t0_ms) / 1000.0;
        if (phase == Phase::Playing) {
            for (auto& m : matches) {
                if (!m.finished && now >= m.step_end) {
                    step_match(m, true);
                    schedule_step(m);
                }
            }
            bool all = std::all_of(matches.begin(), matches.end(), [](const Match& m) { return m.finished; });
            if (all) {
                if (all_finished_at < 0) all_finished_at = now;
                if (now - all_finished_at >= 2.0) end_round();
            }
        } else if (phase == Phase::Break) {
            if (now >= phase_end) start_round(true);
        }
        // deliver in random chunks to exercise the framer
        uint64_t real_ms = now_ms;
        while (!outgoing.empty()) {
            size_t n = std::min(outgoing.size(), (size_t)uniform(40.0, 900.0));
            framer.feed(outgoing.substr(0, n), real_ms);
            outgoing.erase(0, n);
        }
        framer.tick(real_ms, 300);
        return framer.take();
    }

    int player_count() const { return (int)players.size(); }

    double break_seconds() const { return std::max(10.0, 60.0 / speed); }

private:
    double uniform(double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(rng);
    }

    double normal(double sd) {
        return std::normal_distribution<double>(0.0, sd)(rng);
    }

    void emit(const std::string& head, const std::vector<std::string>& body) {
        outgoing += head + "\r\n";
        for (const auto& line : body) outgoing += "|" + line + "\r\n";
        if (uniform(0.0, 1.0) < 0.8) outgoing += "READY\r\n";
    }

    Position random_start() {
        while (true) {
            Position p = initial_position();
            bool ok = true;
            while (std::popcount(p.black | p.white) < RANDOM_DISCS) {
                uint64_t legal = stream::legal_moves(p);
                if (legal == 0) {
                    ok = false;
                    break;
                }
                int k = (int)uniform(0.0, (double)std::popcount(legal));
                uint64_t l = legal;
                for (int i = 0; i < k; ++i) l &= l - 1;
                int cell = HW2_M1 - std::countr_zero(l);
                stream::apply_move(p, false, cell);
            }
            if (ok && stream::legal_moves(p)) return p;
        }
    }

    void start_round(bool announce) {
        if (round >= (int)schedule.size()) {
            phase = Phase::Over;
            return;
        }
        ++round;
        phase = Phase::Playing;
        all_finished_at = -1.0;
        matches.clear();
        if (announce) emit(format(".tourney /td: starting round %d of tournament %s", round, tid.c_str()), {});
        for (auto [a, b] : schedule[round - 1]) {
            Match m;
            m.number = next_match_number++;
            m.p[0] = a;
            m.p[1] = b;
            Position start = random_start();
            for (int i = 0; i < 2; ++i) {
                Game& g = m.g[i];
                g.id = format(".%d.%d", m.number, i);
                g.player[BLACK] = i == 0 ? a : b;
                g.player[WHITE] = i == 0 ? b : a;
                g.start = g.pos = start;
                g.clock[0] = g.clock[1] = CLOCK_SECONDS;
            }
            schedule_step(m);
            matches.push_back(m);
        }
        // unrelated chat that the client must ignore
        emit(".chat kibitzer: good luck everyone", {});
    }

    void end_round() {
        rounds_played = round;
        std::vector<std::string> body;
        std::string line = format("round %3d:", round);
        for (const auto& m : matches) {
            line += format(" %s-%s %+.1f", players[m.p[0]].name.c_str(), players[m.p[1]].name.c_str(), m.result_p0);
        }
        body.push_back(line);
        emit(format(".tourney /td: ending round %d of tournament %s", round, tid.c_str()), body);
        if (round >= (int)schedule.size()) {
            phase = Phase::Over;
            emit(format(".tourney /td: tournament %s is over. ", tid.c_str()),
                { format("tell /td r %s for final rankings, or", tid.c_str()), format("tell /td st %s for game-by-game results", tid.c_str()) });
        } else {
            phase = Phase::Break;
            phase_end = now + break_seconds();
        }
    }

    double think_time(const Game& g) {
        uint64_t legal = stream::legal_moves(g.pos);
        if (std::popcount(legal) <= 1) return uniform(0.3, 1.2) / speed;
        int empties = HW2 - std::popcount(g.pos.black | g.pos.white);
        double t = std::exp(uniform(0.5, 2.9)) * (empties > 12 ? 1.0 : 0.6);
        t = std::min(t, g.clock[g.pos.to_move] * 0.25 + 0.5);
        return t / speed;
    }

    void schedule_step(Match& m) {
        double t = 0.0;
        for (auto& g : m.g) {
            if (g.finished) continue;
            g.think = think_time(g);
            t = std::max(t, g.think);
        }
        m.step_end = now + t;
    }

    // one synchro step: every unfinished game gets one move, then observers get both updates
    void step_match(Match& m, bool notify) {
        bool ended[2] = { false, false };
        for (int i = 0; i < 2; ++i) {
            Game& g = m.g[i];
            if (g.finished) continue;
            play_move(g);
            if (stream::is_game_over(g.pos)) {
                g.finished = true;
                g.result_black = stream::score_black(g.pos);
                ended[i] = true;
            }
        }
        if (notify && m.watched) {
            for (int i = 0; i < 2; ++i) {
                if (!m.g[i].finished || ended[i]) emit_update(m.g[i]);
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (!ended[i]) continue;
            const Game& g = m.g[i];
            double r0 = g.player[BLACK] == m.p[0] ? g.result_black : -g.result_black;
            if (notify && m.watched) {
                emit(format("/os: end %s ( %s vs. %s ) %+.2f", g.id.c_str(), players[m.p[0]].name.c_str(), players[m.p[1]].name.c_str(), r0), {});
            }
        }
        if (m.g[0].finished && m.g[1].finished && !m.finished) finish_match(m, notify);
    }

    void finish_match(Match& m, bool notify) {
        m.finished = true;
        double r[2];
        for (int i = 0; i < 2; ++i) {
            const Game& g = m.g[i];
            r[i] = g.player[BLACK] == m.p[0] ? g.result_black : -g.result_black;
        }
        m.result_p0 = (r[0] + r[1]) / 2.0;
        Player& a = players[m.p[0]];
        Player& b = players[m.p[1]];
        if (m.result_p0 > 0) {
            a.points += 1; ++a.win; ++b.loss;
        } else if (m.result_p0 < 0) {
            b.points += 1; ++b.win; ++a.loss;
        } else {
            a.points += 0.5; b.points += 0.5; ++a.draw; ++b.draw;
        }
        a.discs += m.result_p0;
        b.discs -= m.result_p0;
        if (notify && m.watched) {
            emit(format("/os: - match .%d %.0f %s %.0f %s s8r%d T %+.2f  .%d", m.number, a.rating, a.name.c_str(), b.rating, b.name.c_str(), RANDOM_DISCS, m.result_p0, 90000 + m.number), {});
        }
    }

    void play_move(Game& g) {
        int mover = g.pos.to_move;
        Player& pl = players[g.player[mover]];
        uint64_t legal = stream::legal_moves(g.pos);
        double used = std::max(0.05, g.think);
        g.clock[mover] -= used;
        std::string token;
        if (legal == 0) {
            token = pl.upper ? "PA" : "pa";
            if (uniform(0.0, 1.0) < 0.5) token += format("/%.2f", normal(6.0)); // meaningless evaluation sent with a pass
            stream::apply_move(g.pos, true, -1);
            g.tokens.push_back(token);
            return;
        }
        int empties = HW2 - std::popcount(g.pos.black | g.pos.white);
        uint64_t me = mover == BLACK ? g.pos.black : g.pos.white;
        uint64_t op = mover == BLACK ? g.pos.white : g.pos.black;
        int best_cell = -1;
        double best_score = -1e9;
        double value = 0.0;
        bool exact = empties <= 10;
        for (uint64_t l = legal; l; l &= l - 1) {
            int place = std::countr_zero(l);
            int cell = HW2_M1 - place;
            Position next = g.pos;
            stream::apply_move(next, false, cell);
            double score;
            if (exact) {
                Flip flip;
                flip.calc_flip(me, op, (uint_fast8_t)place);
                score = -solve(op ^ flip.flip, me ^ flip.flip ^ (1ULL << place), -HW2, HW2, false);
            } else {
                score = heuristic(next, mover);
            }
            double noisy = score + normal(exact ? pl.temperature * 0.5 : pl.temperature);
            if (noisy > best_score) {
                best_score = noisy;
                best_cell = cell;
                value = score;
            }
        }
        g.bias[mover] = g.bias[mover] * 0.7 + normal(pl.noise);
        double eval = value + (exact ? 0.0 : g.bias[mover]);
        std::string mv = format("%c%d", 'a' + best_cell % HW, best_cell / HW + 1);
        if (pl.upper) mv[0] = (char)std::toupper(mv[0]);
        token = mv;
        bool forced = std::popcount(legal) == 1;
        // the server omits an evaluation of 0; forced moves are often sent without one
        bool send_eval = !forced && std::abs(eval) >= 0.005;
        if (send_eval) token += format("/%.2f", eval);
        if (used >= 0.005) token += format("%s/%.2f", send_eval ? "" : "/", used);
        stream::apply_move(g.pos, false, best_cell);
        g.tokens.push_back(token);
    }

    std::string clock_line(const Game& g, int color, bool setting) const {
        const Player& p = players[g.player[color]];
        std::string clock = setting ? hhmmss((int)CLOCK_SECONDS) + "//02:00"
                                    : hhmmss((int)std::floor(g.clock[color])) + ",0:0//02:00,0:0";
        return format("%-8s (%6.1f %c) %s", p.name.c_str(), p.rating, color == BLACK ? '*' : 'O', clock.c_str());
    }

    static void board_lines(const Position& p, std::vector<std::string>& out) {
        out.push_back("   A B C D E F G H");
        for (int y = 0; y < HW; ++y) {
            std::string line = format("%2d", y + 1);
            for (int x = 0; x < HW; ++x) {
                uint64_t bit = ggs::cell_bit(y * HW + x);
                line += (p.black & bit) ? " *" : (p.white & bit) ? " O" : " -";
            }
            line += format(" %-2d", y + 1);
            out.push_back(line);
        }
        out.push_back("   A B C D E F G H");
        out.push_back("");
        out.push_back(p.to_move == BLACK ? "* to move" : "O to move");
    }

    void emit_update(const Game& g) {
        std::vector<std::string> body;
        int n = (int)g.tokens.size();
        body.push_back(format("%3d: %s", n, n ? g.tokens.back().c_str() : "PASS"));
        body.push_back(clock_line(g, BLACK, false));
        body.push_back(clock_line(g, WHITE, false));
        body.push_back("");
        board_lines(g.pos, body);
        body.push_back("");
        emit(format("/os: update %s s8r%d K?", g.id.c_str(), RANDOM_DISCS), body);
    }

    void emit_join(const Game& g) {
        std::vector<std::string> body;
        int n = (int)g.tokens.size();
        body.push_back(format("%d move(s)", n));
        if (n > 0) {
            body.push_back(clock_line(g, BLACK, true));
            body.push_back(clock_line(g, WHITE, true));
            body.push_back("");
            board_lines(g.start, body);
            for (int i = 0; i + 1 < n; ++i) body.push_back(format("%3d: %s", i + 1, g.tokens[i].c_str()));
        }
        body.push_back(format("%3d: %s", n, n ? g.tokens.back().c_str() : "PASS"));
        body.push_back(clock_line(g, BLACK, false));
        body.push_back(clock_line(g, WHITE, false));
        body.push_back("");
        board_lines(g.pos, body);
        body.push_back("");
        emit(format("/os: join %s s8r%d K?", g.id.c_str(), RANDOM_DISCS), body);
    }

    void watch(const std::string& id) {
        for (auto& m : matches) {
            if (format(".%d", m.number) != id || m.finished) continue;
            m.watched = true;
            emit_join(m.g[0]);
            emit_join(m.g[1]);
            return;
        }
        emit("/os: watch + ERR not found: " + id, {});
    }

    void emit_match_list() {
        std::vector<std::string> body;
        if (phase == Phase::Playing) {
            for (const auto& m : matches) {
                if (m.finished) continue;
                const Player& a = players[m.p[0]];
                const Player& b = players[m.p[1]];
                body.push_back(format("%4s %4.0f %-8s %4.0f %-8s s8r%d  T 0", format(".%d", m.number).c_str(), a.rating, a.name.c_str(), b.rating, b.name.c_str(), RANDOM_DISCS));
            }
        }
        // games outside of the tournament
        body.push_back(format("%4s 1712 guest1   1650 guest2   8        R 2", ".98"));
        body.push_back(format("%4s 2210 %-8s 2195 lurker   s8r20  U 1", ".99", players[0].name.c_str()));
        emit(format("/os: match %d/%d", (int)body.size(), (int)body.size()), body);
    }

    void emit_rankings() {
        std::vector<int> order(players.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if (players[a].points != players[b].points) return players[a].points > players[b].points;
            return players[a].discs > players[b].discs;
        });
        std::vector<std::string> body;
        int played = std::max(1, rounds_played);
        for (int i : order) {
            const Player& p = players[i];
            body.push_back(format("%4.1f (%2d %2d %2d) { %6.2f } %8s [%.4f]", p.points, p.win, p.draw, p.loss, p.discs / played, p.name.c_str(), uniform(0.0, 1.0)));
        }
        emit(format("/td: rankings: tournament %s", tid.c_str()), body);
    }

    void emit_finger() {
        std::vector<std::string> body;
        body.push_back("id        : " + tid);
        body.push_back("joining   : closed");
        body.push_back("director  : demo");
        body.push_back("service   : /os");
        body.push_back(format("rounds    : %d/%d", rounds_played, (int)schedule.size()));
        body.push_back("style     : rrobin");
        body.push_back("clock     : 10:00//02:00");
        body.push_back(format("type      : s8r%d", RANDOM_DISCS));
        int b = (int)break_seconds();
        body.push_back(format("breaks    : %2d:%02d", b / 60, b % 60));
        std::string pl = format("players   : %d ", (int)players.size());
        for (const auto& p : players) pl += p.name + " ";
        body.push_back(pl);
        if (phase == Phase::Playing) body.push_back(format("round %d is being played ", round));
        else if (phase == Phase::Break) {
            int left = std::max(0, (int)std::ceil(phase_end - now));
            body.push_back(format("round %d begins in %2d:%02d", round + 1, left / 60, left % 60));
        }
        else body.push_back("tournament has finished");
        emit("/td: finger: ", body);
    }
};

} // namespace demo
