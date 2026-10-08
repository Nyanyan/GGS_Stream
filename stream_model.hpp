/*
    GGS Stream

    @file stream_model.hpp
        Tournament / match / game state built from GGS messages.
        This file does not depend on Siv3D.
*/
#pragma once
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <algorithm>
#include <cmath>
#include "board.hpp"
#include "ggs_protocol.hpp"

namespace stream {

using ggs::Position;
using ggs::MoveInfo;

constexpr uint64_t WATCH_RETRY_MS = 6000;
constexpr uint64_t MATCH_LIST_INTERVAL_MS = 10000;
constexpr uint64_t RANKINGS_INTERVAL_MS = 30000; // also works as keepalive
constexpr uint64_t FINGER_INTERVAL_MS = 120000;

// replay of finished games
constexpr uint64_t REPLAY_FIRST_HOLD_MS = 8000; // show the final position before the first replay
constexpr uint64_t REPLAY_START_HOLD_MS = 2000;
constexpr uint64_t REPLAY_STEP_MS = 800;
constexpr uint64_t REPLAY_END_HOLD_MS = 6000;
constexpr uint64_t FLIP_ANIMATION_MS = 380;

/*
    othello helpers on Position
*/
inline uint64_t legal_moves(const Position& p) {
    if (p.to_move == BLACK) return calc_legal(p.black, p.white);
    if (p.to_move == WHITE) return calc_legal(p.white, p.black);
    return 0;
}

inline bool is_game_over(const Position& p) {
    return calc_legal(p.black, p.white) == 0 && calc_legal(p.white, p.black) == 0;
}

inline int disc_count(const Position& p, int color) {
    return pop_count_ull(color == BLACK ? p.black : p.white);
}

// final score for black, empties go to the winner
inline int score_black(const Position& p) {
    int b = disc_count(p, BLACK), w = disc_count(p, WHITE);
    int e = HW2 - b - w;
    int diff = b - w;
    if (diff > 0) diff += e;
    else if (diff < 0) diff -= e;
    return diff;
}

inline bool apply_move(Position& p, bool pass, int cell) {
    if (p.to_move != BLACK && p.to_move != WHITE) return false;
    uint64_t legal = legal_moves(p);
    if (pass) {
        if (legal) return false;
        p.to_move ^= 1;
        return true;
    }
    if (cell < 0 || cell >= HW2) return false;
    uint64_t bit = ggs::cell_bit(cell);
    if ((legal & bit) == 0) return false;
    uint64_t& me = p.to_move == BLACK ? p.black : p.white;
    uint64_t& op = p.to_move == BLACK ? p.white : p.black;
    Flip flip;
    flip.calc_flip(me, op, (uint_fast8_t)(HW2_M1 - cell));
    me ^= flip.flip;
    op ^= flip.flip;
    me |= bit;
    p.to_move ^= 1;
    return true;
}

/*
    @brief evaluation reported by a player, in the player's own perspective
*/
struct EvalPoint {
    int ply;
    double value;
    bool exact; // final result of the game
};

struct GameView {
    bool active = false; // received at least one board
    std::string id;
    std::string name[2]; // by color
    double rating[2] = { 0.0, 0.0 };
    Position start;
    bool has_start = false;
    Position pos;
    Position prev_pos;   // for the flip animation
    bool has_prev = false;
    uint64_t changed_ms = 0;
    int ply = 0;
    int last_cell = -1;
    bool last_pass = false;
    int start_empties = 0;
    int clock_s[2] = { 0, 0 };
    bool has_clock = false;
    uint64_t clock_ms = 0;
    std::vector<EvalPoint> evals[2]; // by color
    bool sends_eval[2] = { false, false };
    bool finished = false;
    uint64_t finished_ms = 0;
    bool has_official_result = false;
    double result_black = 0.0;
    std::vector<Position> history; // history[0]: start, history[k]: after ply k
    std::vector<int> history_cell; // history_cell[k - 1]: move of ply k (-1: pass)
    bool history_ok = false;

