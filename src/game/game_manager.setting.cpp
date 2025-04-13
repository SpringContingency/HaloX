#include "game_manager.h"

using namespace libmcc;

bool __fastcall c_game_manager::get_game_setting(
	e_game_setting setting, 
	bool* v1, 
	uint64_t* v2
) {
	switch (setting) {
	case _game_setting_enable_subtitle:
		if (v1) {
			*v1 = true;
		}
		return true;
	case libmcc::_game_setting_new_font_package:
		if (v1) {
			*v1 = true;
		}
		return true;
	default:
		break;
	}

	return false;
}

static s_game_video_settings video_settings;

bool __fastcall libmcc::c_game_manager::get_video_setting(s_game_video_settings* settings) {
	if (settings) {
		memcpy(settings, &video_settings, sizeof(video_settings));
	}

	return true;
}

static s_game_audio_settings audio_settings;

uintptr_t __fastcall libmcc::c_game_manager::get_audio_setting(s_game_audio_settings* settings) {
	if (settings)
		memcpy(settings, &audio_settings, sizeof(audio_settings));

	return true;
}
