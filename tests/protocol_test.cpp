/*
    GGS Stream - tests for the protocol parser, the state model and the demo simulator (no Siv3D needed)

    build (x64 Native Tools Command Prompt):
        cl /std:c++latest /EHsc /O2 /I.. protocol_test.cpp
*/
#include <iostream>
#include <map>
#include "../board.hpp"
#include "../stream_model.hpp"
#include "../demo_server.hpp"
#include "../log_replay.hpp"
#include <cstdio>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { ++failures; std::cerr << "FAILED " << __LINE__ << ": " << #cond << std::endl; } } while (0)

static ggs::Message frame_one(const std::string& raw) {
    ggs::MessageFramer f;
    f.feed(raw, 0);
    f.tick(10000);
    auto msgs = f.take();
    return msgs.empty() ? ggs::Message{} : msgs[0];
}

static void test_parsers() {
    int s;
    CHECK(ggs::parse_hhmmss("14:52", s) && s == 892);
    CHECK(ggs::parse_hhmmss("01:00:00", s) && s == 3600);
    CHECK(ggs::parse_hhmmss("-00:05", s) && s == -5);
    CHECK(ggs::parse_hhmmss("1.00:00:01", s) && s == 86401);
    CHECK(!ggs::parse_hhmmss("ab:cd", s));

    ggs::ClockInfo ci;
    CHECK(ggs::parse_clock_line("egrcd    (2556.5 *) 00:02,25:0//00:00,25:0", ci) && ci.name == "egrcd" && ci.color == BLACK && ci.seconds == 2);
    CHECK(ggs::parse_clock_line("newbie   ( 999.9 O) 01:02:03,0:0//02:00,0:0", ci) && ci.color == WHITE && ci.seconds == 3723 && ci.rating == 999.9);
    CHECK(ggs::parse_clock_line("slowpoke (2000.0 O) -00:05,N1:0//02:00,0:0", ci) && ci.seconds == -5);

    ggs::MoveInfo mi;
    CHECK(ggs::parse_move_line(" 49: PA/-2.00", mi) && mi.ply == 49 && mi.pass && mi.has_eval && mi.eval == -2.0);
    CHECK(ggs::parse_move_line(" 23: f5/1.23/4.56", mi) && mi.ply == 23 && !mi.pass && mi.cell == 4 * 8 + 5 && mi.eval == 1.23);
    CHECK(ggs::parse_move_line("  3: D3//0.51", mi) && mi.cell == 2 * 8 + 3 && !mi.has_eval);
    CHECK(ggs::parse_move_line("  0: PASS", mi) && mi.ply == 0);
    CHECK(!ggs::parse_move_line("egrcd    (2556.5 *) 00:02,25:0//00:00,25:0", mi));

    // framing with fragments and prompts
    ggs::MessageFramer f;
    std::string raw = "/os: end .31.1 ( nyanyan vs. egrcd ) +2.00\r\nREADY\r\n.tourney /td: starting round 3 of tournament 6\r\n/td: rankings: tournament 6\r\n|  6.0 ( 6  0  0) { 12.34 } nyanyan  [0.1234]\r\n|  3.5 ( 3  1  2) {  -1.50 } egrcd    [0.2]\r\n";
    for (size_t i = 0; i < raw.size(); i += 7) f.feed(raw.substr(i, 7), 0);
    auto msgs = f.take();
    CHECK(msgs.size() == 2);
    f.tick(1000);
    msgs = f.take();
    CHECK(msgs.size() == 1 && msgs[0].body.size() == 2);

    std::vector<ggs::RankRow> ranks;
    CHECK(ggs::parse_rankings(msgs[0], "6", ranks) && ranks.size() == 2 && ranks[0].name == "nyanyan" && ranks[1].points == 3.5 && ranks[1].draw == 1 && ranks[1].discs == -1.5);
    CHECK(!ggs::parse_rankings(msgs[0], "7", ranks));

    ggs::Message start = frame_one(".tourney /td: starting round 10 of tournament 6\n");
    CHECK(ggs::parse_round_event(start, "6", true) == 10);
    CHECK(ggs::parse_round_event(start, "6", false) == -1);
    CHECK(ggs::parse_round_event(start, "61", true) == -1);
    ggs::Message spoof = frame_one(".tourney nyanyan: starting round 10 of tournament 6\n");
    CHECK(ggs::parse_round_event(spoof, "6", true) == -1);

    ggs::GameEnd ge;
    CHECK(ggs::parse_game_end(frame_one("/os: end .31.1 ( nyanyan vs. egrcd ) +2.00\n"), ge) && ge.match_id == ".31" && ge.sub == 1 && ge.player[1] == "egrcd" && ge.result0 == 2.0);

    ggs::MatchEvent me;
    CHECK(ggs::parse_match_event(frame_one("/os: - match .65 2628 nyanyan 2616 egrcd s8r14 R +0.00  .83353\n"), me) && !me.start && me.row.id == ".65" && me.row.player[0] == "nyanyan" && me.has_result && me.result0 == 0.0);
    CHECK(ggs::parse_match_event(frame_one("/os: + match .9 2544 lynx 2505 kitty s8r20 T\n"), me) && me.start && me.row.player[1] == "kitty" && me.row.flag == "T");

    std::vector<ggs::MatchRow> rows;
    CHECK(ggs::parse_match_list(frame_one("/os: match 2/2\n|  .2 2664 nyanyan  2562 egrcd       s8r14  R 0\n|.1234 2000 a 1999 b 8 U 0\n"), rows) && rows.size() == 2 && rows[0].id == ".2" && rows[0].player[1] == "egrcd" && rows[1].id == ".1234" && rows[1].player[1] == "b");

    ggs::TournamentInfo info;
    CHECK(ggs::parse_finger(frame_one("/td: finger: \n|id        : 6\n|rounds    : 2/5\n|players   : 4 a b (c) d \n|round 3 is being played \n"), "6", info) && info.rounds_total == 5 && info.current_round == 3 && info.playing && info.players.size() == 4 && info.players[2] == "c");

    CHECK(ggs::parse_finger(frame_one("/td: finger: \n|id        : 6\n|breaks    :  1:00\n|round 4 begins in  0:42\n"), "6", info) && info.break_seconds == 60 && info.begins_in == 42 && info.in_break && info.current_round == 4);

    int sr_round = 0;
    std::vector<ggs::Pairing> pairings;
    CHECK(ggs::parse_schedule(frame_one("/td: sr: schedule for round 4 in tournament 27:\n|round   4:\n|  4.1   Kalmia ymatioun    +0.0\n|  4.2    egrcd   Melody    +0.0\n|  4.3   Forest rests\n"), "27", sr_round, pairings)
        && sr_round == 4 && pairings.size() == 2 && pairings[0].player[0] == "Kalmia" && pairings[1].player[1] == "Melody");

    // update message in the server format
    std::string update =
        "/os: update .4.1 s8r18 K?\n"
        "| 23: F5/1.23/4.56\n"
        "|lynx     (2544.0 *) 14:52,0:0//02:00,0:0\n"
        "|kitty    (2505.0 O) 01:02:03,0:0//02:00,0:0\n"
        "|\n"
        "|   A B C D E F G H\n"
        "| 1 - - - - - - - - 1 \n"
        "| 2 - - - - - - - - 2 \n"
        "| 3 - - - - - - - - 3 \n"
        "| 4 - - - O * - - - 4 \n"
        "| 5 - - - * O - - - 5 \n"
        "| 6 - - - - - - - - 6 \n"
        "| 7 - - - - - - - - 7 \n"
        "| 8 - - - - - - - * 8 \n"
        "|   A B C D E F G H\n"
        "|\n"
        "|O to move\n"
        "|\n";
    ggs::BoardMessage bm;
    CHECK(ggs::parse_board_message(frame_one(update), bm));
    CHECK(!bm.is_join && bm.match_id == ".4" && bm.sub == 1 && bm.boards.size() == 1 && bm.moves.size() == 1);
    CHECK(bm.boards[0].to_move == WHITE && (bm.boards[0].black & ggs::cell_bit(63)) && (bm.boards[0].white & ggs::cell_bit(27)));
    CHECK(bm.clock[BLACK].name == "lynx" && bm.clock[WHITE].seconds == 3723);
}

