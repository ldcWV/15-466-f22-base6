#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(space);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &space);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() : mt(0x15466666) {
	spawn_npcs();
}

glm::vec2 Game::random_point_in_arena(float center_weight, float boundary_thickness) {
	// center_weight = 0 -> spawn completely random
	// center_weight = 1 -> spawn only in center
	glm::vec2 res;
	res.x = glm::mix(ArenaMin.x + 2.0f * boundary_thickness, ArenaMax.x - 2.0f * boundary_thickness, center_weight/2 + (1 - center_weight) * mt() / float(mt.max()));
	res.y = glm::mix(ArenaMin.y + 2.0f * boundary_thickness, ArenaMax.y - 2.0f * boundary_thickness, center_weight/2 + (1 - center_weight) * mt() / float(mt.max()));
	return res;
}

void Game::spawn_npcs() {
	for (int i = 0; i < NUM_NPCS; i++) {
		NPC npc;
		npc.position = random_point_in_arena(0.2f);
		npc.destination = random_point_in_arena(0.0f);
		npcs.push_back(npc);
	}
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//random point in the middle area of the arena:
	player.position = random_point_in_arena(0.2f);

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::update(float elapsed) {
	// update coin stuff
	if (coin_countdown > 0) {
		coin_countdown -= fmin(coin_countdown, elapsed);
		if (coin_countdown == 0) {
			break_countdown = break_duration;
			for (auto& p: players) {
				if (!p.collected_coin) {
					p.revealed_countdown = revealed_duration;
				}
			}
		}
	} else {
		assert(break_countdown > 0);
		break_countdown -= fmin(break_countdown, elapsed);
		if (break_countdown == 0) {
			coin_countdown = coin_duration;
			coin_position = random_point_in_arena(0.2f, CoinRadius);
			for (auto& p: players) {
				p.collected_coin = false;
			}
		}
	}

	static float PI = acos(-1.f);

	// npc position update:
	for (auto &npc : npcs) {
		//if close to destination, pick a new one and possibly rest for a bit
		float dist_to_dest = glm::length(npc.position - npc.destination);
		if (dist_to_dest < PlayerRadius) {
			npc.destination = random_point_in_arena(0.f, 0.f);
			npc.rest_countdown = mt() / float(mt.max()) < 0.5 ? 0.f : 2.f + 5.f * mt() / float(mt.max());
			npc.direction_change_cooldown = 0.f;
		}

		//if currently resting or have a designated direction, go that way
		npc.rest_countdown -= fmin(npc.rest_countdown, elapsed);
		npc.direction_change_cooldown -= fmin(npc.direction_change_cooldown, elapsed);
		if (npc.rest_countdown > 0) continue;
		if (npc.direction_change_cooldown > 0) {
			npc.position += npc.direction * 0.5f * elapsed;
			continue;
		}

		// recompute direction towards destination
		glm::vec2 best_dir = glm::vec2(0.f, 0.f);
		float best_dist = 100000.f;
		for (float angle = 0.f; angle < 2*PI; angle += PI / 4) {
			glm::vec2 dir = glm::vec2(cos(angle), sin(angle));
			float dist = glm::length2(npc.position + dir - npc.destination);
			if (dist < best_dist) {
				best_dir = dir;
				best_dist = dist;
			}
		}
		npc.direction = best_dir;
		npc.direction_change_cooldown = 0.25f + 1.f * mt() / float(mt.max());
		npc.position += npc.direction * 0.5f * elapsed;
	}

	for (auto &p : players) if (!p.dead) {
		p.revealed_countdown -= fmin(p.revealed_countdown, elapsed);
	}

	//player position update:
	for (auto &p : players) if (!p.dead) {
		p.dash_cooldown -= fmin(p.dash_cooldown, elapsed);
		if (p.dash_countdown == 0.f) {
			//move normally
			glm::vec2 dir = glm::vec2(0.0f, 0.0f);
			if (p.controls.left.pressed) dir.x -= 1.0f;
			if (p.controls.right.pressed) dir.x += 1.0f;
			if (p.controls.down.pressed) dir.y -= 1.0f;
			if (p.controls.up.pressed) dir.y += 1.0f;

			if (dir != glm::vec2(0.f, 0.f)) {
				dir = glm::normalize(dir);
			}

			p.position += dir * 0.5f * elapsed;

			//detect dash
			if (p.controls.space.downs && p.dash_cooldown == 0.f) {
				p.dash_countdown = 0.15f;
				p.dash_dir = dir;
				p.dash_cooldown = p.dash_cooldown_duration;
			}
		} else {
			//mid dash, move in dash_dir
			float dash_time = fmin(elapsed, p.dash_countdown);
			p.dash_countdown -= dash_time;

			p.position += p.dash_dir * 3.f * dash_time;
		}

		//reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.space.downs = 0;
	}

	//player collision resolution:
	for (auto &p1 : players) if (!p1.dead) {
		//player/arena collisions:
		if (p1.position.x < ArenaMin.x + PlayerRadius) {
			p1.position.x = ArenaMin.x + PlayerRadius;
		}
		if (p1.position.x > ArenaMax.x - PlayerRadius) {
			p1.position.x = ArenaMax.x - PlayerRadius;
		}
		if (p1.position.y < ArenaMin.y + PlayerRadius) {
			p1.position.y = ArenaMin.y + PlayerRadius;
		}
		if (p1.position.y > ArenaMax.y - PlayerRadius) {
			p1.position.y = ArenaMax.y - PlayerRadius;
		}

		//see if we killed anyone:
		if (p1.dash_countdown > 0.f) {
			for (auto &p2 : players) if(!p2.dead) {
				if (&p1 == &p2) continue;
				glm::vec2 p12 = p2.position - p1.position;
				float len2 = glm::length2(p12);
				if (len2 < (2.f * PlayerRadius) * (2.f * PlayerRadius)) {
					p2.dead = true;
				}
			}
		}

		//did we collect coin?
		if (coin_countdown > 0) {
			if (glm::length(p1.position - coin_position) < PlayerRadius + CoinRadius) {
				p1.collected_coin = true;
			}
		}
	}

	//npc/arena collisions
	for (auto &npc : npcs) {
		if (npc.position.x < ArenaMin.x + PlayerRadius) {
			npc.position.x = ArenaMin.x + PlayerRadius;
		}
		if (npc.position.x > ArenaMax.x - PlayerRadius) {
			npc.position.x = ArenaMax.x - PlayerRadius;
		}
		if (npc.position.y < ArenaMin.y + PlayerRadius) {
			npc.position.y = ArenaMin.y + PlayerRadius;
		}
		if (npc.position.y > ArenaMax.y - PlayerRadius) {
			npc.position.y = ArenaMax.y - PlayerRadius;
		}
	}

}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer

	//coin stuff
	connection.send(coin_countdown);
	connection.send(break_countdown);
	connection.send(coin_position);

	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.dead);
		connection.send(player.collected_coin);
		connection.send(player.revealed_countdown);
		connection.send(player.dash_cooldown);
	};

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) {
		send_player(*connection_player);
	}
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//npc count:
	connection.send(uint8_t(npcs.size()));
	for (auto const &npc : npcs) {
		connection.send(npc.position);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	read(&coin_countdown);
	read(&break_countdown);
	read(&coin_position);

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.dead);
		read(&player.collected_coin);
		read(&player.revealed_countdown);
		read(&player.dash_cooldown);
	}

	npcs.clear();
	uint8_t npc_count;
	read(&npc_count);
	for (uint8_t i = 0; i < npc_count; ++i) {
		npcs.emplace_back();
		NPC &npc = npcs.back();
		read(&npc.position);
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