    bool can_replay() const {
        return finished && history_ok && history.size() >= 2;
    }

    int color_of(const std::string& player) const {
        if (!player.empty() && name[BLACK] == player) return BLACK;
        if (!player.empty() && name[WHITE] == player) return WHITE;
        return -1;
    }

    int discs(int color) const { return disc_count(pos, color); }

    // remaining time on the clock at now_ms
    int remaining_seconds(int color, uint64_t now_ms) const {
        if (!has_clock) return 0;
        int s = clock_s[color];
        if (!finished && pos.to_move == color) {
            s -= (int)((now_ms - clock_ms) / 1000);
        }
        return s;
    }

    double final_result_black() const {
        return has_official_result ? result_black : (double)score_black(pos);
    }

    // last evaluation of the player with this color (own perspective)
    bool last_eval(int color, double& value) const {
        if (evals[color].empty()) return false;
        value = evals[color].back().value;
        return true;
    }

    int x_max() const {
        int e = start_empties > 0 ? start_empties : 60;
        return std::max(e, ply);
    }
};

struct MatchView {
    std::string id;
    long number = -1;
    std::string player[2]; // left / right
    GameView game[2];
    uint64_t created_ms = 0;
    uint64_t watch_sent_ms = 0;
    int watch_attempts = 0;
    bool watch_failed = false;
    bool finished = false;
    bool has_result = false;
    double result_p0 = 0.0; // match result for player[0] (average of both games)

    bool joined() const { return game[0].active && game[1].active; }

    int total_discs(int idx) const {
        int res = 0;
        for (const GameView& g : game) {
            int c = g.color_of(player[idx]);
            if (g.active && c >= 0) res += g.discs(c);
        }
        return res;
    }

    bool both_games_finished() const {
        return game[0].active && game[1].active && game[0].finished && game[1].finished;
    }

    // sum of both games' results for player[0]
    bool result_sum(double& value) const {
        if (has_result) {
            value = result_p0 * 2.0;
            return true;
        }
        if (!both_games_finished()) return false;
        value = 0.0;
        for (const GameView& g : game) {
            int c = g.color_of(player[0]);
            if (c < 0) return false;
            double r = g.final_result_black();
            value += c == BLACK ? r : -r;
        }
        return true;
    }

    /*
        @brief sum of the player's evaluations over both games (player's own perspective)
        returns (ply, value) points where both games have a value
    */
    std::vector<std::pair<int, double>> eval_series(int idx) const {
        std::vector<std::pair<int, double>> res;
        const std::vector<EvalPoint>* series[2];
        for (int i = 0; i < 2; ++i) {
            int c = game[i].color_of(player[idx]);
            if (c < 0) return res;
            series[i] = &game[i].evals[c];
        }
        std::vector<int> plies;
        for (int i = 0; i < 2; ++i) {
            for (const EvalPoint& p : *series[i]) plies.push_back(p.ply);
        }
        std::sort(plies.begin(), plies.end());
        plies.erase(std::unique(plies.begin(), plies.end()), plies.end());
        size_t it[2] = { 0, 0 };
        double value[2] = { 0.0, 0.0 };
        bool has[2] = { false, false };
        for (int x : plies) {
            for (int i = 0; i < 2; ++i) {
                while (it[i] < series[i]->size() && (*series[i])[it[i]].ply <= x) {
                    value[i] = (*series[i])[it[i]].value;
                    has[i] = true;
                    ++it[i];
                }
            }
            if (has[0] && has[1]) res.emplace_back(x, value[0] + value[1]);
        }
        return res;
    }