static void test_forced_eval() {
    // position where black has exactly one legal move
    stream::StreamState st("1");
    st.t.players = { "a", "b" };
    st.t.status = stream::RoundStatus::Playing;
    std::vector<std::string> sent;
    st.send = [&](const std::string& s) { sent.push_back(s); };
    st.on_message(frame_one("/os: match 1/1\n|  .7 2000 a 2000 b s8r18 T 0\n"), 0);
    CHECK(st.t.matches.size() == 1);

    auto board = [](const ggs::Position& p) {
        std::string s = "|   A B C D E F G H\n";
        for (int y = 0; y < 8; ++y) {
            s += "| " + std::to_string(y + 1);
            for (int x = 0; x < 8; ++x) {
                uint64_t bit = ggs::cell_bit(y * 8 + x);
                s += (p.black & bit) ? " *" : (p.white & bit) ? " O" : " -";
            }
            s += " " + std::to_string(y + 1) + " \n";
        }
        s += "|   A B C D E F G H\n|\n";
        s += p.to_move == BLACK ? "|* to move\n" : "|O to move\n";
        return s;
    };
    ggs::Position p = demo::initial_position();
    std::string join = "/os: join .7.0 s8r18 K?\n|0 move(s)\n|  0: PASS\n|a        (2000.0 *) 10:00,0:0//02:00,0:0\n|b        (2000.0 O) 10:00,0:0//02:00,0:0\n|\n" + board(p);
    st.on_message(frame_one(join), 0);
    // f5 with an evaluation
    ggs::Position q = p;
    stream::apply_move(q, false, 4 * 8 + 5);
    st.on_message(frame_one("/os: update .7.0 s8r18 K?\n|  1: f5/3.50\n|a        (2000.0 *) 09:50,0:0//02:00,0:0\n|b        (2000.0 O) 10:00,0:0//02:00,0:0\n|\n" + board(q)), 1000);
    const stream::GameView& g = st.t.matches[0].game[0];
    CHECK(g.ply == 1 && g.evals[BLACK].size() == 1 && g.evals[BLACK][0].value == 3.5);
    // white answers without evaluation: 3 legal moves, so it is a real 0 only if white sent evaluations before
    ggs::Position r = q;
    stream::apply_move(r, false, 5 * 8 + 5);
    st.on_message(frame_one("/os: update .7.0 s8r18 K?\n|  2: f6\n|a        (2000.0 *) 09:50,0:0//02:00,0:0\n|b        (2000.0 O) 09:40,0:0//02:00,0:0\n|\n" + board(r)), 2000);
    CHECK(st.t.matches[0].game[0].evals[WHITE].empty());
    CHECK(st.t.matches[0].game[0].has_prev);
}

