#include "game_manager.h"

using namespace libmcc;

bool __fastcall c_game_manager::chud_(uint64_t, float*, float*, float*) {
	return false;
}

bool __fastcall c_game_manager::chud__(uint64_t, void*) {
	return false;
}

uint32_t __fastcall c_game_manager::chud_blend_color(
	e_local_player player, 
	uint32_t rgba
) {
	return rgba;
}

void __fastcall c_game_manager::draw_player_emblem(
	XUID xuid, 
	int, 
	float x0, float x1, 
	float y0, float y1, 
	float
) {

}