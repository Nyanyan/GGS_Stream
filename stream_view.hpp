/*
    GGS Stream

    @file stream_view.hpp
        Broadcast layout (1920x1080 scene) drawn with Siv3D
*/
#pragma once
# include <Siv3D.hpp>
#include "stream_model.hpp"
#include "app_log.hpp"

namespace view {

using stream::GameView;
using stream::MatchView;
using stream::TournamentView;
using stream::RoundStatus;

namespace col {
    // Warm score sheet, ink, and the green of a physical Othello board.
    inline constexpr ColorF bg{ 0.945, 0.945, 0.921 };
    inline constexpr ColorF header{ 0.977, 0.977, 0.956 };
    inline constexpr ColorF panel{ 0.998, 0.997, 0.984 };
    inline constexpr ColorF panel2{ 0.950, 0.965, 0.934 };
    inline constexpr ColorF border{ 0.655, 0.717, 0.655 };
    inline constexpr ColorF text{ 0.063, 0.122, 0.086 };
    inline constexpr ColorF sub{ 0.235, 0.310, 0.259 };
    inline constexpr ColorF faint{ 0.345, 0.416, 0.361 };
    // Player colours occupy full name bands and are identified by player names.
    inline constexpr ColorF accent[2] = { ColorF{ 0.765, 0.310, 0.043 }, ColorF{ 0.039, 0.388, 0.631 } };
    inline constexpr ColorF on_accent{ 1.0 };
    inline constexpr ColorF win{ 0.043, 0.443, 0.220 };
    inline constexpr ColorF loss{ 0.741, 0.188, 0.141 };
    inline constexpr ColorF draw{ 0.405, 0.450, 0.410 };
    inline constexpr ColorF warn{ 0.675, 0.353, 0.043 };
    inline constexpr ColorF board{ 0.094, 0.502, 0.267 };
    inline constexpr ColorF board_frame{ 0.024, 0.231, 0.114 };
    inline constexpr ColorF grid{ 0.008, 0.173, 0.067 };
    inline constexpr ColorF disc_black{ 0.080, 0.112, 0.091 };
    inline constexpr ColorF disc_white{ 0.975, 0.967, 0.930 };
    inline constexpr ColorF last_move{ 0.925, 0.216, 0.133 };
    inline constexpr ColorF gold{ 0.678, 0.427, 0.031 };
    inline constexpr ColorF silver{ 0.420, 0.490, 0.460 };
    inline constexpr ColorF bronze{ 0.584, 0.400, 0.296 };
    inline constexpr ColorF demo{ 0.455, 0.500, 0.455 };
    // Replay boards are quiet and desaturated, with their own explicit label.
    inline constexpr ColorF replay{ 0.196, 0.369, 0.263 };
    inline constexpr ColorF board_replay{ 0.337, 0.510, 0.392 };
    inline constexpr ColorF grid_replay{ 0.055, 0.192, 0.090 };
    inline constexpr ColorF graph_diverged{ 0.855, 0.887, 0.839 };
}

constexpr double SCENE_W = 1920;
constexpr double SCENE_H = 1080;
constexpr double MARGIN = 28;
constexpr double HEADER_H = 76;
constexpr double SIDEBAR_W = 380;
constexpr double MIN_TEXT = 15; // smallest font size used on the stream

enum class Align { Left, Center, Right };

inline String widen(const std::string& s) {
    return Unicode::FromUTF8(s);
}

inline String format_clock(int seconds) {
    bool negative = seconds < 0;
    int a = std::abs(seconds);
    String res = negative ? U"-" : U"";
    if (a >= 3600) res += U"{}:{:0>2}:{:0>2}"_fmt(a / 3600, a / 60 % 60, a % 60);
    else res += U"{}:{:0>2}"_fmt(a / 60, a % 60);
    return res;
}

inline String format_signed(double v) {
    if (std::abs(v) < 0.05) return U"0";
    if (std::abs(v - std::round(v)) < 0.05) return U"{:+}"_fmt((int)std::round(v));
    return U"{:+.1f}"_fmt(v);
}

inline String format_points(double p) {
    if (std::abs(p - std::round(p)) < 1e-6) return U"{}"_fmt((int)std::round(p));
    return U"{:.1f}"_fmt(p);
}

inline double fs(double size) {
    return Max(MIN_TEXT, size);
}

/*
    @brief geometry of a match card
    vertical:   header / board / board / graph
    horizontal: header / board board (side by side) / graph
    player information is shown either in a panel beside each board or in a strip above it
*/
struct CardMetrics {
    bool horizontal = false;
    bool side = false; // player panel beside the board instead of a strip above it
    double board = 0;  // board size
    double s = 1;      // scale for texts and paddings
    double pad = 0, header = 0, strip = 0, side_w = 0, gap = 0, graph = 0;
    double width = 0, height = 0;