static void test_real_rankings() {
    // response seen on the real server; rows with and without the leading '|'
    const char* rows[] = {
        "79.0 (62 34  4) {   1.48 } ymatioun [0.4768]",
        "65.5 (45 41 14) {   0.77 }   Kalmia [0.4875]",
        "64.5 (48 33 19) {   0.81 }   Melody [0.4887]",
        "59.0 (38 42 20) {   0.45 }    egrcd [0.4928]",
        "19.0 (12 14 74) {  -1.28 }   Forest [0.5260]",
        "13.0 ( 7 12 81) {  -2.23 }   piglet [0.5283]",
    };
    for (int with_bar = 0; with_bar < 2; ++with_bar) {
        stream::StreamState st("27");
        std::string raw = "/td: rankings: tournament 27\n";
        for (const char* r : rows) raw += std::string(with_bar ? "|" : "") + r + "\n";
        ggs::MessageFramer f;
        f.feed(raw, 0);
        f.tick(1000);
        for (const auto& m : f.take()) st.on_message(m, 1000);
        const auto& rk = st.t.rankings;
        CHECK(rk.size() == 6);
        if (rk.size() != 6) continue;
        CHECK(rk[0].rank == 1 && rk[0].row.name == "ymatioun" && rk[0].row.points == 79.0 && rk[0].row.win == 62 && rk[0].row.draw == 34 && rk[0].row.loss == 4 && rk[0].row.discs == 1.48);
        CHECK(rk[5].rank == 6 && rk[5].row.name == "piglet" && rk[5].row.discs == -2.23);
        CHECK(st.t.is_player("egrcd"));
    }
}

