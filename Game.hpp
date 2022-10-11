#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, space;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	float dash_countdown = 0.f; // time before dash ends
	glm::vec2 dash_dir = glm::vec2(0.f, 0.f);
	float dash_cooldown_duration = 5.f; // seconds between consecutive dashes
	float dash_cooldown = 0.f; // time before can dash again

	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	bool dead = false;
	bool collected_coin = false;
	float revealed_countdown = 0.f;
};

struct NPC {
	glm::vec2 position = glm::vec2(0.f, 0.f);
	glm::vec2 destination = glm::vec2(0.f, 0.f);
	float rest_countdown = 0.f;
	float direction_change_cooldown = 0.f;
	glm::vec2 direction = glm::vec2(0.f, 0.f);
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	std::mt19937 mt; //used for spawning players

	static constexpr int NUM_NPCS = 50;
	std::vector< NPC > npcs;
	void Game::spawn_npcs();

	Game();

	glm::vec2 random_point_in_arena(float center_weight, float boundary_thickness = PlayerRadius);

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-2.0f, -1.0f);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 2.0f,  1.0f);

	//player constants:
	inline static constexpr float PlayerRadius = 0.06f;
	inline static constexpr float PlayerSpeed = 2.0f;
	inline static constexpr float PlayerAccelHalflife = 0.25f;

	//coin stuff
	inline static constexpr float coin_duration = 15.f;
	inline static constexpr float break_duration = 20.f;
	inline static constexpr float revealed_duration = 2.f;
	float coin_countdown = 0.f;
	float break_countdown = 10.f;
	inline static constexpr float CoinRadius = 0.03f;
	glm::vec2 coin_position = glm::vec2(0.f, 0.f);

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