    static CardMetrics make(bool horizontal, bool side, double board) {
        CardMetrics m;
        m.horizontal = horizontal;
        m.side = side;
        m.board = board;
        m.s = Clamp(board / 330.0, 0.62, 1.45);
        m.pad = 18 * m.s;
        m.header = Max(66.0, 82 * m.s);
        m.strip = side ? 0 : Max(64.0, 78 * m.s);
        m.side_w = side ? Max(126 * m.s, 104.0) : 0;
        m.gap = 12 * m.s;
        m.graph = Max(220.0, 220 * m.s);
        double unit_w = side ? m.side_w + m.gap + board : board;
        double unit_h = m.strip + board;
        if (horizontal) {
            m.width = m.pad * 2 + unit_w * 2 + m.gap * 3;
            m.height = m.pad * 2 + m.header + m.gap + unit_h + m.gap + m.graph;
        } else {
            m.width = m.pad * 2 + unit_w;
            m.height = m.pad * 2 + m.header + (m.gap + unit_h) * 2 + m.gap + m.graph;
        }
        return m;
    }
};

struct GridLayout {
    CardMetrics card;
    int cols = 1, rows = 1;
    double gap = 26;
};

/*
    @brief choose the card style and grid that maximize the board size for n matches
*/
inline GridLayout solve_layout(int n, double area_w, double area_h) {
    GridLayout best;
    double best_score = -1;
    n = std::max(n, 1);
    for (int variant = 0; variant < 4; ++variant) {
        bool horizontal = (variant & 1) != 0;
        bool side = (variant & 2) != 0;
        for (int cols = 1; cols <= n; ++cols) {
            int rows = (n + cols - 1) / cols;
            double gap = 26;
            double lo = 40, hi = 1000;
            for (int it = 0; it < 40; ++it) {
                double mid = (lo + hi) / 2;
                CardMetrics c = CardMetrics::make(horizontal, side, mid);
                bool ok = cols * c.width + (cols - 1) * gap <= area_w && rows * c.height + (rows - 1) * gap <= area_h;
                (ok ? lo : hi) = mid;
            }
            double score = lo * (side ? 1.06 : 1.0); // side panels are easier to read
            if (score > best_score) {
                best_score = score;
                best.card = CardMetrics::make(horizontal, side, std::floor(lo));
                best.cols = cols;
                best.rows = rows;
                best.gap = gap;
            }
        }
    }
    // leftover width goes to the player panels
    if (best.card.side) {
        CardMetrics& c = best.card;
        int units = c.horizontal ? 2 : 1;
        double extra = (area_w - best.cols * c.width - (best.cols - 1) * best.gap) / best.cols / units;
        double add = Clamp(extra, 0.0, c.side_w * 0.5);
        c.side_w += add;
        c.width += add * units;
    }
    return best;
}

struct AppInfo {
    String title;
    String badge; // "DEMO", "LOG REPLAY"
    bool online = false;
    String connection;
    bool debug = false;
    int focus = -1; // index of the match shown alone, -1: all matches
    String toast;   // short notice for the operator
    uint64_t toast_until_ms = 0;
};

class StreamView {
    Font heavy{ FontMethod::MSDF, 48, Typeface::Heavy };
    Font bold{ FontMethod::MSDF, 48, Typeface::Bold };
    Font medium{ FontMethod::MSDF, 48, Typeface::Medium };
    Font cjk{ FontMethod::MSDF, 48, Typeface::CJK_Regular_JP };
    double digit_ratio_heavy = 0.6;
    double digit_ratio_bold = 0.6;
    std::map<std::string, String> display_names; // GGS login -> name shown on the stream
    std::map<std::string, stream::ReplayControl> replay_controls;
    std::string dragging_match;
    std::string selected_match;
    int replay_round = -1;
    bool dragging_visible = false;

public:
    void set_display_names(std::map<std::string, String> names) {
        display_names = std::move(names);
    }
    StreamView() {
        heavy.addFallback(cjk);
        bold.addFallback(cjk);
        medium.addFallback(cjk);
        digit_ratio_heavy = heavy(U"0").region(100).w / 100.0;
        digit_ratio_bold = bold(U"0").region(100).w / 100.0;
    }

    void draw(const TournamentView& t, const AppInfo& info, uint64_t now) {
        if (replay_round != t.round) {
            if (!dragging_match.empty()) Cursor::SetCapture(false);
            replay_controls.clear();
            dragging_match.clear();
            selected_match.clear();
            replay_round = t.round;
        }
        if (!MouseL.pressed()) {
            if (!dragging_match.empty()) Cursor::SetCapture(false);
            dragging_match.clear();
        }
        dragging_visible = false;
        for (auto it = replay_controls.begin(); it != replay_controls.end();) {
            bool present = std::any_of(t.matches.begin(), t.matches.end(), [&](const auto& m) { return m.id == it->first; });
            if (present) ++it;
            else it = replay_controls.erase(it);
        }
        RectF{ 0, 0, SCENE_W, SCENE_H }.draw(col::bg);
        draw_header(t, info, now);
        RectF sidebar{ SCENE_W - MARGIN - SIDEBAR_W, HEADER_H + 28, SIDEBAR_W, SCENE_H - HEADER_H - 28 - MARGIN };
        bool focused = 0 <= info.focus && info.focus < (int)t.matches.size();
        bool results = t.status == RoundStatus::Over && !t.rankings.empty() && !focused;
        RectF main{ MARGIN, HEADER_H + 28, results ? SCENE_W - MARGIN * 2 : sidebar.x - MARGIN - 26, sidebar.h };
        if (results) {
            draw_results(t, main);
        } else {
            draw_sidebar(t, sidebar);
            draw_matches(t, main, now, focused ? info.focus : -1);
        }
        if (!dragging_visible && !dragging_match.empty()) {
            Cursor::SetCapture(false);
            dragging_match.clear();
        }
        if (info.debug) draw_debug(t, info);
    }

private:
    String player_name(const std::string& login) const {
        auto it = display_names.find(login);
        return it == display_names.end() ? widen(login) : it->second;
    }

    /*
        text helpers
    */
    // shrink the text to max_w, but not below MIN_TEXT; longer texts are cut with an ellipsis
    static String fit(const Font& font, const String& s, double& size, double max_w) {
        if (max_w <= 0) return s;
        double w = font(s).region(size).w;
        if (w <= max_w) return s;
        double smallest = Min(size, MIN_TEXT);
        if (size * max_w / w >= smallest) {
            size *= max_w / w;
            return s;
        }
        size = smallest;
        String cut = s;
        while (cut.size() > 1 && font(cut + U"\u2026").region(size).w > max_w) cut.pop_back();
        return cut + U"\u2026";
    }

    static RectF text(const Font& font, const String& str, double size, Align align, const Vec2& p, const ColorF& color, double max_w = 0) {
        if (str.isEmpty()) return RectF{ p, 0, 0 };
        const String s = fit(font, str, size, max_w);
        switch (align) {
        case Align::Left: return font(s).draw(size, Arg::leftCenter = p, color);
        case Align::Center: return font(s).draw(size, Arg::center = p, color);
        default: return font(s).draw(size, Arg::rightCenter = p, color);
        }
    }

    // fixed advance for digits so that numbers do not jitter
    static RectF mono(const Font& font, double ratio, const String& s, double size, Align align, const Vec2& p, const ColorF& color, double max_w = 0) {
        double advance = 0;
        auto wide = [](char32 c) { return IsDigit(c) || c == U'+' || c == U'-'; };
        for (char32 c : s) advance += wide(c) ? 1.0 : 0.55;
        if (max_w > 0 && advance > 0) size = Min(size, max_w / (ratio * advance));
        double dw = ratio * size;
        double nw = dw * 0.55;
        double total = 0;
        for (char32 c : s) total += wide(c) ? dw : nw;
        double x = align == Align::Left ? p.x : align == Align::Center ? p.x - total / 2 : p.x - total;
        RectF region{ x, p.y - size * 0.6, total, size * 1.2 };
        for (char32 c : s) {
            double w = wide(c) ? dw : nw;
            font(String(1, c)).draw(size, Arg::center = Vec2{ x + w / 2, p.y }, color);
            x += w;
        }
        return region;
    }

    static void disc(const Vec2& c, double r, int color, double xscale = 1.0) {
        s3d::Ellipse e{ c, r * xscale, r };
        if (color == BLACK) {
            e.draw(col::disc_black);
        } else {
            e.draw(col::disc_white);
            e.drawFrame(Max(1.0, r * 0.05), ColorF{ 0.0, 0.25 });
        }
    }

