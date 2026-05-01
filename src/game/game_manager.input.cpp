#include "game_manager.h"

// todo: use hid and gip instead of xinput

#include <Xinput.h>
#include <atomic>
#include <cstring>

#include "../logging/logging.h"

#include "../input/win32_input.h"
#include "../main/main.h"

using namespace libmcc;

static bool xinput_query(e_local_player player, s_gamepad_state* gp) {
	XINPUT_STATE xstate;
	if (XInputGetState(player, &xstate) != ERROR_SUCCESS) return false;
	gp->buttons       = xstate.Gamepad.wButtons;
	gp->leftTrigger   = xstate.Gamepad.bLeftTrigger;
	gp->rightTrigger  = xstate.Gamepad.bRightTrigger;
	gp->thumbLX       = xstate.Gamepad.sThumbLX;
	gp->thumbLY       = xstate.Gamepad.sThumbLY;
	gp->thumbRX       = xstate.Gamepad.sThumbRX;
	gp->thumbRY       = xstate.Gamepad.sThumbRY;
	return true;
}

// Cheap "did this gamepad poll show user activity?" check. Used to flip
// out of kbm-sticky mode only when there's real pad input — pad-connected
// alone (idle sticks/buttons) shouldn't override mouse/keyboard.
static bool gamepad_has_activity(const s_gamepad_state* gp) {
	if ((int)gp->buttons != 0) return true;
	if (gp->leftTrigger > 30 || gp->rightTrigger > 30) return true;
	const int dz = 8000;  // XInput stick deadzone
	if (gp->thumbLX > dz || gp->thumbLX < -dz) return true;
	if (gp->thumbLY > dz || gp->thumbLY < -dz) return true;
	if (gp->thumbRX > dz || gp->thumbRX < -dz) return true;
	if (gp->thumbRY > dz || gp->thumbRY < -dz) return true;
	return false;
}

bool __fastcall c_game_manager::get_input_state(e_local_player player, s_input_state* state) {
	memset(state, 0, sizeof(*state));

	const bool has_pad = xinput_query(player, &state->gamepad);

	if (player == _local_player_0) {
		win32_input_snapshot(&state->keyboard, &state->mouse);

		// Pause-overlay gate: while the halox imgui overlay is visible, the
		// game must not see any KB/M input. Otherwise mouse motion drags the
		// camera while the user is trying to click "Quit" and key presses
		// (E/F/SPACE) leak into the game world. The OS cursor IS released
		// (apply_cursor_capture(false)) but raw-input deltas keep flowing —
		// gating here is the canonical fix.
		if (g_show_imgui) {
			std::memset(&state->keyboard, 0, sizeof(state->keyboard));
			state->mouse.lX      = 0.0f;
			state->mouse.lY      = 0.0f;
			state->mouse.lZ      = 0.0f;
			state->mouse.buttons = 0;
		}

		// is_km is sticky: once the user touches mouse/keyboard, stay in kbm
		// mode until the gamepad shows real activity (sticks past deadzone or
		// buttons/triggers pressed). This prevents the per-frame flicker that
		// caused jumpy aim — a single still frame would otherwise flip the
		// game back into pad-mode and discard our mouse delta.
		const bool pad_active = has_pad && gamepad_has_activity(&state->gamepad);
		if (pad_active) {
			win32_input_clear_kbm_sticky();
		}
		const bool kbm_sticky = win32_input_kbm_sticky_active();
		state->is_km = (kbm_sticky || !has_pad) ? 1 : 0;

		// Vehicle / zoom camera fix. Halo 2 is a console port — scoped fine-
		// aim and turret/vehicle aim are wired internally to the right-stick
		// regardless of the is_km flag (the kbm "look" path that drives
		// on-foot mouse-look isn't the same path that drives those modes).
		// Without a synthesized right-stick value, the camera locks the
		// instant the player zooms or boards a vehicle. Mirror the mouse
		// delta into thumbRX/RY so the right-stick path sees identical motion.
		//
		// Scale: the engine treats thumbR in [-32768, 32767] as "stick
		// deflection". Mouse delta arrives already scaled by sensitivity *
		// k_mouse_delta_scale (0.10) inside win32_input_snapshot, so a
		// fast flick is a few units. Multiplying by ~2000 maps a fast flick
		// to ~10k–20k thumb units — in the same ballpark as a real stick.
		// Y is inverted because XInput's positive Y is "up" while our mouse
		// dY is positive downward.
		//
		// Done AFTER is_km is computed and AFTER the gamepad_has_activity
		// check so the synthesized stick values can't accidentally flip the
		// kbm-sticky mode off. Only synthesized when not gated by imgui
		// (mouse deltas are already zeroed in that case above).
		if (state->is_km && !g_show_imgui) {
			constexpr float k_mouse_to_stick = 2000.0f;
			float sx = state->mouse.lX *  k_mouse_to_stick;
			float sy = state->mouse.lY * -k_mouse_to_stick;  // invert Y for XInput convention
			if (sx >  32767.0f) sx =  32767.0f;
			if (sx < -32768.0f) sx = -32768.0f;
			if (sy >  32767.0f) sy =  32767.0f;
			if (sy < -32768.0f) sy = -32768.0f;
			state->gamepad.thumbRX = (int16_t)sx;
			state->gamepad.thumbRY = (int16_t)sy;
		}

		// Diagnostic: per-100-call snapshot of the input-mode arbitration so
		// we can verify which path (kbm vs pad) the engine is being told to
		// use. Throttled so it doesn't flood at 60Hz.
		static std::atomic<int> s_call{0};
		int n = s_call.fetch_add(1, std::memory_order_relaxed) + 1;
		if (n <= 4 || (n % 240) == 0) {
			CONSOLE_LOG_INFO("input: get_input_state #%d has_pad=%d pad_active=%d kbm_sticky=%d → is_km=%d "
				"(pad_buttons=0x%X mouse_buttons=0x%X)",
				n, (int)has_pad, (int)pad_active, (int)kbm_sticky, (int)state->is_km,
				(unsigned)state->gamepad.buttons, (unsigned)state->mouse.buttons);
		}
	}

	return true;
}

bool __fastcall c_game_manager::get_input_state_gamepad(e_local_player player, s_input_state* state) {
	memset(state, 0, sizeof(*state));
	xinput_query(player, &state->gamepad);
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