    int x_max() const {
        return std::max(game[0].x_max(), game[1].x_max());
    }
};

/*
    @brief what to show on the board of a finished game
*/
struct ReplayFrame {
    bool active = false;
    int ply = 0;      // position history[ply] is shown
    int n = 0;        // number of plies of the game
    double t = 1.0;   // flip animation progress from history[ply - 1]
};

inline ReplayFrame replay_frame(const MatchView& m, int i, uint64_t now) {
    ReplayFrame f;
    const GameView& g = m.game[i];
    if (!g.can_replay()) return f;
    const GameView& o = m.game[1 - i];
    // a match is replayed when both of its games are over; both boards are replayed side by side
    if (!o.active || !o.finished) return f;
    int n = (int)g.history.size() - 1;
    uint64_t base = std::max(g.finished_ms, o.finished_ms);
    int n_cycle = o.can_replay() ? std::max(n, (int)o.history.size() - 1) : n;
    if (now < base + REPLAY_FIRST_HOLD_MS) return f;
    uint64_t cycle = REPLAY_START_HOLD_MS + (uint64_t)n_cycle * REPLAY_STEP_MS + REPLAY_END_HOLD_MS;
    uint64_t e = (now - base - REPLAY_FIRST_HOLD_MS) % cycle;
    f.active = true;
    f.n = n;
    if (e < REPLAY_START_HOLD_MS) {
        f.ply = 0;
        return f;
    }
    uint64_t k = (e - REPLAY_START_HOLD_MS) / REPLAY_STEP_MS + 1;
    if ((int)k > n) {
        f.ply = n;
        return f;
    }
    f.ply = (int)k;
    f.t = std::min(1.0, (double)((e - REPLAY_START_HOLD_MS) % REPLAY_STEP_MS) / FLIP_ANIMATION_MS);
    return f;
}

struct RankEntry {
    int rank = 0;
    ggs::RankRow row;
};

enum class RoundStatus { Unknown, Playing, Break, Over };

struct TournamentView {
    std::string id;
    int round = -1;
    int rounds_total = -1;
    RoundStatus status = RoundStatus::Unknown;
    std::vector<RankEntry> rankings;
    std::set<std::string> players;
    std::vector<MatchView> matches; // sorted by match number
    uint64_t rankings_ms = 0;
    int break_seconds = 60;         // break between rounds (from the tournament info)
    uint64_t next_round_ms = 0;     // expected start of the next round during a break

    MatchView* find_match(const std::string& match_id) {
        for (auto& m : matches) {
            if (m.id == match_id) return &m;
        }
        return nullptr;
    }

    bool is_player(const std::string& name) const {
        return players.count(name) > 0;
    }

    bool is_playing(const std::string& name) const {
        if (status == RoundStatus::Break || status == RoundStatus::Over) return false;
        for (const auto& m : matches) {
            if (!m.finished && (m.player[0] == name || m.player[1] == name)) return true;
        }
        return false;
    }

    int finished_matches() const {
        int n = 0;
        for (const auto& m : matches) n += m.finished ? 1 : 0;
        return n;
    }
};

/*
    @brief turns GGS messages into a TournamentView and decides what to send to the server
*/
class StreamState {
public:
    TournamentView t;
    std::function<void(const std::string&)> send;
    std::function<void(const std::string&)> log;

private:
    uint64_t last_match_list_ms = 0;
    uint64_t last_rankings_ms = 0;
    uint64_t last_finger_ms = 0;
    uint64_t rankings_due_ms = 0;  // 0: not scheduled
    uint64_t match_list_due_ms = 0;
    uint64_t finger_due_ms = 0;
    bool connected = false;

public:
    explicit StreamState(const std::string& tournament_id) {
        t.id = tournament_id;
    }

    void on_connected(uint64_t now) {
        connected = true;
        for (auto& m : t.matches) {
            m.watch_sent_ms = 0;
            m.watch_attempts = 0;
        }
        request_finger(now);
        request_rankings(now);
        request_match_list(now);
    }

    void on_disconnected() {
        connected = false;
    }