static void test_log_replay() {
    std::string tag;
    LogLine l;
    CHECK(parse_log_line("23:59:59.250 RECV |  1: f5/1.00", tag, l) && tag == "RECV" && l.text == "|  1: f5/1.00" && l.t_ms == 86399250);
    CHECK(parse_log_line("00:00:01.000 SEND ts match", tag, l) && tag == "SEND");
    CHECK(!parse_log_line("garbage", tag, l));

    // log a simulated tournament in the AppLog format, then replay it
    char path[L_tmpnam_s];
    tmpnam_s(path, sizeof(path));
    demo::DemoServer server(4, 40.0, "9", 11);
    stream::StreamState live("9");
    live.send = [&](const std::string& s) { server.send(s); };
    std::ofstream ofs(path);
    uint64_t now = 23 * 3600 * 1000ULL + 59 * 60 * 1000ULL; // crosses midnight
    server.start(now);
    live.on_connected(now);
    for (int step = 0; step < 4000 && live.t.status != stream::RoundStatus::Over; ++step) {
        now += 50;
        for (const auto& m : server.poll(now)) {
            live.on_message(m, now);
            uint64_t t = now % (24 * 3600 * 1000ULL);
            char ts[32];
            std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d", (int)(t / 3600000), (int)(t / 60000 % 60), (int)(t / 1000 % 60), (int)(t % 1000));
            std::string text = m.text();
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                ofs << ts << " RECV " << text.substr(start, nl == std::string::npos ? std::string::npos : nl - start) << '\n';
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
            ofs << ts << " SEND ts match\n";
        }
        live.tick(now);
    }
    ofs.close();
    CHECK(live.t.status == stream::RoundStatus::Over);

    LogReplaySource replay(path, 1000.0, 0);
    CHECK(!replay.empty());
    stream::StreamState st("9");
    st.send = [&](const std::string& s) { replay.send(s); };
    st.on_connected(0);
    for (uint64_t t = 0; t < 600000; t += 50) {
        for (const auto& m : replay.poll(t)) st.on_message(m, t);
        st.tick(t);
    }
    CHECK(st.t.status == stream::RoundStatus::Over);
    CHECK(st.t.rankings.size() == live.t.rankings.size());
    for (size_t i = 0; i < st.t.rankings.size() && i < live.t.rankings.size(); ++i) {
        CHECK(st.t.rankings[i].row.name == live.t.rankings[i].row.name && st.t.rankings[i].row.points == live.t.rankings[i].row.points);
    }
    std::remove(path);
}