    /*
        header bar
    */
    void draw_header(const TournamentView& t, const AppInfo& info, uint64_t now) {
        RectF{ 0, 0, SCENE_W, HEADER_H }.draw(col::header);
        RectF{ 0, HEADER_H - 1, SCENE_W, 1 }.draw(col::border);
        double cy = HEADER_H / 2;
        double x = MARGIN + 4;
        RectF title = text(bold, info.title, 27, Align::Left, Vec2{ x, cy }, col::text, 740);
        x = title.rightX() + 28;
        if (!info.badge.isEmpty()) {
            x = text(medium, info.badge, 16, Align::Left, Vec2{ x, cy }, col::sub).rightX() + 28;
        }
        String round_label = t.round > 0 ? (t.rounds_total > 0 ? U"Round {} / {}"_fmt(t.round, t.rounds_total) : U"Round {}"_fmt(t.round)) : U"";
        switch (t.status) {
        case RoundStatus::Playing:
            text(medium, round_label, 23, Align::Left, Vec2{ x, cy }, col::text);
            break;
        case RoundStatus::Break: {
            RectF r = text(medium, round_label + U" complete", 22, Align::Left, Vec2{ x, cy }, col::sub);
            double nx = r.rightX() + 26;
            if (t.next_round_ms > now) {
                int left = (int)((t.next_round_ms - now + 999) / 1000);
                RectF l = text(medium, U"Next round in", 22, Align::Left, Vec2{ nx, cy }, col::sub);
                mono(bold, digit_ratio_bold, format_clock(left), 26, Align::Left, Vec2{ l.rightX() + 12, cy }, col::text);
            } else {
                text(medium, U"Next round starting", 22, Align::Left, Vec2{ nx, cy }, col::text);
            }
            break;
        }
        case RoundStatus::Over:
            text(medium, U"Tournament complete", 22, Align::Left, Vec2{ x, cy }, col::text);
            break;
        default:
            text(medium, U"Standby", 20, Align::Left, Vec2{ x, cy }, col::sub);
            break;
        }

        double rx = SCENE_W - MARGIN;
        RectF clock = mono(bold, digit_ratio_bold, DateTime::Now().format(U"HH:mm"), 29, Align::Right, Vec2{ rx, cy }, col::text);
        rx = clock.x - 28;
        if (now < info.toast_until_ms) {
            text(bold, info.toast, 20, Align::Right, Vec2{ rx, cy }, col::warn);
        } else if (!info.online) {
            text(bold, U"OFFLINE: " + info.connection, 18, Align::Right, Vec2{ rx, cy }, col::loss, 560);
        } else if (!t.matches.empty() && (t.status == RoundStatus::Playing || t.status == RoundStatus::Break)) {
            text(medium, U"{} / {} matches complete"_fmt(t.finished_matches(), t.matches.size()), 19, Align::Right, Vec2{ rx, cy }, col::sub);
        }
    }

    /*
        standings sidebar
    */
    void draw_sidebar(const TournamentView& t, const RectF& r) {
        RectF{ r.x, r.y, 1, r.h }.draw(col::border);
        RectF standings = r;
        if (t.status == RoundStatus::Break && !t.next_pairings.empty()) {
            double h = Min(66 + 34.0 * t.next_pairings.size(), r.h * 0.5);
            standings.h -= h;
            draw_pairings(t, RectF{ r.x, standings.bottomY(), r.w, h });
        }
        draw_standings(t, standings);
    }

    /*
        pairings of the next round, shown during a break
    */
    void draw_pairings(const TournamentView& t, const RectF& r) {
        double x0 = r.x + 20, x1 = r.rightX() - 20;
        RectF{ x0, r.y, x1 - x0, 1 }.draw(col::border);
        double y = r.y + 30;
        text(bold, U"Next round", 22, Align::Left, Vec2{ x0, y }, col::text);
        text(medium, U"Round {}"_fmt(t.next_round), 16, Align::Right, Vec2{ x1, y }, col::sub);
        y += 22;
        double rh = Min(34.0, (r.bottomY() - 8 - y) / t.next_pairings.size());
        double size = Clamp(rh * 0.58, MIN_TEXT, 20.0);
        double cx = r.centerX();
        for (size_t i = 0; i < t.next_pairings.size(); ++i) {
            const auto& p = t.next_pairings[i];
            double cy = y + rh * (i + 0.5);
            text(bold, player_name(p.player[0]), size, Align::Right, Vec2{ cx - 20, cy }, col::text, cx - 20 - x0);
            text(medium, U"vs", size * 0.85, Align::Center, Vec2{ cx, cy }, col::faint);
            text(bold, player_name(p.player[1]), size, Align::Left, Vec2{ cx + 20, cy }, col::text, x1 - cx - 20);
        }
    }

    void draw_standings(const TournamentView& t, const RectF& r) {
        double x0 = r.x + 20, x1 = r.rightX() - 20;
        double y = r.y + 32;
        text(bold, U"Standings", 26, Align::Left, Vec2{ x0, y }, col::text);
        if (t.status == RoundStatus::Over) {
            text(medium, U"Final", 16, Align::Right, Vec2{ x1, y }, col::sub);
        } else if (t.round > 0) {
            int done = t.status == RoundStatus::Break ? t.round : t.round - 1;
            if (done > 0) text(medium, U"After round {}"_fmt(done), 16, Align::Right, Vec2{ x1, y }, col::sub);
        }
        y += 38;
        double c_rank = x0 + 12, c_move = x0 + 33, c_name = x0 + 46, c_pts = x0 + 180, c_wdl = x0 + 248, c_disc = x1;
        text(bold, U"PTS", 15, Align::Center, Vec2{ c_pts, y }, col::faint);
        text(bold, U"W-D-L", 15, Align::Center, Vec2{ c_wdl, y }, col::faint);
        text(bold, U"DISC", 15, Align::Right, Vec2{ c_disc, y }, col::faint);
        y += 16;
        RectF{ x0, y, x1 - x0, 1 }.draw(col::border);
        y += 4;

        if (t.rankings.empty()) {
            text(medium, U"waiting for rankings", 18, Align::Center, Vec2{ r.centerX(), y + 40 }, col::faint);
            return;
        }
        double avail = r.bottomY() - 12 - y;
        double rh = Clamp(avail / (double)t.rankings.size(), 22.0, 52.0);
        double size = Clamp(rh * 0.44, MIN_TEXT, 22.0);
        for (size_t i = 0; i < t.rankings.size(); ++i) {
            const auto& e = t.rankings[i];
            double cy = y + rh * (i + 0.5);
            if (cy + rh / 2 > r.bottomY() - 6) break;
            RectF{ x0, cy + rh / 2 - 0.5, x1 - x0, 1 }.draw(col::border);
            ColorF rank_color = e.rank == 1 && t.status == RoundStatus::Over ? col::gold : col::sub;
            text(bold, U"{}"_fmt(e.rank), size, Align::Center, Vec2{ c_rank, cy }, rank_color);
            int move = t.rank_change(e.row.name, e.rank);
            if (move != 0) {
                double a = Clamp(size * 0.28, 4.0, 6.0);
                if (move > 0) Triangle{ Vec2{ c_move, cy - a }, Vec2{ c_move + a, cy + a * 0.8 }, Vec2{ c_move - a, cy + a * 0.8 } }.draw(col::win);
                else Triangle{ Vec2{ c_move, cy + a }, Vec2{ c_move - a, cy - a * 0.8 }, Vec2{ c_move + a, cy - a * 0.8 } }.draw(col::loss);
            }
            text(medium, player_name(e.row.name), size, Align::Left, Vec2{ c_name, cy }, col::text, c_pts - 24 - c_name);
            text(bold, format_points(e.row.points), size, Align::Center, Vec2{ c_pts, cy }, col::text);
            if (e.row.has_record) {
                text(medium, U"{}-{}-{}"_fmt(e.row.win, e.row.draw, e.row.loss), size * 0.92, Align::Center, Vec2{ c_wdl, cy }, col::sub, 82);
            }
            if (e.row.has_discs) {
                text(medium, format_signed(e.row.discs), size * 0.92, Align::Right, Vec2{ c_disc, cy }, col::sub, 58);
            }
        }
    }

