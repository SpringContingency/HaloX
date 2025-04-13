#include "game_manager.h"

// todo: use hid and gip instead of xinput

#include <Xinput.h>

using namespace libmcc;

bool __fastcall c_game_manager::get_input_state(e_local_player player, s_input_state* state) {
	XINPUT_STATE xstate;

	memset(state, 0, sizeof(*state));

	if (XInputGetState(player, &xstate) == ERROR_SUCCESS) {
		state->gamepad.buttons = xstate.Gamepad.wButtons;
		state->gamepad.leftTrigger = xstate.Gamepad.bLeftTrigger;
		state->gamepad.rightTrigger = xstate.Gamepad.bRightTrigger;
		state->gamepad.thumbLX = xstate.Gamepad.sThumbLX;
		state->gamepad.thumbLY = xstate.Gamepad.sThumbLY;
		state->gamepad.thumbRX = xstate.Gamepad.sThumbRX;
		state->gamepad.thumbRY = xstate.Gamepad.sThumbRY;
	}

	return true;
}

bool __fastcall c_game_manager::get_input_state_gamepad(e_local_player player, s_input_state* state) {
	XINPUT_STATE xstate;

	memset(state, 0, sizeof(*state));

	if (XInputGetState(player, &xstate) == ERROR_SUCCESS) {
		state->gamepad.buttons = xstate.Gamepad.wButtons;
		state->gamepad.leftTrigger = xstate.Gamepad.bLeftTrigger;
		state->gamepad.rightTrigger = xstate.Gamepad.bRightTrigger;
		state->gamepad.thumbLX = xstate.Gamepad.sThumbLX;
		state->gamepad.thumbLY = xstate.Gamepad.sThumbLY;
		state->gamepad.thumbRX = xstate.Gamepad.sThumbRX;
		state->gamepad.thumbRY = xstate.Gamepad.sThumbRY;
	}

	return true;
}

float __fastcall c_game_manager::update_input_time(e_local_player player) {
	return 0.0f;
}

void __fastcall c_game_manager::set_input_state(e_local_player player, s_rumble_state* state) {
	XINPUT_VIBRATION vibration;

	vibration.wLeftMotorSpeed = state->left_motor_speed;
	vibration.wRightMotorSpeed = state->right_motor_speed;

	XInputSetState(player, &vibration);
}
