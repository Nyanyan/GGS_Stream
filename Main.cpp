# include <Siv3D.hpp> // Siv3D v0.6.15
#include "board.hpp"
#include "ggs_net.hpp"
#include "stream_model.hpp"
#include "stream_view.hpp"

#define GGS_HOST "skatgame.net"
#define GGS_PORT 5000

/*
    info.txt (next to the executable)
        line 1: GGS username
        line 2: GGS password
        line 3: tournament id
        line 4: title shown on the stream (optional)

    command line options
        --demo              run an offline simulation instead of connecting to GGS
        --players N         number of players in the demo (even, default 6)
        --speed X           demo speed (default 3)
        --seed N            demo random seed
        --title TEXT        title shown on the stream
        --fullscreen        start in fullscreen
        --capture PREFIX    save screenshots as PREFIX_<sec>.png ...
        --capture-at A,B,C  ... at these times in seconds
        --quit-after SEC    exit automatically
*/
struct Config {
    std::string username;
    std::string password;
    std::string tournament_id;
    std::string title;
    bool demo = false;
    int demo_players = 6;
    double demo_speed = 3.0;
    uint64_t demo_seed = 0;
    bool fullscreen = false;
    std::string capture_prefix;
    std::vector<double> capture_at;
    double quit_after = -1.0;
    std::string error;
};

static Config load_config() {
    Config cfg;
    cfg.demo_seed = tim();
    const Array<String>& args = System::GetCommandLineArgs();
    for (size_t i = 0; i < args.size(); ++i) {
        const String& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i].toUTF8() : std::string(); };
        if (a == U"--demo") cfg.demo = true;
        else if (a == U"--players") cfg.demo_players = ParseOr<int32>(Unicode::FromUTF8(next()), 6);
        else if (a == U"--speed") cfg.demo_speed = ParseOr<double>(Unicode::FromUTF8(next()), 3.0);
        else if (a == U"--seed") cfg.demo_seed = ParseOr<uint64>(Unicode::FromUTF8(next()), 0);
        else if (a == U"--title") cfg.title = next();
        else if (a == U"--fullscreen") cfg.fullscreen = true;
        else if (a == U"--capture") cfg.capture_prefix = next();
        else if (a == U"--capture-at") {
            for (const auto& t : Unicode::FromUTF8(next()).split(U',')) cfg.capture_at.push_back(ParseOr<double>(t, 0.0));
        } else if (a == U"--quit-after") cfg.quit_after = ParseOr<double>(Unicode::FromUTF8(next()), -1.0);
    }
    if (cfg.demo) {
        cfg.tournament_id = "7";
        if (cfg.title.empty()) cfg.title = "GGS Synchro Tournament";
        return cfg;
    }
    std::ifstream ifs("info.txt");
    if (!ifs) {
        cfg.error = "info.txt not found. Put username, password and tournament id (one per line) in info.txt, or start with --demo.";
        return cfg;
    }
    std::string lines[4];
    for (auto& l : lines) {
        if (!std::getline(ifs, l)) break;
        if (&l == &lines[0] && ggs::starts_with(l, "\xEF\xBB\xBF")) l.erase(0, 3); // UTF-8 BOM
        l = ggs::trim(l);
    }
    cfg.username = lines[0];
    cfg.password = lines[1];
    cfg.tournament_id = lines[2];
    if (cfg.title.empty()) cfg.title = lines[3];
    if (cfg.username.empty() || cfg.password.empty() || cfg.tournament_id.empty()) {
        cfg.error = "info.txt must contain username, password and tournament id (one per line).";
    }
    if (cfg.title.empty()) cfg.title = "GGS Tournament " + cfg.tournament_id;
    return cfg;
}

