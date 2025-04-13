#pragma once

#include "../common.h"

namespace libmcc {

class c_game_event_manager : public i_game_event_manager {
public:
	c_game_event_manager();

	bool AchievementEarned() override;

};
}

inline static libmcc::c_game_event_manager g_game_event_manager;
inline libmcc::i_game_event_manager* game_event_manager() {
	return &g_game_event_manager;
}