    /*
        Final results read in order: winner, medal places, remaining players.
        Names and points lead; records are supporting information.
    */
    void draw_results(const TournamentView& t, const RectF& area) {
        int n = (int)t.rankings.size();
        if (n == 0) return;
        const double x = area.x + 28;
        const double width = area.w - 56;
        text(medium, U"Final results", 32, Align::Left, Vec2{ x, area.y + 52 }, col::text);
        text(medium, U"{} players  /  {} rounds"_fmt(n, t.round), 18, Align::Right,
            Vec2{ x + width, area.y + 57 }, col::sub);
        RectF{ x, area.y + 98, width, 2 }.draw(col::board_frame);

        // More than twelve entrants use balanced columns; no ranking is omitted.
        int columns = (n + 11) / 12;
        int rows = (n + columns - 1) / columns;
        double gap = 40;
        double cw = (width - (columns - 1) * gap) / columns;
        double top = area.y + 161;
        auto row_weight = [](int rank) { return rank == 1 ? 1.75 : 1 < rank && rank <= 3 ? 1.18 : 1.0; };
        double max_weight = 0;
        for (int column = 0; column < columns; ++column) {
            double weight = 0;
            for (int i = column * rows; i < Min(n, (column + 1) * rows); ++i) weight += row_weight(t.rankings[i].rank);
            max_weight = Max(max_weight, weight);
        }
        double row_unit = Min(100.0, (area.bottomY() - top - 20) / max_weight);
        for (int column = 0; column < columns; ++column) {
            double cx = x + column * (cw + gap);
            double rank_x = cx + 20;
            double name_x = cx + cw * 0.10;
            double points_x = cx + cw * 0.52;
            double wins_x = cx + cw * 0.62;
            double draws_x = cx + cw * 0.70;
            double losses_x = cx + cw * 0.78;
            double discs_x = cx + cw - 20;
            double hy = top - 28;
            double heading_size = columns >= 3 ? 15 : 18;
            text(medium, columns >= 3 ? U"#" : U"Rank", heading_size, Align::Left, Vec2{ rank_x, hy }, col::sub);
            text(medium, U"Player", heading_size, Align::Left, Vec2{ name_x, hy }, col::sub);
            text(medium, U"Points", heading_size, Align::Center, Vec2{ points_x, hy }, col::sub);
            text(medium, U"Wins", heading_size, Align::Center, Vec2{ wins_x, hy }, col::sub);
            text(medium, U"Draws", heading_size, Align::Center, Vec2{ draws_x, hy }, col::sub);
            text(medium, U"Losses", heading_size, Align::Center, Vec2{ losses_x, hy }, col::sub);
            text(medium, columns >= 3 ? U"Avg. diff." : U"Avg. disc diff.", heading_size, Align::Right,
                Vec2{ discs_x, hy }, col::sub, discs_x - losses_x - 40);
            double row_y = top;
            for (int row = 0; row < rows; ++row) {
                int i = column * rows + row;
                if (i >= n) break;
                const auto& e = t.rankings[i];
                bool champion = e.rank == 1;
                bool medal = 1 < e.rank && e.rank <= 3;
                RectF line{ cx, row_y, cw, row_unit * row_weight(e.rank) };
                double y = line.centerY();
                double name_size = champion ? Min(columns == 1 ? 72.0 : 60.0, line.h * 0.50)
                    : medal ? Min(44.0, line.h * 0.45) : Clamp(line.h * 0.37, 21.0, 28.0);
                double points_size = champion ? name_size * 0.88 : medal ? Min(42.0, line.h * 0.43) : name_size;
                double rank_size = champion ? name_size * 0.65 : medal ? 30.0 : 22.0;
                double secondary_size = champion ? 21.0 : medal ? 20.0 : 18.0;
                const Font& name_font = champion ? heavy : medal ? bold : medium;
                const Font& points_font = champion ? heavy : medal ? bold : medium;
                text(medium, U"{:0>2}"_fmt(e.rank), rank_size, Align::Left, Vec2{ rank_x, y }, champion ? col::gold : col::faint);
                text(name_font, player_name(e.row.name), name_size, Align::Left,
                    Vec2{ name_x, y }, col::text, points_x - name_x - 70);
                text(points_font, format_points(e.row.points), points_size, Align::Center, Vec2{ points_x, y }, col::text);
                text(medium, e.row.has_record ? U"{}"_fmt(e.row.win) : U"\u2014", secondary_size, Align::Center, Vec2{ wins_x, y }, col::sub);
                text(medium, e.row.has_record ? U"{}"_fmt(e.row.draw) : U"\u2014", secondary_size, Align::Center, Vec2{ draws_x, y }, col::sub);
                text(medium, e.row.has_record ? U"{}"_fmt(e.row.loss) : U"\u2014", secondary_size, Align::Center, Vec2{ losses_x, y }, col::sub);
                // Disc difference deserves emphasis when it separates equal points.
                auto separates_tie = [&](int other) {
                    if (other < 0 || other >= n) return false;
                    const auto& r = t.rankings[other].row;
                    return e.row.has_discs && r.has_discs && std::abs(e.row.points - r.points) < 1e-6
                        && std::abs(e.row.discs - r.discs) > 1e-6;
                };
                bool tie_break = separates_tie(i - 1) || separates_tie(i + 1);
                text(tie_break ? bold : medium, e.row.has_discs ? format_signed(e.row.discs) : U"\u2014",
                    secondary_size + (tie_break ? 2 : 0), Align::Right, Vec2{ discs_x, y }, tie_break ? col::text : col::sub);
                RectF{ line.x, line.bottomY() - 1, line.w, champion ? 2.0 : 1.0 }.draw(col::border);
                row_y = line.bottomY();
            }
        }
    }