    void tick(uint64_t now) {
        if (!connected) return;
        if (rankings_due_ms && now >= rankings_due_ms) request_rankings(now);
        if (match_list_due_ms && now >= match_list_due_ms) request_match_list(now);
        if (finger_due_ms && now >= finger_due_ms) request_finger(now);
        if (now - last_rankings_ms >= RANKINGS_INTERVAL_MS) request_rankings(now);
        if (now - last_finger_ms >= FINGER_INTERVAL_MS) request_finger(now);
        if (need_match_list() && now - last_match_list_ms >= MATCH_LIST_INTERVAL_MS) request_match_list(now);
        for (auto& m : t.matches) {
            if (m.finished || m.watch_failed || m.joined()) continue;
            if (m.watch_sent_ms == 0 || now - m.watch_sent_ms >= WATCH_RETRY_MS) {
                emit("tell /os watch + " + m.id);
                m.watch_sent_ms = now;
                ++m.watch_attempts;
            }
        }
    }

    void on_message(const ggs::Message& msg, uint64_t now) {
        int round = ggs::parse_round_event(msg, t.id, true);
        if (round >= 0) {
            on_round_start(round, now);
            return;
        }
        round = ggs::parse_round_event(msg, t.id, false);
        if (round >= 0) {
            on_round_end(round, now);
            return;
        }
        if (ggs::parse_tournament_over(msg, t.id)) {
            t.status = RoundStatus::Over;
            schedule_rankings(now, 500);
            return;
        }
        std::vector<ggs::RankRow> ranks;
        if (ggs::parse_rankings(msg, t.id, ranks)) {
            on_rankings(ranks, now);
            return;
        }
        ggs::TournamentInfo info;
        if (ggs::parse_finger(msg, t.id, info)) {
            on_finger(info, now);
            return;
        }
        ggs::BoardMessage bm;
        if (ggs::parse_board_message(msg, bm)) {
            on_board(bm, now);
            return;
        }
        ggs::GameEnd ge;
        if (ggs::parse_game_end(msg, ge)) {
            on_game_end(ge, now);
            return;
        }
        ggs::MatchEvent me;
        if (ggs::parse_match_event(msg, me)) {
            on_match_event(me, now);
            return;
        }
        std::vector<ggs::MatchRow> rows;
        if (ggs::parse_match_list(msg, rows)) {
            for (const auto& row : rows) add_match(row, now);
            return;
        }
        on_watch_error(msg);
    }

private:
    void emit(const std::string& cmd) {
        if (send) send(cmd);
    }

    void write_log(const std::string& s) {
        if (log) log(s);
    }

    void request_rankings(uint64_t now) {
        emit("t /td r " + t.id);
        last_rankings_ms = now;
        rankings_due_ms = 0;
    }

    void request_finger(uint64_t now) {
        emit("t /td f " + t.id);
        last_finger_ms = now;
        finger_due_ms = 0;
    }

    void schedule_finger(uint64_t now, uint64_t delay) {
        uint64_t due = now + delay;
        if (finger_due_ms == 0 || due < finger_due_ms) finger_due_ms = due;
    }

    void request_match_list(uint64_t now) {
        emit("ts match");
        last_match_list_ms = now;
        match_list_due_ms = 0;
    }

    void schedule_rankings(uint64_t now, uint64_t delay) {
        uint64_t due = now + delay;
        if (rankings_due_ms == 0 || due < rankings_due_ms) rankings_due_ms = due;
    }

    void schedule_match_list(uint64_t now, uint64_t delay) {
        uint64_t due = now + delay;
        if (match_list_due_ms == 0 || due < match_list_due_ms) match_list_due_ms = due;
    }

    bool need_match_list() const {
        if (t.status == RoundStatus::Break || t.status == RoundStatus::Over) return false;
        if (t.players.size() >= 2 && t.matches.size() >= t.players.size() / 2) return false;
        return true;
    }

