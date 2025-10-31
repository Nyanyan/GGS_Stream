# include <Siv3D.hpp> // Siv3D v0.6.15
#include "ggs.hpp"

// スコア履歴を保存する構造体
struct ScoreHistory {
	std::vector<std::tuple<int, double, double>> history; // (合計石数, 黒番スコア, 白番スコア)
	void add(int total_stones, double black_score, double white_score) {
		history.emplace_back(total_stones, black_score, white_score);
	}
	void clear() {
		history.clear();
	}
};

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

	std::string username, password, tournament_id;

	std::ifstream info_file("info.txt");
	if (info_file.is_open()) {
		std::getline(info_file, username);
		std::getline(info_file, password);
		std::getline(info_file, tournament_id);
		info_file.close();
	} else {
		std::cerr << "Error: Could not open info.txt" << std::endl;
	}

	Console.open();

	init_board_processing();
	ggs_client_init(username, password);

	const Font font{ 30 };
	const Font small_font{ 15 };
	



	std::future<std::vector<std::string>> ggs_message_f;
	std::vector<std::string> matches = {};
	std::vector<GGS_Board> ggs_boards;
	std::vector<std::pair<double, double>> last_scores;
	std::vector<ScoreHistory> score_histories; // スコア履歴を追加
	int playing_round = -2;
	Stopwatch keepalive_timer{ StartImmediately::Yes };
	std::vector<Rank_Player> rankings;

	ggs_send_message(sock, "t /td r " + tournament_id + "\n");
	ggs_send_message(sock, "ts match\n");

	while (System::Update())
	{
		// 1分おきにkeepaliveメッセージを送信
		if (keepalive_timer.sF() >= 60.0) {
			// ggs_send_message(sock, "\n");
			ggs_send_message(sock, "t /td r " + tournament_id + "\n");
			keepalive_timer.restart();
		}

		for (const auto& game_id : matches) {
			bool found = false;
			for (const auto& board : ggs_boards) {
				if (board.game_id.find(game_id) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				ggs_watch_game(game_id);
				break;
			}
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
		for (std::string server_reply: server_replies) {
			if (server_reply.size()) {
				std::cerr << "----- REPLY START -----" << std::endl;
				std::cerr << server_reply << std::endl;
				std::cerr << "----- REPLY END -----" << std::endl;
				std::string os_info = ggs_get_os_info(server_reply);

				int round = ggs_get_starting_round(server_reply, tournament_id);
				if (round != -1) {
					std::cout << "round " << round << " start!" << std::endl;
					playing_round = round;
					matches.clear();
					ggs_boards.clear();
					last_scores.clear();
					score_histories.clear(); // スコア履歴もクリア
					ggs_send_message(sock, "ts match\n");
					ggs_send_message(sock, "t /td r " + tournament_id + "\n");
				}

				int round2 = ggs_get_ending_round(server_reply, tournament_id);
				if (round2 != -1) {
					std::cout << "round " << round2 << " finished!" << std::endl;
					playing_round = round2;
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
				}

				// tournament rankings
				if (ggs_is_ranking(server_reply, tournament_id)) {
					rankings = ggs_client_get_ranking(server_reply, tournament_id);
				}

				// board info
				if (ggs_is_board_info(os_info)) {
					GGS_Board ggs_board = ggs_get_board(server_reply);
					if (ggs_board.is_valid()) {
						std::cerr << ggs_board.game_id << std::endl;
						ggs_board.board.print();

						auto it = std::find_if(ggs_boards.begin(), ggs_boards.end(),
							[&](const GGS_Board& b) {
								return b.game_id == ggs_board.game_id;
							});
						if (it != ggs_boards.end()) {
							size_t idx = std::distance(ggs_boards.begin(), it);
							ggs_boards[idx] = ggs_board;
							
							// 合計石数を計算
							int total_stones = __popcnt64(ggs_board.board.player) + __popcnt64(ggs_board.board.opponent);
							
							if (ggs_board.player_to_move == BLACK) {
								last_scores[idx].second = ggs_board.last_score;
								score_histories[idx].add(total_stones, last_scores[idx].first, ggs_board.last_score);
							} else if (ggs_board.player_to_move == WHITE) {
								last_scores[idx].first = ggs_board.last_score;
								score_histories[idx].add(total_stones, ggs_board.last_score, last_scores[idx].second);
							}
						} else {
							std::cerr << "emplace back new board " << ggs_board.game_id << std::endl;
							ggs_boards.emplace_back(ggs_board);
							last_scores.emplace_back(std::make_pair(-127.0, -127.0));
							score_histories.emplace_back(ScoreHistory()); // 履歴を初期化
							
							// 合計石数を計算
							int total_stones = __popcnt64(ggs_board.board.player) + __popcnt64(ggs_board.board.opponent);
							
							if (ggs_board.player_to_move == BLACK) {
								last_scores[last_scores.size() - 1].second = ggs_board.last_score;
								score_histories[score_histories.size() - 1].add(total_stones, -127.0, ggs_board.last_score);
							} else if (ggs_board.player_to_move == WHITE) {
								last_scores[last_scores.size() - 1].first = ggs_board.last_score;
								score_histories[score_histories.size() - 1].add(total_stones, ggs_board.last_score, -127.0);
							}
						}
					}
				}

			}
		}

		// Drawing
		int boards_size = ggs_boards.size();
		int cols = (boards_size + 1) / 2;
		if (cols == 0) cols = 1;
		int rows = 2;
		int square_horizontal_margin = 200;
		int square_vertical_margin = 50;
		int graph_height = 100; // グラフの高さ
		int right_margin = 350;
		int top_margin = 80;

		// ボード間にマージンだけスペースが空くように計算
		double drawable_width = window_size.x - right_margin;
		double drawable_height = window_size.y - top_margin;
		double cellWidth = (drawable_width - square_horizontal_margin * cols) / cols;
		double cellHeight = (drawable_height - square_vertical_margin * (rows + 1) - graph_height * rows) / rows; // グラフ高さを考慮
		double squareSize = std::min((double)400.0, std::min(cellWidth, cellHeight));

		// ボードエリアの幅と高さ
		double boards_total_width = cols * squareSize + cols * square_horizontal_margin;
		double boards_total_height = rows * (squareSize + graph_height) + (rows + 1) * square_vertical_margin; // グラフ高さを含める

		// 左上のオフセット（中央揃え、右マージン考慮）
		double offset_x = (drawable_width - boards_total_width) / 2.0;
		double offset_y = top_margin + (drawable_height - boards_total_height) / 2.0;

		for (size_t i = 0; i < boards_size; ++i) {
			int row = i % 2;
			int col = i / 2;
			double x = offset_x + col * (squareSize + square_horizontal_margin) + square_horizontal_margin;
			double y = offset_y + row * (squareSize + square_vertical_margin + graph_height) + square_vertical_margin; // グラフ高さを考慮
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
			double x = offset_x + col * (squareSize + square_horizontal_margin) + square_horizontal_margin;
			const std::string& match_id = matches[col];
			std::vector<std::pair<std::string, int>> total_result;
			bool both_finished;
			for (size_t board_idx = 0; board_idx < ggs_boards.size(); ++board_idx) {
				GGS_Board board = ggs_boards[board_idx];
				const std::string& gid = board.game_id;
				size_t first_dot = gid.find('.');
				if (first_dot == std::string::npos) continue;
				size_t second_dot = gid.find('.', first_dot + 1);
				if (second_dot == std::string::npos) continue;
				std::string prefix = gid.substr(0, second_dot);
				if (prefix == match_id) {
					int row = board.synchro_id;
					double y = offset_y + row * (squareSize + square_vertical_margin + graph_height) + square_vertical_margin; // グラフ高さを考慮

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

					if (total_result.size() == 0) {
						total_result.emplace_back(std::make_pair(board.player_black, black_count));
						total_result.emplace_back(std::make_pair(board.player_white, white_count));
						both_finished = board.board.is_end();
					} else {
						for (auto& result : total_result) {
							if (result.first == board.player_black) {
								result.second += black_count;
							} else if (result.first == board.player_white) {
								result.second += white_count;
							}
						}
						both_finished &= board.board.is_end();
					}

					// ゲームID
					small_font(Unicode::Widen(board.game_id)).draw(Arg::topRight(x - 5, y), Palette::White);

					// 石数
					String stone_info = Format(black_count) + U" - " + Format(white_count);
					small_font(stone_info).draw(Arg::topRight(x - 5, y + 30), Palette::White);

					String black_info = U"Black: " + Unicode::Widen(board.player_black);
					if (board.player_to_move == BLACK) {
						black_info = U"* " + black_info;
					}
					small_font(black_info).draw(Arg::topRight(x - 5, y + 60), Palette::White);
					uint64_t seconds_black = board.remaining_seconds_black;
					int minute_black = seconds_black / 60;
					int minute_second_black = seconds_black - minute_black * 60;
					String remaining_time_black = U"Remaining " + Format(minute_black) + U":" + Format(minute_second_black).lpadded(2, U'0');
					small_font(remaining_time_black).draw(Arg::topRight(x - 5, y + 80), Palette::White);
					if (last_scores[board_idx].first != -127) {
						String black_score = U"Score: " + Format(last_scores[board_idx].first);
						small_font(black_score).draw(Arg::topRight(x - 5, y + 100), Palette::White);
					}

					String white_info = U"White: " + Unicode::Widen(board.player_white);
					if (board.player_to_move == WHITE) {
						white_info = U"* " + white_info;
					}
					small_font(white_info).draw(Arg::topRight(x - 5, y + 140), Palette::White);
					uint64_t seconds_white = board.remaining_seconds_white;
					int minute_white = seconds_white / 60;
					int minute_second_white = seconds_white - minute_white * 60;
					String remaining_time_white = U"Remaining " + Format(minute_white) + U":" + Format(minute_second_white).lpadded(2, U'0');
					small_font(remaining_time_white).draw(Arg::topRight(x - 5, y + 160), Palette::White);
					if (last_scores[board_idx].second != -127) {
						String white_score = U"Score: " + Format(last_scores[board_idx].second);
						small_font(white_score).draw(Arg::topRight(x - 5, y + 180), Palette::White);
					}

					// グラフの描画
					double graph_y = y + squareSize;
					const ScoreHistory& history = score_histories[board_idx];
					if (history.history.size() >= 2) {
						// 石数の範囲を計算
						int min_stones = 64, max_stones = 0;
						double min_score = 64.0, max_score = -64.0;
						for (const auto& [stones, black_score, white_score] : history.history) {
							min_stones = std::min(min_stones, stones);
							max_stones = std::max(max_stones, stones);
							if (black_score != -127.0) {
								min_score = std::min(min_score, black_score);
								max_score = std::max(max_score, black_score);
							}
							if (white_score != -127.0) {
								min_score = std::min(min_score, -white_score); // 白は反転
								max_score = std::max(max_score, -white_score);
							}
						}
						min_score -= 2.0;
						max_score += 2.0;
						
						// グラフ背景
						RectF(x, graph_y, squareSize, graph_height).draw(ColorF(0.1, 0.1, 0.1));
						
						// ゼロライン
						if (min_score <= 0 && max_score >= 0) {
							double zero_y = graph_y + graph_height * (1.0 - (0.0 - min_score) / (max_score - min_score));
							Line(x, zero_y, x + squareSize, zero_y).draw(1, ColorF(0.5, 0.5, 0.5));
						}
						
						// スコア推移を描画
						for (size_t i = 1; i < history.history.size(); ++i) {
							auto [stones1, black1, white1] = history.history[i - 1];
							auto [stones2, black2, white2] = history.history[i];
							
							double x1 = x + squareSize * (stones1 - min_stones) / (double)(max_stones - min_stones);
							double x2 = x + squareSize * (stones2 - min_stones) / (double)(max_stones - min_stones);
							
							// 黒番スコア
							if (black1 != -127.0 && black2 != -127.0) {
								double y1 = graph_y + graph_height * (1.0 - (black1 - min_score) / (max_score - min_score));
								double y2 = graph_y + graph_height * (1.0 - (black2 - min_score) / (max_score - min_score));
								Line(x1, y1, x2, y2).draw(2, Palette::Black);
							}
							
							// 白番スコア（反転）
							if (white1 != -127.0 && white2 != -127.0) {
								double y1 = graph_y + graph_height * (1.0 - (-white1 - min_score) / (max_score - min_score));
								double y2 = graph_y + graph_height * (1.0 - (-white2 - min_score) / (max_score - min_score));
								Line(x1, y1, x2, y2).draw(2, Palette::White);
							}
						}
					}
				}
			}
			if (!total_result.empty()) {
				String player1 = Unicode::Widen(total_result[0].first);
				String player2 = Unicode::Widen(total_result[1].first);
				String vs = U" vs ";
				font(player1).draw(Arg::topRight(x + squareSize / 2 - font(vs).region().w / 2, offset_y + square_vertical_margin - 100), Palette::White);
				font(vs).draw(Arg::topCenter(x + squareSize / 2, offset_y + square_vertical_margin - 100), Palette::White);
				font(player2).draw(Arg::topLeft(x + squareSize / 2 + font(vs).region().w / 2, offset_y + square_vertical_margin - 100), Palette::White);
				
				String score1 = Format(total_result[0].second);
				String score2 = Format(total_result[1].second);
				String dash = U" - ";
				font(score1).draw(Arg::topRight(x + squareSize / 2 - font(dash).region().w / 2, offset_y + square_vertical_margin - 50), Palette::White);
				font(dash).draw(Arg::topCenter(x + squareSize / 2, offset_y + square_vertical_margin - 50), Palette::White);
				font(score2).draw(Arg::topLeft(x + squareSize / 2 + font(dash).region().w / 2, offset_y + square_vertical_margin - 50), Palette::White);
				if (both_finished) {
					if (total_result[0].second > total_result[1].second) {
						String winner_mark = U"Win";
						String loser_mark = U"Loss";
						font(winner_mark).draw(Arg::topRight(x + squareSize / 2 - 70, offset_y + square_vertical_margin - 50), Palette::White);
						font(loser_mark).draw(Arg::topLeft(x + squareSize / 2 + 70, offset_y + square_vertical_margin - 50), Palette::White);
					} else if (total_result[1].second > total_result[0].second) {
						String winner_mark = U"Win";
						String loser_mark = U"Loss";
						font(loser_mark).draw(Arg::topRight(x + squareSize / 2 - 70, offset_y + square_vertical_margin - 50), Palette::White);
						font(winner_mark).draw(Arg::topLeft(x + squareSize / 2 + 70, offset_y + square_vertical_margin - 50), Palette::White);
					} else {
						String draw_mark = U"Draw";
						font(draw_mark).draw(Arg::topRight(x + squareSize / 2 - 70, offset_y + square_vertical_margin - 50), Palette::White);
						font(draw_mark).draw(Arg::topLeft(x + squareSize / 2 + 70, offset_y + square_vertical_margin - 50), Palette::White);
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
