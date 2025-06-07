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
	Window::SetTitle(U"GGS Stream");
	Scene::SetBackground(Palette::Black);

	std::string username, password;



	Console.open();

	init_board_processing();
	ggs_client_init(username, password);

	const Font font{ 30 };
	const Font small_font{ 15 };
	



	std::future<std::vector<std::string>> ggs_message_f;
	std::vector<std::string> matches = {};
	std::vector<GGS_Board> ggs_boards;
	std::vector<std::pair<double, double>> last_scores;
	int playing_round = -2;
	Stopwatch keepalive_timer{ StartImmediately::Yes };
	std::vector<Rank_Player> rankings;

	ggs_send_message(sock, "t /td r " + tournament_id + "\n");
	ggs_send_message(sock, "ts match\n");

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
                if (server_reply.size()) {
					std::string os_info = ggs_get_os_info(server_reply);

					int round = ggs_get_starting_round(server_reply, tournament_id);
					if (round != -1) {
						std::cout << "round " << round << " start!" << std::endl;
						playing_round = round;
						matches.clear();
						ggs_boards.clear();
						ggs_send_message(sock, "ts match\n");
						ggs_send_message(sock, "t /td r " + tournament_id + "\n");
					}

                    // // match start
                    // if (ggs_is_tournament_match_start(server_reply)) {
					// 	std::string game_id = ggs_get_tournament_match_id(server_reply);
                    //     ggs_print_info("match start! " + game_id);
					// 	ggs_watch_game(game_id);
					// 	matches.emplace_back(game_id);
                    // }

					// // match end
					// if (ggs_is_tournament_match_end(server_reply)) {
					// 	std::string game_id = ggs_get_tournament_match_id(server_reply);
                    //     ggs_print_info("match end! " + game_id);
                    // }

					// match info
					if (ggs_is_match_info(server_reply)) {
						matches = ggs_get_match_ids(server_reply);
						for (const auto& game_id : matches) {
							ggs_watch_game(game_id);
						}
					}

					// tournament rankings
					if (ggs_is_ranking(server_reply, tournament_id)) {
						rankings = ggs_client_get_ranking(server_reply, tournament_id);
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
									return b.game_id == ggs_board.game_id;
								});
							if (it != ggs_boards.end()) {
								size_t idx = std::distance(ggs_boards.begin(), it);
								ggs_boards[idx] = ggs_board;
								if (ggs_board.player_to_move == BLACK) {
									last_scores[idx].second = ggs_board.last_score;
								} else if (ggs_board.player_to_move == WHITE) {
									last_scores[idx].first = ggs_board.last_score;
								}
							} else {
								std::cerr << "emplace back new board " << ggs_board.game_id << std::endl;
								ggs_boards.emplace_back(ggs_board);
								last_scores.emplace_back(std::make_pair(-127.0, -127.0));
								if (ggs_board.player_to_move == BLACK) {
									last_scores[last_scores.size() - 1].second = ggs_board.last_score;
								} else if (ggs_board.player_to_move == WHITE) {
									last_scores[last_scores.size() - 1].first = ggs_board.last_score;
								}
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
		int square_horizontal_margin = 200;
		int square_vertical_margin = 50;
		int right_margin = 350;
		int top_margin = 0;

		// ボード間にマージンだけスペースが空くように計算
		double drawable_width = window_size.x - right_margin;
		double drawable_height = window_size.y - top_margin;
		double cellWidth = (drawable_width - square_horizontal_margin * cols) / cols;
		double cellHeight = (drawable_height - square_vertical_margin * (rows + 1)) / rows;
		double squareSize = std::min(cellWidth, cellHeight);

		// ボードエリアの幅と高さ
		double boards_total_width = cols * squareSize + cols * square_horizontal_margin;
		double boards_total_height = rows * squareSize + (rows + 1) * square_vertical_margin;

		// 左上のオフセット（中央揃え、右マージン考慮）
		double offset_x = (drawable_width - boards_total_width) / 2.0;
		double offset_y = top_margin + (drawable_height - boards_total_height) / 2.0;

		for (size_t i = 0; i < boards_size; ++i) {
			int row = i % 2;
			int col = i / 2;
			double x = offset_x + col * (squareSize + square_horizontal_margin) + square_horizontal_margin;
			double y = offset_y + row * (squareSize + square_vertical_margin) + square_vertical_margin;
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
			for (size_t board_idx = 0; board_idx < ggs_boards.size(); ++board_idx) {
				const auto& board = ggs_boards[board_idx];
				const std::string& gid = board.game_id;
				size_t first_dot = gid.find('.');
				if (first_dot == std::string::npos) continue;
				size_t second_dot = gid.find('.', first_dot + 1);
				if (second_dot == std::string::npos) continue;
				std::string prefix = gid.substr(0, second_dot);
				if (prefix == match_id) {
					int row = board.synchro_id;
					double x = offset_x + col * (squareSize + square_horizontal_margin) + square_horizontal_margin;
					double y = offset_y + row * (squareSize + square_vertical_margin) + square_vertical_margin;

					// 64ビット整数のboard.board.playerを上位ビットから1ビットずつ走査
					double cell = squareSize / 8.0;
					for (int idx = 0; idx < 64; ++idx) {
						// 上位ビットから
						int bit_pos = 63 - idx;
						int r = idx / 8;
						int c = idx % 8;
						double cx = x + c * cell + cell / 2.0;
						double cy = y + r * cell + cell / 2.0;
						if ((board.board.player >> bit_pos) & 1) {
							if (board.player_to_move == BLACK) {
								Circle(cx, cy, cell * 0.4).draw(Palette::Black);
							} else if (board.player_to_move == WHITE) {
								Circle(cx, cy, cell * 0.4).draw(Palette::White);
							}
						}
						if ((board.board.opponent >> bit_pos) & 1) {
							if (board.player_to_move == BLACK) {
								Circle(cx, cy, cell * 0.4).draw(Palette::White);
							} else if (board.player_to_move == WHITE) {
								Circle(cx, cy, cell * 0.4).draw(Palette::Black);
							}
						}
					}
					int black_count = 0, white_count = 0;
					for (int idx = 0; idx < 64; ++idx) {
						int bit_pos = 63 - idx;
						if ((board.board.player >> bit_pos) & 1) {
							if (board.player_to_move == BLACK) {
								++black_count;
							} else if (board.player_to_move == WHITE) {
								++white_count;
							}
						}
						if ((board.board.opponent >> bit_pos) & 1) {
							if (board.player_to_move == BLACK) {
								++white_count;
							} else if (board.player_to_move == WHITE) {
								++black_count;
							}
						}
					}

					// ゲームID
					small_font(Unicode::Widen(board.game_id)).draw(Arg::topRight(x - 5, y), Palette::White);

					// 石数
					String stone_info = Format(black_count) + U" - " + Format(white_count);
					small_font(stone_info).draw(Arg::topRight(x - 5, y + 30), Palette::White);

					String black_info = U"Black: " + Unicode::Widen(board.player_black);
					small_font(black_info).draw(Arg::topRight(x - 5, y + 60), Palette::White);
					if (last_scores[board_idx].first != -127) {
						String black_score = U"Score: " + Format(last_scores[board_idx].first);
						small_font(black_score).draw(Arg::topRight(x - 5, y + 80), Palette::White);
					}

					String white_info = U"White: " + Unicode::Widen(board.player_white);
					small_font(white_info).draw(Arg::topRight(x - 5, y + 110), Palette::White);
					if (last_scores[board_idx].second != -127) {
						String white_score = U"Score: " + Format(last_scores[board_idx].second);
						small_font(white_score).draw(Arg::topRight(x - 5, y + 130), Palette::White);
					}
				}
			}
		}

		double info_x = window_size.x - right_margin + 30;
		font(U"Playing Round: " + Format(playing_round)).draw(info_x, 10, Palette::White);
		double ranking_y = 70;
		font(U"Ranking").draw(info_x, ranking_y, Palette::White);
		for (size_t i = 0; i < rankings.size(); ++i) {
			// rankings[i]は "名前 勝ち点 w d l" の形式を想定
			String ranking_info = Format(i + 1) + U". " + Unicode::Widen(rankings[i].name) + U" " + Unicode::Widen(rankings[i].point) + U" points " + Unicode::Widen(rankings[i].wdl);
			small_font(ranking_info).draw(info_x, ranking_y + 50 + i * 30, Palette::White);
		}
	}
}