    /*
        match cards
    */
    void draw_matches(const TournamentView& t, const RectF& area, uint64_t now, int focus) {
        if (t.matches.empty()) {
            String msg = t.status == RoundStatus::Over ? U"Tournament finished"
                       : t.status == RoundStatus::Break ? U"Next round will start soon"
                       : t.status == RoundStatus::Playing ? U"Waiting for games"
                       : U"Waiting for the tournament";
            text(heavy, msg, 36, Align::Center, area.center(), col::sub);
            return;
        }
        // a focused match is drawn alone, as large as possible
        int n = focus >= 0 ? 1 : (int)t.matches.size();
        GridLayout g = solve_layout(n, area.w, area.h);
        const CardMetrics& c = g.card;
        double grid_h = g.rows * c.height + (g.rows - 1) * g.gap;
        double y0 = area.y + (area.h - grid_h) / 2;
        for (int row = 0; row < g.rows; ++row) {
            int in_row = std::min(g.cols, n - row * g.cols);
            double row_w = in_row * c.width + (in_row - 1) * g.gap;
            double x0 = area.x + (area.w - row_w) / 2;
            for (int i = 0; i < in_row; ++i) {
                const MatchView& m = t.matches[focus >= 0 ? focus : row * g.cols + i];
                RectF card{ x0 + i * (c.width + g.gap), y0 + row * (c.height + g.gap), c.width, c.height };
                draw_card(m, card, c, now);
            }
        }
    }

    void draw_card(const MatchView& m, const RectF& card, const CardMetrics& c, uint64_t now) {
        const double s = c.s;
        card.draw(m.finished ? col::panel2 : col::panel);
        RectF{ card.x, card.y, card.w, Max(2.0, 3 * s) }.draw(m.finished ? col::sub : col::board_frame);
        RectF{ card.x, card.bottomY() - 1, card.w, 1 }.draw(col::border);
        RectF inner = card.stretched(-c.pad);
        draw_card_header(m, RectF{ inner.x, inner.y, inner.w, c.header }, s);
        double y = inner.y + c.header + c.gap;
        RectF graph{ inner.x, y + (c.horizontal ? 1 : 2) * (c.strip + c.board + c.gap), inner.w, c.graph };
        auto& playback = replay_controls[m.id];
        handle_replay_input(m, graph, s, now, playback);
        stream::ReplayFrame frames[2] = { playback.frame(m, 0, now), playback.frame(m, 1, now) };
        auto draw_unit = [&](int i, double x, double uy, bool panel_left) {
            const GameView& g = m.game[i];
            if (c.side) {
                double bx = panel_left ? x + c.side_w + c.gap : x;
                double px = panel_left ? x : x + c.board + c.gap;
                draw_side(m, g, frames[i], RectF{ px, uy, c.side_w, c.board }, s, now, panel_left);
                draw_board(g, frames[i], RectF{ bx, uy, c.board, c.board }, now);
            } else {
                draw_strip(m, g, frames[i], RectF{ x, uy, c.board, c.strip }, s, now);
                draw_board(g, frames[i], RectF{ x, uy + c.strip, c.board, c.board }, now);
            }
        };
        double unit_w = c.side ? c.side_w + c.gap + c.board : c.board;
        if (c.horizontal) {
            draw_unit(0, inner.x, y, true);
            draw_unit(1, inner.rightX() - unit_w, y, false);
            y += c.strip + c.board + c.gap;
        } else {
            for (int i = 0; i < 2; ++i) {
                draw_unit(i, inner.x, y, true);
                y += c.strip + c.board + c.gap;
            }
        }
        // The longer game's cursor keeps advancing if the shorter game has ended.
        int cursor = -1;
        for (const auto& f : frames) if (f.active) cursor = Max(cursor, f.ply);
        draw_graph(m, graph, s, cursor, playback.paused());
    }

    void draw_card_header(const MatchView& m, const RectF& r, double s) {
        double band_h = Max(32.0, 36 * s);
        double y1 = r.y + band_h / 2 + 5;
        double y2 = r.bottomY() - Max(13.0, 14 * s);
        int da = m.total_discs(0), db = m.total_discs(1);
        double score_size = 32 * s;
        String sa = U"{}"_fmt(da), sb = U"{}"_fmt(db);
        double half_score = Max(sa.size(), sb.size()) * digit_ratio_heavy * score_size + 14 * s;
        double result = 0;
        bool has_result = m.result_sum(result);
        double name_w = r.w / 2 - half_score - Max(10.0, 10 * s);
        for (int i = 0; i < 2; ++i) {
            bool left = i == 0;
            double edge = left ? r.x : r.rightX();
            double dir = left ? 1 : -1;
            Align a = left ? Align::Left : Align::Right;
            RectF name_band{ left ? edge : edge - name_w, y1 - band_h / 2, name_w, band_h };
            name_band.draw(col::accent[i]);
            double inset = Max(10.0, 10 * s);
            text(bold, player_name(m.player[i]), fs(25 * s), a, Vec2{ edge + dir * inset, y1 }, col::on_accent, name_w - inset * 2);
            if (has_result) {
                double mine = i == 0 ? result : -result;
                String label = mine > 0 ? U"WIN" : mine < 0 ? U"LOSS" : U"DRAW";
                ColorF c = mine > 0 ? col::text : col::sub;
                text(medium, label, fs(16 * s), a, Vec2{ edge + dir * 4 * s, y2 }, c);
            }
        }
        double cx = r.centerX();
        mono(heavy, digit_ratio_heavy, sa, score_size, Align::Right, Vec2{ cx - 12 * s, y1 }, col::text);
        RectF{ cx - 5 * s, y1 - 1.5 * s, 10 * s, 3 * s }.draw(col::faint);
        mono(heavy, digit_ratio_heavy, sb, score_size, Align::Left, Vec2{ cx + 12 * s, y1 }, col::text);
        if (has_result) {
            text(medium, U"final " + format_signed(result), fs(16 * s), Align::Center, Vec2{ cx, y2 }, col::sub);
        } else if (!m.joined()) {
            text(bold, U"connecting", fs(16 * s), Align::Center, Vec2{ cx, y2 }, col::faint);
        }
    }

    struct PlayerStyle {
        ColorF accent;
        bool to_move;
    };

    PlayerStyle player_style(const MatchView& m, const GameView& g, int color) const {
        const std::string& name = g.name[color];
        int idx = name == m.player[0] ? 0 : name == m.player[1] ? 1 : -1;
        return PlayerStyle{ idx >= 0 ? col::accent[idx] : col::text, g.active && !g.finished && g.pos.to_move == color };
    }

    // final score of one game for the player with this color ("+6")
    // win / loss is decided only by the sum of both games, so it is not shown per game
    void draw_game_score(const GameView& g, int color, double size, Align a, const Vec2& p) {
        double r_black = g.final_result_black();
        double mine = color == BLACK ? r_black : -r_black;
        text(heavy, format_signed(mine), size, a, p, col::text);
    }