static void setup_window() {
    Window::SetTitle(U"GGS Stream");
    System::SetTerminationTriggers(UserAction::CloseButtonClicked); // Esc must not end the stream
    Scene::Resize(1920, 1080);
    Scene::SetResizeMode(ResizeMode::Keep);
    Scene::SetBackground(view::col::bg);
    Scene::SetLetterbox(view::col::bg);
    Window::SetStyle(WindowStyle::Sizable);
    // fit the window into the work area keeping 16:9
    const Size work = System::GetCurrentMonitor().workArea.size;
    const double scaling = Window::GetState().scaling;
    const double avail_w = work.x / scaling - 16, avail_h = work.y / scaling - 48;
    const double k = Min(1.0, Min(avail_w / 1920.0, avail_h / 1080.0));
    Window::Resize(Size{ (int32)(1920 * k), (int32)(1080 * k) });
    Window::Centering();
}

void Main()
{
    setup_window();
    bit_init();
    mobility_init();
    flip_init();

    Config cfg = load_config();
    FileSystem::CreateDirectories(U"logs");
    app_log().open(("logs/ggs_stream_" + DateTime::Now().format(U"yyyyMMdd-HHmmss").toUTF8() + ".log"));
    if (cfg.fullscreen) Window::SetFullscreen(true);

    view::StreamView view;
    view::AppInfo info;
    info.title = Unicode::FromUTF8(cfg.title);
    info.demo = cfg.demo;

    if (!cfg.error.empty()) {
        app_log().write("ERROR", cfg.error);
        const Font font{ FontMethod::MSDF, 48, Typeface::Bold };
        while (System::Update()) {
            font(Unicode::FromUTF8(cfg.error)).draw(28, Rect{ 160, 400, 1600, 400 }, view::col::loss);
        }
        return;
    }

    std::unique_ptr<MessageSource> source;
    if (cfg.demo) {
        source = std::make_unique<DemoSource>(cfg.demo_players, cfg.demo_speed, cfg.tournament_id, cfg.demo_seed, tim());
    } else {
        auto conn = std::make_unique<GGSConnection>(GGS_HOST, GGS_PORT, cfg.username, cfg.password,
            std::vector<std::string>{ "ms /os", "ts client -", "ts vt100 -", "chann + .tourney", "chann + /os" });
        conn->start();
        source = std::move(conn);
    }

    stream::StreamState state(cfg.tournament_id);
    state.send = [&](const std::string& cmd) { source->send(cmd); };
    state.log = [](const std::string& s) { app_log().write("STATE", s); };

    int seen_epoch = 0;
    bool was_online = false;
    size_t next_capture = 0;
    String capture_path;
    std::sort(cfg.capture_at.begin(), cfg.capture_at.end());

    while (System::Update()) {
        const uint64_t now = tim();
        const bool online = source->online();
        if (online && source->epoch() != seen_epoch) {
            seen_epoch = source->epoch();
            state.on_connected(now);
        } else if (!online && was_online) {
            state.on_disconnected();
        }
        was_online = online;
        for (const auto& msg : source->poll(now)) state.on_message(msg, now);
        state.tick(now);

        if (KeyF1.down()) info.debug = !info.debug;
        if (KeyF11.down()) Window::SetFullscreen(!Window::GetState().fullscreen);

        info.online = online;
        info.connection = Unicode::FromUTF8(source->status());
        view.draw(state.t, info, now);

        if (!capture_path.isEmpty() && ScreenCapture::HasNewFrame()) {
            ScreenCapture::GetFrame().save(capture_path);
            capture_path.clear();
        }
        if (!cfg.capture_prefix.empty() && next_capture < cfg.capture_at.size() && Scene::Time() >= cfg.capture_at[next_capture]) {
            ScreenCapture::RequestCurrentFrame();
            capture_path = Unicode::FromUTF8(cfg.capture_prefix) + U"_{}.png"_fmt((int)cfg.capture_at[next_capture]);
            ++next_capture;
        }
        if (cfg.quit_after > 0 && Scene::Time() >= cfg.quit_after) System::Exit();
    }
    source.reset();
}