static void run_simulation(int n_players, uint64_t seed) {
    demo::DemoServer server(n_players, 40.0, "9", seed);
    stream::StreamState st("9");
    std::vector<std::string> problems;
    std::map<int, size_t> matches_per_round;
    st.send = [&](const std::string& s) { server.send(s); };
    st.log = [&](const std::string& s) {
        if (s.find("mismatch") != std::string::npos || s.find("failed") != std::string::npos || s.find("differs") != std::string::npos) problems.push_back(s);
    };
    uint64_t now = 1000;
    server.start(now);
    st.on_connected(now);
    size_t eval_points = 0, series_points = 0;
    int finished_rounds = 0;
    uint64_t break_started = 0;
    auto collect = [&]() {
        for (const auto& m : st.t.matches) {
            CHECK(m.finished && m.joined());
            double r;
            CHECK(m.result_sum(r));
            for (int i = 0; i < 2; ++i) {
                eval_points += m.game[i].evals[0].size() + m.game[i].evals[1].size();
                series_points += m.eval_series(i).size();
                const stream::GameView& g = m.game[i];
                CHECK(g.can_replay());
                CHECK((int)g.history.size() == g.ply + 1 && g.history.back() == g.pos);
                // replay: final position first, then the start position, then moves
                uint64_t base = std::max(m.game[0].finished_ms, m.game[1].finished_ms);
                CHECK(!stream::replay_frame(m, i, base + 100).active);
                stream::ReplayFrame f0 = stream::replay_frame(m, i, base + stream::REPLAY_FIRST_HOLD_MS + 10);
                CHECK(f0.active && f0.ply == 0);
                stream::ReplayFrame f3 = stream::replay_frame(m, i, base + stream::REPLAY_FIRST_HOLD_MS + stream::REPLAY_START_HOLD_MS + 2 * stream::REPLAY_STEP_MS + 10);
                CHECK(f3.active && f3.ply == 3 && f3.t < 0.1);
            }
        }
    };
    for (int step = 0; step < 400000 && st.t.status != stream::RoundStatus::Over; ++step) {
        now += 50;
        for (const auto& m : server.poll(now)) st.on_message(m, now);
        st.tick(now);
        if (st.t.status == stream::RoundStatus::Break) {
            CHECK(st.t.next_round_ms > now);
            if (now > break_started + 3000) CHECK(st.t.next_round == st.t.round + 1 && (int)st.t.next_pairings.size() == n_players / 2);
        } else {
            break_started = now;
        }
        if (st.t.status == stream::RoundStatus::Playing && st.t.round > 1) CHECK(!st.t.rank_at_round_start.empty());
        if (st.t.status == stream::RoundStatus::Playing && st.t.round > 0) {
            matches_per_round[st.t.round] = std::max(matches_per_round[st.t.round], st.t.matches.size());
        }
        if ((st.t.status == stream::RoundStatus::Break || st.t.status == stream::RoundStatus::Over) && st.t.round > finished_rounds) {
            finished_rounds = st.t.round;
            collect();
        }
    }
    if (st.t.round > finished_rounds) collect();
    CHECK(st.t.status == stream::RoundStatus::Over);
    CHECK((int)matches_per_round.size() == n_players - 1);
    for (auto [round, n] : matches_per_round) CHECK(n == (size_t)n_players / 2);
    CHECK((int)st.t.rankings.size() == n_players);
    CHECK(st.t.rounds_total == n_players - 1);
    CHECK(eval_points > 0 && series_points > 0);
    for (const auto& p : problems) std::cerr << "  problem: " << p << std::endl;
    CHECK(problems.empty());
    std::cout << "simulation " << n_players << " players: rounds " << matches_per_round.size() << ", eval points " << eval_points << ", series points " << series_points << ", sim time " << (now / 1000) << " s" << std::endl;
}