    static String engine_evaluation(const GameView& g, const stream::ReplayFrame& f, int color) {
        double value = 0;
        if (!g.engine_eval_at(color, f.active ? f.ply : g.ply, value)) return U"\u2014";
        if (std::abs(value) < 0.05) value = 0; // avoid displaying a rounded negative zero
        return U"{:+.1f}"_fmt(value);
    }

    void draw_engine_evaluation(const GameView& g, const stream::ReplayFrame& f, int color,
        double size, Align a, const Vec2& p, double max_w) {
        String value = engine_evaluation(g, f, color);
        if (value == U"\u2014") text(heavy, value, size, a, p, col::faint, max_w);
        else mono(heavy, digit_ratio_heavy, value, size, a, p, col::text, max_w);
    }

    /*
        player panel beside a board: black on top, white below
    */
    void draw_side(const MatchView& m, const GameView& g, const stream::ReplayFrame& f, const RectF& r, double s, uint64_t now, bool panel_left) {
        r.draw(col::panel2);
        RectF{ r.x, r.centerY(), r.w, 1 }.draw(col::border);
        if (f.active) {
            double size = fs(15 * s);
            RectF band{ r.x, r.centerY() - size * 0.9, r.w, size * 1.8 };
            band.draw(col::replay);
            text(heavy, U"REPLAY", size, Align::Center, band.center(), col::bg, band.w - 8 * s);
        }
        for (int color = 0; color < 2; ++color) {
            RectF h{ r.x, r.y + (color == BLACK ? 0 : r.h / 2), r.w, r.h / 2 };
            PlayerStyle st = player_style(m, g, color);
            double x = h.x + 12 * s;
            double dr = 8 * s;
            double band_h = Max(30.0, 32 * s);
            double top_pad = f.active ? fs(15 * s) * 0.9 + 8 : Max(8.0, 8 * s);
            double y_name = h.y + top_pad + band_h / 2;
            RectF{ h.x, y_name - band_h / 2, h.w, band_h }.draw(st.accent);
            if (st.to_move) {
                double bar_w = Max(6.0, 6 * s);
                RectF{ panel_left ? h.rightX() - bar_w : h.x, h.y, bar_w, h.h }.draw(st.accent);
            }
            Circle{ x + dr, y_name, dr + 3 * s }.draw(col::panel);
            disc(Vec2{ x + dr, y_name }, dr, color);
            double name_x = x + dr * 2 + 8 * s;
            if (!g.active) continue;
            text(bold, player_name(g.name[color]), fs(19 * s), Align::Left, Vec2{ name_x, y_name }, col::on_accent, h.rightX() - 10 * s - name_x);
            double y_clock = h.y + h.h * 0.81;
            double clock_h = bold(U"00:00").region(fs(23 * s)).h;
            double value_top = y_name + band_h / 2 + 6;
            double value_bottom = y_clock - clock_h / 2 - 6;
            double value_size = Min(48 * s, Max(15.0, (value_bottom - value_top) * 48 / heavy(U"64").region(48).h));
            draw_engine_evaluation(g, f, color, value_size, Align::Left,
                Vec2{ x, (value_top + value_bottom) / 2 }, h.w - 24 * s);
            if (g.finished) {
                draw_game_score(g, color, fs(23 * s), Align::Left, Vec2{ x, y_clock });
            } else if (g.has_clock) {
                int sec = g.remaining_seconds(color, now);
                ColorF cc = sec <= 60 ? col::loss : sec <= 180 ? col::warn : st.to_move ? col::text : col::sub;
                mono(bold, digit_ratio_bold, format_clock(sec), fs(23 * s), Align::Left, Vec2{ x, y_clock }, cc);
            }
            if (g.last_pass && !g.finished && g.pos.to_move != color) {
                text(heavy, U"PASS", fs(15 * s), Align::Left, Vec2{ x, h.y + h.h * 0.93 }, col::warn);
            }
        }
    }

    /*
        player strip above a board (compact layout): black left, white right
    */
    void draw_strip(const MatchView& m, const GameView& g, const stream::ReplayFrame& f, const RectF& r, double s, uint64_t now) {
        RectF area{ r.x, r.y, r.w, r.h - 4 * s };
        area.draw(col::panel2);
        RectF{ area.centerX(), area.y, 1, area.h }.draw(col::border);
        double half = area.w / 2;
        double band_h = Max(30.0, 32 * s);
        double bar_h = Max(6.0, 6 * s);
        double y1 = area.y + band_h / 2;
        double y2 = (area.y + band_h + area.bottomY() - bar_h) / 2;
        for (int color = 0; color < 2; ++color) {
            bool left = color == BLACK;
            RectF h{ left ? area.x : area.x + half, area.y, half, area.h };
            PlayerStyle st = player_style(m, g, color);
            RectF{ h.x, h.y, h.w, band_h }.draw(st.accent);
            if (st.to_move) RectF{ h.x, h.bottomY() - bar_h, h.w, bar_h }.draw(st.accent);
            double dir = left ? 1 : -1;
            double edge = left ? h.x + 8 * s : h.rightX() - 8 * s;
            double inner = left ? h.rightX() - 8 * s : h.x + 8 * s;
            Align a = left ? Align::Left : Align::Right;
            Align b = left ? Align::Right : Align::Left;
            double dr = 7 * s;
            Circle{ edge + dir * dr, y1, dr + 2 * s }.draw(col::panel);
            disc(Vec2{ edge + dir * dr, y1 }, dr, color);
            if (!g.active) continue;
            double name_x = edge + dir * (dr * 2 + 6 * s);
            text(bold, player_name(g.name[color]), fs(18 * s), a, Vec2{ name_x, y1 }, col::on_accent, std::abs(inner - name_x));
            double value_w = half - 16 * s;
            if (f.active) {
                double tag_w = heavy(U"REPLAY").region(fs(14 * s)).w + fs(14 * s);
                value_w -= tag_w / 2 + 6;
            } else {
                String secondary = g.finished ? format_signed(color == BLACK ? g.final_result_black() : -g.final_result_black())
                    : g.has_clock ? format_clock(g.remaining_seconds(color, now)) : U"";
                double secondary_w = bold(secondary).region(fs(19 * s)).w;
                value_w -= secondary_w + 8 * s;
            }
            draw_engine_evaluation(g, f, color, fs(26 * s), f.active ? a : b,
                Vec2{ f.active ? edge : inner, y2 }, Max(1.0, value_w));
            if (g.finished && !f.active) {
                draw_game_score(g, color, fs(19 * s), a, Vec2{ edge, y2 });
            } else if (!g.finished && g.has_clock) {
                int sec = g.remaining_seconds(color, now);
                ColorF cc = sec <= 60 ? col::loss : sec <= 180 ? col::warn : st.to_move ? col::text : col::sub;
                mono(bold, digit_ratio_bold, format_clock(sec), fs(19 * s), a, Vec2{ edge, y2 }, cc);
            }
        }
        if (f.active) {
            // Keep the replay tag between the two engines' estimates.
            double size = fs(14 * s);
            double w = heavy(U"REPLAY").region(size).w + size;
            RectF tag{ area.centerX() - w / 2, y2 - size * 0.75, w, size * 1.5 };
            tag.draw(col::replay);
            heavy(U"REPLAY").draw(size, Arg::center = tag.center(), col::bg);
        }
    }