    void on_round_start(int round, uint64_t now) {
        write_log("round " + std::to_string(round) + " started");
        if (t.round != round || t.status != RoundStatus::Playing) {
            t.matches.clear();
        }
        t.round = round;
        t.status = RoundStatus::Playing;
        t.next_round_ms = 0;
        request_match_list(now);
        schedule_match_list(now, 3000);
        schedule_rankings(now, 200);
    }

    void on_round_end(int round, uint64_t now) {
        write_log("round " + std::to_string(round) + " finished");
        t.round = round;
        t.status = RoundStatus::Break;
        t.next_round_ms = now + (uint64_t)t.break_seconds * 1000;
        for (auto& m : t.matches) {
            if (!m.joined()) m.watch_failed = true;
        }
        schedule_rankings(now, 500);
        schedule_finger(now, 1000); // exact time to the next round
    }

    void on_rankings(const std::vector<ggs::RankRow>& rows, uint64_t now) {
        t.rankings.clear();
        int rank = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            // same points and discs share the rank
            if (i == 0 || rows[i].points != rows[i - 1].points || rows[i].discs != rows[i - 1].discs) rank = (int)i + 1;
            t.rankings.push_back(RankEntry{ rank, rows[i] });
            t.players.insert(rows[i].name);
        }
        t.rankings_ms = now;
    }

    void on_finger(const ggs::TournamentInfo& info, uint64_t now) {
        if (info.rounds_total > 0) t.rounds_total = info.rounds_total;
        if (info.break_seconds > 0) t.break_seconds = info.break_seconds;
        for (const auto& p : info.players) t.players.insert(p);
        if (info.finished) {
            t.status = RoundStatus::Over;
            if (info.rounds_total > 0) t.round = info.rounds_total;
        } else if (info.playing && info.current_round > 0) {
            if (t.round != info.current_round || t.status != RoundStatus::Playing) {
                if (t.round != -1 && t.round != info.current_round) t.matches.clear();
                t.round = info.current_round;
                t.status = RoundStatus::Playing;
                schedule_match_list(now, 0);
            }
        } else if (info.in_break && info.current_round > 0) {
            t.status = RoundStatus::Break;
            t.round = info.current_round - 1;
            if (info.begins_in >= 0) t.next_round_ms = now + (uint64_t)info.begins_in * 1000;
        }
    }

    bool accept_players(const std::string& a, const std::string& b) const {
        return t.is_player(a) && t.is_player(b) && a != b;
    }

    MatchView* add_match(const ggs::MatchRow& row, uint64_t now) {
        if (t.status == RoundStatus::Break || t.status == RoundStatus::Over) return nullptr;
        if (!accept_players(row.player[0], row.player[1])) return nullptr;
        if (!row.type.empty() && row.type[0] != 's') return nullptr; // synchro only
        if (MatchView* m = t.find_match(row.id)) return m;
        MatchView m;
        m.id = row.id;
        m.number = ggs::match_number(row.id);
        m.player[0] = row.player[0];
        m.player[1] = row.player[1];
        m.created_ms = now;
        write_log("new match " + m.id + " " + m.player[0] + " vs " + m.player[1]);
        t.matches.push_back(m);
        std::sort(t.matches.begin(), t.matches.end(), [](const MatchView& a, const MatchView& b) {
            return a.number < b.number;
        });
        return t.find_match(row.id);
    }

    void on_match_event(const ggs::MatchEvent& ev, uint64_t now) {
        if (ev.start) {
            add_match(ev.row, now);
            return;
        }
        MatchView* m = t.find_match(ev.row.id);
        if (m == nullptr) return;
        m->finished = true;
        if (ev.has_result) {
            m->has_result = true;
            m->result_p0 = ev.row.player[0] == m->player[0] ? ev.result0 : -ev.result0;
        }
        schedule_rankings(now, 1500);
    }

