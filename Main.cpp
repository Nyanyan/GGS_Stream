# include <Siv3D.hpp> // Siv3D v0.6.15
#include "ggs.hpp"

void init_board_processing() {
    bit_init();
    mobility_init();
    flip_init();
}

void Main()
{
	Size window_size = Size(1920, 1080);
    Window::Resize(window_size);

	// 背景の色を設定する | Set the background color
	Scene::SetBackground(Palette::Black);

	std::string username, password;



	Console.open();

	init_board_processing();
	ggs_client_init(username, password);

	const Font font{ 30 };
	const Font small_font{ 15 };
	



	std::future<std::vector<std::string>> ggs_message_f;
	std::vector<std::string> matches;
	std::vector<GGS_Board> ggs_boards;
	int playing_round = -2;
	Stopwatch keepalive_timer{ StartImmediately::Yes };

	while (System::Update())
	{
		// 2分おきにkeepaliveメッセージを送信
		if (keepalive_timer.sF() >= 120.0) {
			ggs_send_message(sock, "\n");
			keepalive_timer.restart();
		}

		// check server reply
		std::vector<std::string> server_replies;
        if (ggs_message_f.valid()) {
            if (ggs_message_f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                server_replies = ggs_message_f.get();
            }
        } else {
            ggs_message_f = std::async(std::launch::async, ggs_receive_message, &sock); // ask ggs message
        }

		// GGS
		if (server_replies.size()) {
            // match start
            for (std::string server_reply: server_replies) {
				std::cout << server_reply << std::endl;
                if (server_reply.size()) {
					std::string os_info = ggs_get_os_info(server_reply);

					int round = ggs_get_starting_round(server_reply, tournament_id);
					if (round != -1) {
						std::cout << "round " << round << "start!" << std::endl;
						playing_round = round;
						matches.clear();
						ggs_boards.clear();
					}

                    // match start
                    if (ggs_is_tournament_match_start(server_reply)) {
						std::string game_id = ggs_get_tournament_match_id(server_reply);
                        ggs_print_info("match start! " + game_id);
						ggs_watch_game(game_id);
						matches.emplace_back(game_id);
                    }

					// match end
					if (ggs_is_tournament_match_end(server_reply)) {
						std::string game_id = ggs_get_tournament_match_id(server_reply);
                        ggs_print_info("match end! " + game_id);
                    }

					// board info
                    if (ggs_is_board_info(os_info)) {
                        GGS_Board ggs_board = ggs_get_board(server_reply);
                        if (ggs_board.is_valid()) {
							ggs_print_info("ggs board synchro id " + std::to_string(ggs_board.synchro_id));
							std::cerr << ggs_board.game_id << "/" << ggs_board.synchro_id << std::endl;
							ggs_board.board.print();

							auto it = std::find_if(ggs_boards.begin(), ggs_boards.end(),
								[&](const GGS_Board& b) {
									return b.game_id == ggs_board.game_id && b.synchro_id == ggs_board.synchro_id;
								});
							if (it != ggs_boards.end()) {
								*it = ggs_board;
							} else {
								ggs_boards.emplace_back(ggs_board);
							}
                        }
                    }

                }
            }
		}

		// Drawing
		int boards_size = 6; // ggs_boards.size();
		int cols = (boards_size + 1) / 2;
		if (cols == 0) cols = 1;
		int rows = 2;
		int square_margin = 50;
		int top_margin = 100; // 上側に100ピクセル分スペース

		// ボード間にsquare_marginだけスペースが空くように計算
		double cellWidth = (window_size.x - square_margin * (cols + 1)) / cols;
		double cellHeight = (window_size.y - top_margin - square_margin * (rows + 1)) / rows;
		double squareSize = std::min(cellWidth, cellHeight);

		// ボード全体の幅・高さ
		double boards_total_width = cols * squareSize + (cols - 1) * square_margin;
		double boards_total_height = rows * squareSize + (rows - 1) * square_margin;

		// 左上のオフセット（中央揃え）
		double offset_x = (window_size.x - boards_total_width) / 2.0;
		double offset_y = top_margin + ((window_size.y - top_margin) - boards_total_height) / 2.0;

		for (size_t i = 0; i < boards_size; ++i) {
			int row = i % 2;
			int col = i / 2;
			double x = offset_x + col * (squareSize + square_margin);
			double y = offset_y + row * (squareSize + square_margin);
			RectF(x, y, squareSize, squareSize).draw(Palette::Green);
			// 8x8 grid
			double cell = squareSize / 8.0;
			for (int r = 0; r < 8; ++r) {
				for (int c = 0; c < 8; ++c) {
					RectF(x + c * cell, y + r * cell, cell, cell)
						.drawFrame(1, Palette::White);
				}
			}
		}

		// matchesを走査し、そのインデックスをcolとする
		for (size_t col = 0; col < matches.size(); ++col) {
			const std::string& match_id = matches[col];
			auto it = std::find_if(ggs_boards.begin(), ggs_boards.end(),
				[&](const GGS_Board& b) {
					// game_idの2個目の"."以前を抽出
					const std::string& gid = b.game_id;
					size_t first_dot = gid.find('.');
					if (first_dot == std::string::npos) return false;
					size_t second_dot = gid.find('.', first_dot + 1);
					if (second_dot == std::string::npos) return false;
					std::string prefix = gid.substr(0, second_dot);
					return prefix == match_id;
				});
			if (it != ggs_boards.end()) {
				int row = it->synchro_id;
				double x = offset_x + col * (squareSize + square_margin);
				double y = offset_y + row * (squareSize + square_margin);

				// 表示するテキスト
				String info = Unicode::Widen(it->game_id) + U" " + Unicode::Widen(it->player_black) + U" vs. " + Unicode::Widen(it->player_white);

				// テキストをボードの上に描画
				small_font(info).draw(x, y - 30, Palette::White);

				// 64ビット整数のit->board.playerを上位ビットから1ビットずつ走査
				double cell = squareSize / 8.0;
				for (int idx = 0; idx < 64; ++idx) {
					// 上位ビットから
					int bit_pos = 63 - idx;
					int r = idx / 8;
					int c = idx % 8;
					double cx = x + c * cell + cell / 2.0;
					double cy = y + r * cell + cell / 2.0;
					if ((it->board.player >> bit_pos) & 1) {
						if (it->player_to_move == BLACK) {
							Circle(cx, cy, cell * 0.4).draw(Palette::Black);
						} else if (it->player_to_move == WHITE) {
							Circle(cx, cy, cell * 0.4).draw(Palette::White);
						}
					}
					if ((it->board.opponent >> bit_pos) & 1) {
						if (it->player_to_move == BLACK) {
							Circle(cx, cy, cell * 0.4).draw(Palette::White);
						} else if (it->player_to_move == WHITE) {
							Circle(cx, cy, cell * 0.4).draw(Palette::Black);
						}
					}
				}
			}
		}

		font(U"Playing Round: " + Format(playing_round)).draw(Arg::topRight(1900, 10), Palette::White);
	}
}