    void draw_board(const GameView& g, const stream::ReplayFrame& f, const RectF& r, uint64_t now) {
        double w = r.w;
        const ColorF& board_color = f.active ? col::board_replay : col::board;
        const ColorF& grid_color = f.active ? col::grid_replay : col::grid;
        r.draw(f.active ? col::replay : col::board_frame);
        // coordinates only when the board is large enough to show them at a readable size
        const bool coords = w >= 460;
        double fr = coords ? Max(24.0, w * 0.042) : Max(4.0, w * 0.028);
        RectF gr = r.stretched(-fr);
        gr.draw(board_color);
        double cell = gr.w / HW;
        double lw = Max(1.0, w * 0.003);
        for (int i = 1; i < HW; ++i) {
            RectF{ gr.x + i * cell - lw / 2, gr.y, lw, gr.h }.draw(grid_color);
            RectF{ gr.x, gr.y + i * cell - lw / 2, gr.w, lw }.draw(grid_color);
        }
        for (int sy : { 2, 6 }) {
            for (int sx : { 2, 6 }) Circle{ gr.x + sx * cell, gr.y + sy * cell, Max(1.5, cell * 0.06) }.draw(grid_color);
        }
        if (coords) {
            double size = Max(MIN_TEXT, fr * 0.62);
            ColorF c = f.active ? ColorF{ col::bg, 0.8 } : ColorF{ 1.0, 0.55 };
            for (int i = 0; i < HW; ++i) {
                medium(String(1, U'a' + i)).draw(size, Arg::center = Vec2{ gr.x + (i + 0.5) * cell, r.y + fr / 2 }, c);
                medium(String(1, U'1' + i)).draw(size, Arg::center = Vec2{ r.x + fr / 2, gr.y + (i + 0.5) * cell }, c);
            }
        }
        if (!g.active) return;

        // position to show: live position, or a replay of the finished game
        const ggs::Position* pos = &g.pos;
        const ggs::Position* prev = g.has_prev ? &g.prev_pos : nullptr;
        double t = 1.0;
        int last_cell = g.last_cell;
        if (f.active) {
            pos = &g.history[f.ply];
            prev = f.ply >= 1 ? &g.history[f.ply - 1] : nullptr;
            t = f.t;
            last_cell = f.ply >= 1 ? g.history_cell[f.ply - 1] : -1;
        } else if (prev && now >= g.changed_ms) {
            t = Min(1.0, (now - g.changed_ms) / (double)stream::FLIP_ANIMATION_MS);
        }
        double rad = cell * 0.41;
        for (int i = 0; i < HW2; ++i) {
            uint64_t bit = ggs::cell_bit(i);
            int cur = (pos->black & bit) ? BLACK : (pos->white & bit) ? WHITE : -1;
            if (cur < 0) continue;
            int before = (t < 1.0 && prev) ? ((prev->black & bit) ? BLACK : (prev->white & bit) ? WHITE : -1) : cur;
            Vec2 c{ gr.x + (i % HW + 0.5) * cell, gr.y + (i / HW + 0.5) * cell };
            if (before == cur) {
                disc(c, rad, cur);
            } else if (before < 0) {
                disc(c, rad * EaseOutCubic(Min(1.0, t * 1.6)), cur);
            } else {
                disc(c, rad, t < 0.5 ? before : cur, Max(0.06, std::abs(std::cos(Math::Pi * t))));
            }
        }
        if (last_cell >= 0) {
            Circle{ gr.x + (last_cell % HW + 0.5) * cell, gr.y + (last_cell / HW + 0.5) * cell, cell * 0.11 }.draw(col::last_move);
        }

        if (f.active) {
            // replay progress in the bottom frame
            double progress = f.n > 0 ? (double)f.ply / f.n : 1.0;
            RectF{ gr.x, gr.bottomY() + fr * 0.25, gr.w, fr * 0.5 }.draw(ColorF{ 0.0, 0.0, 0.0, 0.30 });
            RectF{ gr.x, gr.bottomY() + fr * 0.25, gr.w * progress, fr * 0.5 }.draw(col::bg);
        }
    }

    struct GraphMetrics {
        double size, label_h, heading_y, legend_y;
        RectF plot, play;
    };

    GraphMetrics graph_metrics(const RectF& r, double s, bool has_divergence) const {
        double size = fs(14 * s);
        double h = Max(medium(U"Evaluation").region(size).h, bold(U"Diverged").region(size).h);
        double heading = r.y + h / 2 + 2;
        double legend = heading + h + 5;
        double plot_y = legend + h / 2 + 8 + (has_divergence ? h + 11 : 0);
        return { size, h, heading, legend,
            RectF{ r.x + 40, plot_y, r.w - 50, r.bottomY() - 24 - plot_y },
            RectF{ r.rightX() - 30, heading - (h + 4) / 2, 28, h + 4 } };
    }

    void handle_replay_input(const MatchView& m, const RectF& r, double s, uint64_t now, stream::ReplayControl& playback) {
        if (stream::ReplayControl::length(m) == 0) return;
        auto geometry = graph_metrics(r, s, m.first_divergent_ply() >= 0);
        if (selected_match == m.id && KeySpace.down()) {
            if (playback.paused()) playback.resume(m, now);
            else {
                int ply = 0;
                for (int i = 0; i < 2; ++i) ply = Max(ply, playback.frame(m, i, now).ply);
                playback.seek(m, ply);
            }
        }
        if (playback.paused() && geometry.play.mouseOver()) {
            Cursor::RequestStyle(CursorStyle::Hand);
            if (MouseL.down()) playback.resume(m, now);
        }
        RectF hit{ geometry.plot.x, geometry.plot.y, geometry.plot.w, geometry.plot.h + 18 };
        if (hit.mouseOver()) {
            Cursor::RequestStyle(CursorStyle::ResizeLeftRight);
            if (MouseL.down()) {
                dragging_match = m.id;
                selected_match = m.id;
                Cursor::SetCapture(true);
            }
        }
        if (dragging_match == m.id) {
            dragging_visible = true;
            Cursor::RequestStyle(CursorStyle::ResizeLeftRight);
            playback.seek_fraction(m, (Cursor::PosF().x - geometry.plot.x) / geometry.plot.w);
        }
    }