    void on_watch_error(const ggs::Message& msg) {
        if (msg.head.find("ERR") == std::string::npos || msg.head.find("not found") == std::string::npos) return;
        for (const std::string& w : ggs::tokenize(msg.head)) {
            std::string id = w;
            id.erase(std::remove(id.begin(), id.end(), '\''), id.end());
            id.erase(std::remove(id.begin(), id.end(), '"'), id.end());
            if (!ggs::is_match_id_token(id)) continue;
            for (size_t i = 0; i < t.matches.size(); ++i) {
                MatchView& m = t.matches[i];
                if (m.id != id) continue;
                write_log("watch failed " + id);
                if (!m.game[0].active && !m.game[1].active) {
                    t.matches.erase(t.matches.begin() + i);
                } else {
                    m.watch_failed = true;
                }
                return;
            }
        }
    }

    static void mark_finished(GameView& g, uint64_t now) {
        if (!g.finished) {
            g.finished = true;
            g.finished_ms = now;
        }
    }

    void on_game_end(const ggs::GameEnd& ge, uint64_t now) {
        MatchView* m = t.find_match(ge.match_id);
        if (m == nullptr || ge.sub < 0 || ge.sub > 1) return;
        GameView& g = m->game[ge.sub];
        mark_finished(g, now);
        if (ge.has_result) {
            int c = g.color_of(ge.player[0]);
            if (c >= 0) {
                g.has_official_result = true;
                g.result_black = c == BLACK ? ge.result0 : -ge.result0;
            }
        }
        add_final_evals(g);
    }

    static void add_final_evals(GameView& g) {
        if (!g.active) return;
        double r = g.final_result_black();
        for (int c = 0; c < 2; ++c) {
            double v = c == BLACK ? r : -r;
            if (!g.evals[c].empty() && g.evals[c].back().exact) {
                g.evals[c].back().value = v;
            } else {
                g.evals[c].push_back(EvalPoint{ g.ply, v, true });
            }
        }
    }

    static void record_eval(GameView& g, int mover, const MoveInfo& mv, bool forced_known, bool forced) {
        if (mover != BLACK && mover != WHITE) return;
        if (mv.has_eval) g.sends_eval[mover] = true;
        if (mv.pass) return;                  // evaluations sent with a pass are meaningless
        if (forced_known && forced) return;   // only one legal move: evaluation is usually 0
        if (!mv.has_eval && (!forced_known || !g.sends_eval[mover])) return;
        // the server omits an evaluation of exactly 0
        g.evals[mover].push_back(EvalPoint{ mv.ply, mv.has_eval ? mv.eval : 0.0, false });
    }

    void on_board(const ggs::BoardMessage& bm, uint64_t now) {
        if (bm.sub < 0 || bm.sub > 1) return; // synchro games only
        MatchView* m = t.find_match(bm.match_id);
        if (m == nullptr) {
            if (!bm.clock[BLACK].valid || !bm.clock[WHITE].valid) return;
            ggs::MatchRow row;
            row.id = bm.match_id;
            row.player[0] = bm.sub == 0 ? bm.clock[BLACK].name : bm.clock[WHITE].name;
            row.player[1] = bm.sub == 0 ? bm.clock[WHITE].name : bm.clock[BLACK].name;
            row.type = bm.type;
            m = add_match(row, now);
            if (m == nullptr) return;
        }
        GameView& g = m->game[bm.sub];
        g.id = bm.game_id;
        for (int c = 0; c < 2; ++c) {
            if (bm.clock[c].valid) {
                g.name[c] = bm.clock[c].name;
                g.rating[c] = bm.clock[c].rating;
                g.clock_s[c] = bm.clock[c].seconds;
                g.has_clock = true;
            }
        }
        g.clock_ms = now;
        if (bm.is_join) {
            apply_join(g, bm);
        } else {
            apply_update(g, bm, now);
        }
        g.active = true;
        if (is_game_over(g.pos)) mark_finished(g, now);
        if (g.finished) add_final_evals(g);
    }