static void test_synchro_divergence() {
    stream::MatchView match;
    ggs::Position start;
    start.black = ggs::cell_bit(3 * 8 + 4) | ggs::cell_bit(4 * 8 + 3);
    start.white = ggs::cell_bit(3 * 8 + 3) | ggs::cell_bit(4 * 8 + 4);
    start.to_move = BLACK;
    for (auto& game : match.game) {
        game.history_ok = true;
        game.history.push_back(start);
    }
    CHECK(match.first_divergent_ply() == -1);
    auto first = start;
    CHECK(stream::apply_move(first, false, 2 * 8 + 3));
    match.game[0].history.push_back(first);
    CHECK(match.first_divergent_ply() == -1); // one game is ahead in time
    match.game[1].history.push_back(first);
    CHECK(match.first_divergent_ply() == -1);
    std::vector<ggs::Position> replies;
    uint64_t legal = stream::legal_moves(first);
    for (int cell = 0; cell < HW2; ++cell) {
        if ((legal & ggs::cell_bit(cell)) == 0) continue;
        auto next = first;
        CHECK(stream::apply_move(next, false, cell));
        replies.push_back(next);
    }
    CHECK(replies.size() >= 2);
    match.game[0].history.push_back(replies[0]);
    match.game[1].history.push_back(replies[1]);
    CHECK(match.first_divergent_ply() == 2);
    match.game[0].history.push_back(replies[1]);
    match.game[1].history.push_back(replies[1]);
    CHECK(match.first_divergent_ply() == 2); // the first split remains marked
    match.game[1].history_ok = false;
    CHECK(match.first_divergent_ply() == -1); // incomplete history is not evidence
    match.game[1].history_ok = true;
    match.game[1].history[0].to_move = WHITE;
    CHECK(match.first_divergent_ply() == 0);
}

static void test_replay_seek() {
    stream::MatchView match;
    ggs::Position start;
    start.black = ggs::cell_bit(3 * 8 + 4) | ggs::cell_bit(4 * 8 + 3);
    start.white = ggs::cell_bit(3 * 8 + 3) | ggs::cell_bit(4 * 8 + 4);
    start.to_move = BLACK;
    for (int i = 0; i < 2; ++i) {
        auto& g = match.game[i];
        g.active = g.finished = g.history_ok = true;
        g.finished_ms = 1000;
        g.start_empties = 5;
        g.history.push_back(start);
        auto pos = start;
        for (int ply = 0; ply < (i == 0 ? 3 : 5); ++ply) {
            uint64_t legal = stream::legal_moves(pos);
            int cell = 0;
            while (!(legal & ggs::cell_bit(cell))) ++cell;
            CHECK(stream::apply_move(pos, false, cell));
            g.history.push_back(pos);
            g.history_cell.push_back(cell);
        }
    }
    stream::ReplayControl control;
    CHECK(stream::ReplayControl::length(match) == 5);
    CHECK(control.seek_fraction(match, -1) && control.selected_ply == 0);
    CHECK(control.frame(match, 0, 1000).active); // manual seek works before automatic replay starts
    CHECK(control.seek_fraction(match, 0.5) && control.selected_ply == 3);
    CHECK(control.seek(match, 2));
    CHECK(control.frame(match, 0, 1000).ply == 2);
    CHECK(control.frame(match, 1, 999999).ply == 2); // remains paused after release
    CHECK(control.frame(match, 1, 999999).t == 1.0);
    control.resume(match, 100000);
    CHECK(!control.paused());
    CHECK(control.frame(match, 1, 100000).ply == 2 && control.frame(match, 1, 100000).t == 1.0);
    CHECK(control.frame(match, 1, 100000 + stream::REPLAY_STEP_MS).ply == 3);
    CHECK(control.seek_fraction(match, 2) && control.selected_ply == 5);
    CHECK(control.frame(match, 0, 0).ply == 3 && control.frame(match, 1, 0).ply == 5);
    control.resume(match, 200000);
    CHECK(control.frame(match, 1, 200000).ply == 5);
    CHECK(control.frame(match, 1, 200000 + stream::REPLAY_END_HOLD_MS).ply == 0);
    match.game[0].finished = false;
    stream::ReplayControl live;
    CHECK(!live.seek(match, 2) && !live.paused());
}

int main() {
    bit_init();
    mobility_init();
    flip_init();
    test_parsers();
    test_forced_eval();
    test_real_rankings();
    test_log_replay();
    test_synchro_divergence();
    test_replay_seek();
    run_simulation(6, 1);
    run_simulation(4, 2);
    run_simulation(8, 3);
    run_simulation(2, 4);
    if (failures) {
        std::cerr << failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all tests passed" << std::endl;
    return 0;
}