    void draw_graph(const MatchView& m, const RectF& r, double s, int cursor_ply, bool paused) {
        RectF{ r.x, r.y, r.w, 1 }.draw(col::border);
        int split = m.first_divergent_ply();
        const auto geometry = graph_metrics(r, s, split >= 0);
        const double label_size = geometry.size;
        const double label_h = geometry.label_h;
        const double heading_y = geometry.heading_y;
        text(medium, U"Evaluation", label_size, Align::Left,
            Vec2{ r.x + 4, heading_y }, col::sub, r.w - 8);
        if (cursor_ply >= 0) {
            text(bold, U"Replay", label_size, Align::Right,
                Vec2{ paused ? geometry.play.x - 8 : r.rightX() - 4, heading_y }, col::text, r.w - 90);
            if (paused) {
                geometry.play.draw(geometry.play.mouseOver() ? col::bg : col::panel);
                geometry.play.drawFrame(1, col::border);
                Vec2 c = geometry.play.center();
                Triangle{ Vec2{ c.x - 4, c.y - 6 }, Vec2{ c.x - 4, c.y + 6 }, Vec2{ c.x + 6, c.y } }.draw(col::text);
            }
        }
        // Identify the source of each estimate by name, rather than L/R codes.
        const double legend_y = geometry.legend_y;
        for (int i = 0; i < 2; ++i) {
            double x = r.x + 4 + i * r.w / 2;
            RectF{ x, legend_y - 2, 35, 4 }.draw(col::accent[i]);
            text(medium, player_name(m.player[i]), label_size, Align::Left,
                Vec2{ x + 43, legend_y }, col::text, r.w / 2 - 53);
        }
        RectF plot = geometry.plot;
        // Always draw the complete series, including moves beyond the replay cursor.
        auto sa = m.eval_series(0);
        auto sb = m.eval_series(1);
        double maxabs = 0;
        for (auto& p : sa) maxabs = Max(maxabs, std::abs(p.second));
        for (auto& p : sb) maxabs = Max(maxabs, std::abs(p.second));
        double range = 4;
        for (double cand : { 4.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0, 96.0, 128.0 }) {
            range = cand;
            if (cand >= maxabs * 1.05) break;
        }
        double xmax = Max(1, m.x_max());
        auto px = [&](double ply) { return plot.x + plot.w * Clamp(ply / xmax, 0.0, 1.0); };
        auto py = [&](double v) { return plot.centerY() - Clamp(v / range, -1.0, 1.0) * plot.h / 2; };

        plot.draw(col::panel);
        if (split >= 0) {
            double x = px(split);
            RectF{ x, plot.y, plot.rightX() - x, plot.h }.draw(col::graph_diverged);
        }
        RectF{ plot.x, plot.y, plot.w, 1 }.draw(ColorF{ col::text, 0.10 });
        RectF{ plot.x, plot.bottomY() - 1, plot.w, 1 }.draw(ColorF{ col::text, 0.10 });
        RectF{ plot.x, plot.centerY() - 0.5, plot.w, 1 }.draw(ColorF{ col::text, 0.28 });
        double size = fs(13 * s);
        double lx = plot.x - 7;
        text(medium, U"+{}"_fmt((int)range), size, Align::Right, Vec2{ lx, plot.y + size * 0.35 }, col::sub);
        text(medium, U"0", size, Align::Right, Vec2{ lx, plot.centerY() }, col::faint);
        text(medium, U"-{}"_fmt((int)range), size, Align::Right, Vec2{ lx, plot.bottomY() - size * 0.35 }, col::sub);
        const double axis_y = plot.bottomY() + 13;
        text(medium, U"0", size, Align::Left, Vec2{ plot.x, axis_y }, col::sub);
        int middle_ply = (int)(xmax / 2);
        text(medium, U"{}"_fmt(middle_ply), size, Align::Center, Vec2{ px(middle_ply), axis_y }, col::sub);
        text(medium, U"{}"_fmt((int)xmax), size, Align::Right, Vec2{ plot.rightX(), axis_y }, col::sub);

        auto draw_series = [&](const std::vector<std::pair<int, double>>& series, double sign, int idx) {
            if (series.empty()) return;
            LineString ls;
            for (auto& p : series) ls << Vec2{ px(p.first), py(sign * p.second) };
            if (ls.size() >= 2) ls.draw(Max(3.0, 3.0 * s), col::accent[idx]);
        };
        // both in player[0]'s frame: upper half = player[0] ahead
        draw_series(sb, -1.0, 1);
        draw_series(sa, 1.0, 0);
        if (split >= 0) {
            double x = px(split);
            bool reached = cursor_ply < 0 || cursor_ply >= split;
            String label = U"Diverged";
            double w = bold(label).region(label_size).w + 16;
            RectF tag{ Clamp(x - w / 2, plot.x, plot.rightX() - w), plot.y - label_h - 11, w, label_h + 4 };
            // The pointer and divider share the exact move coordinate, even when the label is clamped at an edge.
            RectF{ x - 1, plot.y, 2, plot.h }.draw(col::sub);
            tag.draw(reached ? col::text : col::panel);
            if (!reached) tag.drawFrame(1.5, col::sub);
            text(bold, label, label_size, Align::Center, tag.center(), reached ? col::on_accent : col::sub);
            Triangle{ Vec2{ x - 4, tag.bottomY() }, Vec2{ x + 4, tag.bottomY() }, Vec2{ x, plot.y - 1 } }.draw(reached ? col::text : col::sub);
        }
        if (cursor_ply >= 0) {
            double x = px(cursor_ply);
            RectF{ x - 1.5, plot.y, 3, plot.h }.draw(col::text);
        }
    }

    void draw_debug(const TournamentView& t, const AppInfo& info) {
        RectF r{ 40, 100, 1300, 920 };
        r.draw(ColorF{ 0, 0, 0, 0.85 });
        double y = r.y + 14;
        String head = U"[F1] debug  |  {}  |  round {} status {}  |  matches {}  players {}"_fmt(
            info.connection, t.round, (int)t.status, t.matches.size(), t.players.size());
        medium(head).draw(16, Vec2{ r.x + 12, y }, col::warn);
        y += 26;
        for (const auto& m : t.matches) {
            String line = U"{} {} vs {}  joined {}/{}  ply {}/{}  finished {}  watch attempts {}"_fmt(
                widen(m.id), widen(m.player[0]), widen(m.player[1]), m.game[0].active, m.game[1].active,
                m.game[0].ply, m.game[1].ply, m.finished, m.watch_attempts);
            medium(line).draw(15, Vec2{ r.x + 12, y }, col::text);
            y += 20;
        }
        y += 8;
        int lines = (int)((r.bottomY() - y - 8) / 18);
        for (const auto& l : app_log().tail(std::max(0, lines))) {
            medium(widen(l)).draw(14, Vec2{ r.x + 12, y }, col::sub);
            y += 18;
        }
    }
};

} // namespace view