    void apply_join(GameView& g, const ggs::BoardMessage& bm) {
        const Position& server_pos = bm.boards.back();
        for (int c = 0; c < 2; ++c) {
            g.evals[c].clear();
            g.sends_eval[c] = false;
        }
        g.has_prev = false;
        g.finished = false;
        g.has_official_result = false;
        g.history.clear();
        g.history_cell.clear();
        if (bm.boards.size() >= 2 || bm.moves.empty()) {
            g.start = bm.boards.front();
            g.has_start = true;
        } else {
            g.has_start = false;
        }
        int ply = bm.moves.empty() ? 0 : bm.moves.back().ply;
        if (bm.move_count > ply) ply = bm.move_count;
        bool replay_ok = g.has_start;
        Position p = g.start;
        if (replay_ok) g.history.push_back(p);
        for (const MoveInfo& mv : bm.moves) {
            if (replay_ok) {
                int mover = p.to_move;
                int n_legal = pop_count_ull(legal_moves(p));
                record_eval(g, mover, mv, true, mv.pass || n_legal <= 1);
                if (!apply_move(p, mv.pass, mv.cell)) {
                    write_log("replay failed at ply " + std::to_string(mv.ply) + " in " + g.id);
                    replay_ok = false;
                } else {
                    g.history.push_back(p);
                    g.history_cell.push_back(mv.pass ? -1 : mv.cell);
                }
            } else {
                record_eval(g, -1, mv, false, false);
            }
        }
        if (replay_ok && !(p == server_pos)) {
            write_log("replayed position differs from the server position in " + g.id);
        }
        g.pos = server_pos;
        g.ply = ply;
        g.history_ok = replay_ok && p == server_pos && (int)g.history.size() == ply + 1;
        const MoveInfo* last = bm.moves.empty() ? nullptr : &bm.moves.back();
        g.last_cell = last && !last->pass ? last->cell : -1;
        g.last_pass = last && last->pass;
        g.start_empties = g.has_start ? HW2 - pop_count_ull(g.start.black | g.start.white)
                                      : HW2 - pop_count_ull(server_pos.black | server_pos.white) + ply;
    }

    void apply_update(GameView& g, const ggs::BoardMessage& bm, uint64_t now) {
        const Position& server_pos = bm.boards.back();
        if (bm.moves.empty()) {
            g.pos = server_pos;
            return;
        }
        const MoveInfo& mv = bm.moves.back();
        if (g.active && mv.ply <= g.ply) {
            // already known move; keep the history, refresh the position
            g.pos = server_pos;
            return;
        }
        if (g.active && mv.ply == g.ply + 1) {
            Position p = g.pos;
            int mover = p.to_move;
            int n_legal = pop_count_ull(legal_moves(p));
            record_eval(g, mover, mv, true, mv.pass || n_legal <= 1);
            if (!apply_move(p, mv.pass, mv.cell) || !(p == server_pos)) {
                write_log("position mismatch at ply " + std::to_string(mv.ply) + " in " + g.id + ", resynchronized");
                g.history_ok = false;
            } else if (g.history_ok) {
                g.history.push_back(p);
                g.history_cell.push_back(mv.pass ? -1 : mv.cell);
            }
        } else {
            g.history_ok = false;
            // missed some moves: the player who moved is the one not to move now (unless a pass followed)
            int mover = server_pos.to_move ^ 1;
            record_eval(g, mover, mv, false, false);
            if (!g.active) {
                g.start_empties = HW2 - pop_count_ull(server_pos.black | server_pos.white) + mv.ply;
            }
        }
        if (g.active) {
            g.prev_pos = g.pos;
            g.has_prev = true;
            g.changed_ms = now;
        }
        g.pos = server_pos;
        g.ply = mv.ply;
        g.last_cell = mv.pass ? -1 : mv.cell;
        g.last_pass = mv.pass;
    }
};

} // namespace stream
