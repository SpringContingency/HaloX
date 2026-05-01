#include "mcc_user_settings.h"

#include "../logging/logging.h"
#include "../input/win32_input.h"
#include "../player/player_manager.h"
#include "halox_audio.h"

#include <Windows.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <cstring>
#include <fstream>
#include <string>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

static s_mcc_user_settings g_settings;

const s_mcc_user_settings* mcc_user_settings() { return &g_settings; }
s_mcc_user_settings*       mcc_user_settings_mut() { return &g_settings; }

static std::wstring resolve_settings_path() {
	PWSTR path = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, nullptr, &path)) || !path) {
		if (path) CoTaskMemFree(path);
		return L"";
	}
	std::wstring full = path;
	CoTaskMemFree(path);
	full += L"\\MCC\\Saved\\Config\\WindowsNoEditor\\GameUserSettings.ini";
	return full;
}

static bool starts_with(const std::string& s, const char* prefix) {
	auto n = std::strlen(prefix);
	return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

// Parse "Name[idx]=value" -> writes to out_idx and out_value when matched.
static bool parse_indexed(const std::string& line, const char* name,
                          int* out_idx, std::string* out_value) {
	auto n = std::strlen(name);
	if (line.size() < n + 4) return false;                  // need [N]=v
	if (std::memcmp(line.data(), name, n) != 0) return false;
	if (line[n] != '[') return false;
	size_t p = n + 1;
	int idx = 0;
	bool any = false;
	while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
		idx = idx * 10 + (line[p] - '0');
		++p;
		any = true;
	}
	if (!any) return false;
	if (p >= line.size() || line[p] != ']') return false;
	++p;
	if (p >= line.size() || line[p] != '=') return false;
	++p;
	*out_idx = idx;
	*out_value = line.substr(p);
	return true;
}

static void store_float(float* arr, int idx, const std::string& v) {
	if (idx < 0 || idx >= libmcc::k_game_count) return;
	arr[idx] = (float)std::atof(v.c_str());
}
static void store_int(int* arr, int idx, const std::string& v) {
	if (idx < 0 || idx >= libmcc::k_game_count) return;
	arr[idx] = std::atoi(v.c_str());
}
static void store_bool(bool* arr, int idx, const std::string& v) {
	if (idx < 0 || idx >= libmcc::k_game_count) return;
	// MCC writes 0/1 (sometimes "True"/"False", or floats like "0.000000" for
	// keys that are actually bool on the profile — handle each).
	if (v.empty()) { arr[idx] = false; return; }
	if (v[0] == 'T' || v[0] == 't') { arr[idx] = true;  return; }
	if (v[0] == 'F' || v[0] == 'f') { arr[idx] = false; return; }
	arr[idx] = (std::atof(v.c_str()) != 0.0);
}

// Generic helper: in `body` (assumed to be the inside of a `(...)` block),
// find the value for `key` at the top level. Returns the substring after `=`,
// stopping at a sibling-level comma (skipping over nested parens). Empty
// string if not found.
static std::string find_top_level(const std::string& body, const char* key) {
	size_t klen = std::strlen(key);
	size_t i = 0;
	while (i + klen < body.size()) {
		// Match key at a sibling boundary (start of body OR right after a
		// top-level comma) so "ObjectFadeModifer" doesn't accidentally match
		// inside a nested paren that contains the substring.
		bool boundary = (i == 0) || (body[i-1] == ',' || body[i-1] == '(');
		if (boundary && std::memcmp(body.data() + i, key, klen) == 0 &&
		    i + klen < body.size() && body[i + klen] == '=') {
			size_t v = i + klen + 1;
			int depth = 0;
			size_t end = v;
			while (end < body.size()) {
				char c = body[end];
				if (c == '(') depth++;
				else if (c == ')') { if (depth == 0) break; depth--; }
				else if (c == ',' && depth == 0) break;
				end++;
			}
			return body.substr(v, end - v);
		}
		i++;
	}
	return std::string();
}

// MCC's (ScaleVal=X,OffsetVal=Y) shape — both fields are floats.
static void parse_scale_offset(const std::string& v, float* out_scale, float* out_offset) {
	if (v.size() < 2 || v.front() != '(' || v.back() != ')') return;
	std::string inner = v.substr(1, v.size() - 2);
	auto s = find_top_level(inner, "ScaleVal");
	auto o = find_top_level(inner, "OffsetVal");
	if (!s.empty()) *out_scale  = (float)std::atof(s.c_str());
	if (!o.empty()) *out_offset = (float)std::atof(o.c_str());
}
static void parse_scale_offset_i(const std::string& v, float* out_scale, int* out_offset) {
	if (v.size() < 2 || v.front() != '(' || v.back() != ')') return;
	std::string inner = v.substr(1, v.size() - 2);
	auto s = find_top_level(inner, "ScaleVal");
	auto o = find_top_level(inner, "OffsetVal");
	if (!s.empty()) *out_scale  = (float)std::atof(s.c_str());
	if (!o.empty()) *out_offset = std::atoi(o.c_str());
}

static bool parse_inline_bool(const std::string& v) {
	if (v.empty()) return false;
	if (v[0] == 'T' || v[0] == 't' || v == "1") return true;
	return false;
}

// Parse a `(WaterLod=(...),ObjectFadeModifer=(...),...,TextureResolution=2,...)`
// blob into one s_mcc_quality_throttles. Tolerant of missing keys: the struct's
// in-class defaults stand for anything not present.
static void parse_quality_throttles(const std::string& value, s_mcc_quality_throttles* out) {
	if (value.size() < 2 || value.front() != '(' || value.back() != ')') return;
	std::string body = value.substr(1, value.size() - 2);
	auto s = [&](const char* k) { return find_top_level(body, k); };

	// Scale/offset pairs
	parse_scale_offset  (s("WaterLod"),                   &out->water_lod_scale,            &out->water_lod_offset);
	parse_scale_offset  (s("ObjectFadeModifer"),          &out->object_fade_scale,          &out->object_fade_offset);
	parse_scale_offset  (s("ObjectDetailModifer"),        &out->object_detail_scale,        &out->object_detail_offset);
	parse_scale_offset  (s("ObjectImposterCutoffModifer"),&out->object_imposter_cutoff_scale,&out->object_imposter_cutoff_offset);
	parse_scale_offset_i(s("ScreenspaceDynamicLightMaxCount"), &out->ss_dlight_max_scale,    &out->ss_dlight_max_offset);
	parse_scale_offset  (s("ScreenspaceDynamicLightScale"),    &out->ss_dlight_scale_scale,  &out->ss_dlight_scale_offset);
	parse_scale_offset_i(s("CPUDynamicLightMaxCount"),    &out->cpu_dlight_max_scale,       &out->cpu_dlight_max_offset);
	parse_scale_offset  (s("CPUDynamicLightScale"),       &out->cpu_dlight_scale_scale,     &out->cpu_dlight_scale_offset);
	parse_scale_offset_i(s("GPUDynamicLightMaxCount"),    &out->gpu_dlight_max_scale,       &out->gpu_dlight_max_offset);
	parse_scale_offset  (s("GPUDynamicLightScale"),       &out->gpu_dlight_scale_scale,     &out->gpu_dlight_scale_offset);
	parse_scale_offset_i(s("ShadowGenerateCount"),        &out->shadow_generate_scale,      &out->shadow_generate_offset);
	parse_scale_offset  (s("ShadowQualityLOD"),           &out->shadow_quality_lod_scale,   &out->shadow_quality_lod_offset);
	parse_scale_offset  (s("EffectsLODDistanceScale"),    &out->effects_lod_distance_scale_scale,    &out->effects_lod_distance_scale_offset);
	parse_scale_offset  (s("DecalFadeDistanceScale"),     &out->decal_fade_distance_scale_scale,     &out->decal_fade_distance_scale_offset);
	parse_scale_offset  (s("DecoratorFadeDistanceScale"), &out->decorator_fade_distance_scale_scale, &out->decorator_fade_distance_scale_offset);
	parse_scale_offset  (s("StructureInstanceLODModifer"),&out->structure_instance_lod_scale,        &out->structure_instance_lod_offset);
	parse_scale_offset  (s("InstanceFadeModifier"),       &out->instance_fade_scale,                 &out->instance_fade_offset);

	// Bools
	{ auto v = s("DisableDynamicLightingShadows"); if (!v.empty()) out->disable_dynamic_lighting_shadows = parse_inline_bool(v); }
	{ auto v = s("DisableFirstPersonShadow");      if (!v.empty()) out->disable_first_person_shadow      = parse_inline_bool(v); }
	{ auto v = s("DisableCheapParticles");         if (!v.empty()) out->disable_cheap_particles          = parse_inline_bool(v); }
	{ auto v = s("DisablePatchyFog");              if (!v.empty()) out->disable_patchy_fog               = parse_inline_bool(v); }
	{ auto v = s("AntiAliasing");                  if (!v.empty()) out->anti_aliasing                    = parse_inline_bool(v); }
	{ auto v = s("MotionBlur");                    if (!v.empty()) out->motion_blur                      = parse_inline_bool(v); }
	{ auto v = s("Blood");                         if (!v.empty()) out->blood                            = parse_inline_bool(v); }

	// Quality enums
	{ auto v = s("TextureResolution");             if (!v.empty()) out->texture_resolution        = std::atoi(v.c_str()); }
	{ auto v = s("TextureFilteringQuality");       if (!v.empty()) out->texture_filtering_quality = std::atoi(v.c_str()); }
	{ auto v = s("LightingQuality");               if (!v.empty()) out->lighting_quality          = std::atoi(v.c_str()); }
	{ auto v = s("EffectsQuality");                if (!v.empty()) out->effects_quality           = std::atoi(v.c_str()); }
	{ auto v = s("ShadowQuality");                 if (!v.empty()) out->shadow_quality            = std::atoi(v.c_str()); }
	{ auto v = s("DetailsQuality");                if (!v.empty()) out->details_quality           = std::atoi(v.c_str()); }
	{ auto v = s("PostProcessingQuality");         if (!v.empty()) out->post_processing_quality   = std::atoi(v.c_str()); }
	{ auto v = s("WaterQuality");                  if (!v.empty()) out->water_quality             = std::atoi(v.c_str()); }

	out->loaded = true;
}

// Parse a single CustomKeyboardMouseMappingV2[N]=(...) line. The inner shape is
// an unreal-style nested struct; we find each "GameKeyboardMouseMappings[i]=(...)"
// chunk, then pull AbstractButton + VirtualKeyCodes[0..4] out of it.
static void parse_kbm_mapping(const std::string& body,
                              s_mcc_user_settings::s_kbm_entry out[66]) {
	// Reset to "unset" so unparsed slots fall back to built-in defaults.
	for (int i = 0; i < 66; ++i) out[i] = s_mcc_user_settings::s_kbm_entry{};

	const char* tag = "GameKeyboardMouseMappings[";
	size_t pos = 0;
	while (true) {
		size_t hit = body.find(tag, pos);
		if (hit == std::string::npos) break;
		size_t i_begin = hit + std::strlen(tag);
		size_t i_end = body.find(']', i_begin);
		if (i_end == std::string::npos) break;

		int slot = std::atoi(body.c_str() + i_begin);

		// Walk to the matching '(' that opens this entry's body and find the
		// closing ')' (single-level — the entry itself has no nested parens).
		size_t open = body.find('(', i_end);
		if (open == std::string::npos) break;
		size_t close = body.find(')', open + 1);
		if (close == std::string::npos) break;

		std::string entry = body.substr(open + 1, close - open - 1);

		s_mcc_user_settings::s_kbm_entry e{};
		auto find_int = [&](const char* key, int* out_val) -> bool {
			size_t p = entry.find(key);
			if (p == std::string::npos) return false;
			p += std::strlen(key);
			if (p >= entry.size() || entry[p] != '=') return false;
			++p;
			*out_val = std::atoi(entry.c_str() + p);
			return true;
		};

		find_int("AbstractButton", &e.abstract_button);
		for (int k = 0; k < 5; ++k) {
			char key[32];
			std::snprintf(key, sizeof(key), "VirtualKeyCodes[%d]", k);
			find_int(key, &e.virtual_key_codes[k]);
		}

		if (slot >= 0 && slot < 66) out[slot] = e;
		pos = close + 1;
	}
}

bool load_mcc_user_settings(s_mcc_user_settings* out) {
	*out = s_mcc_user_settings{};

	auto path = resolve_settings_path();
	if (path.empty()) {
		CONSOLE_LOG_WARN("MCC GameUserSettings.ini: failed to resolve LocalAppDataLow");
		return false;
	}

	std::ifstream in(path);
	if (!in) {
		CONSOLE_LOG_WARN("MCC GameUserSettings.ini: not found at %ls", path.c_str());
		return false;
	}

	// Helpers for the new graphics-section parsing.
	auto starts_with_eq = [](const std::string& s, const char* key, std::string* out_val) -> bool {
		size_t klen = strlen(key);
		if (s.size() <= klen || memcmp(s.data(), key, klen) != 0 || s[klen] != '=') return false;
		*out_val = s.substr(klen + 1);
		return true;
	};
	auto parse_bool = [](const std::string& v) -> bool {
		// MCC writes "True"/"False"; tolerate "1"/"0" too.
		if (v == "True" || v == "true" || v == "1") return true;
		return false;
	};

	enum e_section { sec_none, sec_mcc, sec_scalability };

	std::string line;
	e_section sec = sec_none;
	while (std::getline(in, line)) {
		// Strip CR + leading/trailing whitespace
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();
		size_t lead = 0;
		while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
		if (lead) line.erase(0, lead);
		if (line.empty() || line[0] == ';') continue;

		if (line[0] == '[') {
			if      (line == "[/Script/MCC.MCCGameUserSettings]") sec = sec_mcc;
			else if (line == "[ScalabilityGroups]")               sec = sec_scalability;
			else                                                  sec = sec_none;
			continue;
		}
		if (sec == sec_scalability) {
			std::string v;
			if      (starts_with_eq(line, "sg.ResolutionQuality",    &v)) out->graphics.sg_resolution_quality = (float)atof(v.c_str());
			else if (starts_with_eq(line, "sg.ViewDistanceQuality",  &v)) out->graphics.sg_view_distance      = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.AntiAliasingQuality",  &v)) out->graphics.sg_anti_aliasing      = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.ShadowQuality",        &v)) out->graphics.sg_shadow             = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.PostProcessQuality",   &v)) out->graphics.sg_post_process       = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.TextureQuality",       &v)) out->graphics.sg_texture            = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.EffectsQuality",       &v)) out->graphics.sg_effects            = atoi(v.c_str());
			else if (starts_with_eq(line, "sg.FoliageQuality",       &v)) out->graphics.sg_foliage            = atoi(v.c_str());
			continue;
		}
		if (sec != sec_mcc) continue;

		// Display / rendering keys live in the MCC section but are NOT indexed
		// (one set, not per-game). Try those first; fall through to per-game.
		{
			std::string v;
			if      (starts_with_eq(line, "bUseVSync",                &v)) { out->graphics.use_vsync         = parse_bool(v);    continue; }
			else if (starts_with_eq(line, "bUseDynamicResolution",    &v)) { out->graphics.use_dynamic_res   = parse_bool(v);    continue; }
			else if (starts_with_eq(line, "ResolutionSizeX",          &v)) { out->graphics.resolution_x      = atoi(v.c_str()); continue; }
			else if (starts_with_eq(line, "ResolutionSizeY",          &v)) { out->graphics.resolution_y      = atoi(v.c_str()); continue; }
			else if (starts_with_eq(line, "FullscreenMode",           &v)) { out->graphics.fullscreen_mode   = atoi(v.c_str()); continue; }
			else if (starts_with_eq(line, "FrameRateLimit",           &v)) { out->graphics.frame_rate_limit  = (float)atof(v.c_str()); continue; }
			else if (starts_with_eq(line, "bUseHDRDisplayOutput",     &v)) { out->graphics.use_hdr           = parse_bool(v);    continue; }
			else if (starts_with_eq(line, "HDRDisplayOutputNits",     &v)) { out->graphics.hdr_nits          = atoi(v.c_str()); continue; }
			else if (starts_with_eq(line, "HDRBrightness",            &v)) { out->graphics.hdr_brightness    = (float)atof(v.c_str()); continue; }
			else if (starts_with_eq(line, "HDRContrast",              &v)) { out->graphics.hdr_contrast      = (float)atof(v.c_str()); continue; }
			else if (starts_with_eq(line, "GameBrightness",           &v)) { out->graphics.game_brightness   = (float)atof(v.c_str()); continue; }
			else if (starts_with_eq(line, "GraphicsQualityThrottles", &v)) {
				parse_quality_throttles(v, &out->graphics.quality_default);
				continue;
			}
		}

		int idx = 0;
		std::string val;
		if      (parse_indexed(line, "MouseSensitivity",            &idx, &val)) store_float(out->mouse_sensitivity,     idx, val);
		else if (parse_indexed(line, "MouseLookControlsInverted",   &idx, &val)) store_bool (out->mouse_look_inverted,   idx, val);
		else if (parse_indexed(line, "MouseSmoothing",              &idx, &val)) store_bool (out->mouse_smoothing,       idx, val);
		else if (parse_indexed(line, "MouseAcceleration",           &idx, &val)) store_bool (out->mouse_acceleration,    idx, val);
		else if (parse_indexed(line, "MouseAccelerationMinRate",    &idx, &val)) store_float(out->mouse_accel_min_rate,  idx, val);
		else if (parse_indexed(line, "MouseAccelerationMaxAccel",   &idx, &val)) store_float(out->mouse_accel_max_accel, idx, val);
		else if (parse_indexed(line, "MouseAccelerationScale",      &idx, &val)) store_float(out->mouse_accel_scale,     idx, val);
		else if (parse_indexed(line, "MouseAccelerationExp",        &idx, &val)) store_float(out->mouse_accel_exp,       idx, val);
		else if (parse_indexed(line, "FOVSetting",                  &idx, &val)) store_int  (out->fov,                  idx, val);
		else if (parse_indexed(line, "VehicleFOVSetting",           &idx, &val)) store_int  (out->vehicle_fov,          idx, val);
		else if (parse_indexed(line, "MouseAircraftControlsInverted",   &idx, &val)) store_bool(out->mouse_aircraft_inverted,    idx, val);
		else if (parse_indexed(line, "LookControlsInverted",        &idx, &val)) store_bool (out->gamepad_look_inverted,        idx, val);
		else if (parse_indexed(line, "GamepadAircraftControlsInverted", &idx, &val)) store_bool(out->gamepad_aircraft_inverted,  idx, val);
		else if (parse_indexed(line, "VibrationDisabled",           &idx, &val)) store_bool (out->vibration_disabled,           idx, val);
		else if (parse_indexed(line, "ImpulseTriggersDisabled",     &idx, &val)) store_bool (out->impulse_triggers_disabled,    idx, val);
		else if (parse_indexed(line, "AutoCenterEnabled",           &idx, &val)) store_bool (out->auto_center_enabled,          idx, val);
		else if (parse_indexed(line, "CrouchLockEnabled",           &idx, &val)) store_bool (out->crouch_lock_enabled,          idx, val);
		else if (parse_indexed(line, "MKCrouchLockEnabled",         &idx, &val)) store_bool (out->mk_crouch_lock_enabled,       idx, val);
		else if (parse_indexed(line, "ClenchProtectionEnabled",     &idx, &val)) store_bool (out->clench_protection_enabled,    idx, val);
		else if (parse_indexed(line, "UseFemaleVoice",              &idx, &val)) store_bool (out->use_female_voice,             idx, val);
		else if (parse_indexed(line, "HoldToZoom",                  &idx, &val)) store_bool (out->hold_to_zoom,                 idx, val);
		else if (parse_indexed(line, "UseEliteModel",               &idx, &val)) store_bool (out->use_elite_model,              idx, val);
		else if (parse_indexed(line, "UseModernAimControl",         &idx, &val)) store_bool (out->use_modern_aim_control,       idx, val);
		else if (parse_indexed(line, "KeyboardMouseButtonPreset",   &idx, &val)) store_int  (out->keyboard_mouse_button_preset, idx, val);
		else if (parse_indexed(line, "ButtonPreset",                &idx, &val)) store_int  (out->button_preset,                idx, val);
		else if (parse_indexed(line, "StickPreset",                 &idx, &val)) store_int  (out->stick_preset,                 idx, val);
		else if (parse_indexed(line, "LeftyToggle",                 &idx, &val)) store_bool (out->lefty_toggle,                 idx, val);
		else if (parse_indexed(line, "SwapTriggersAndBumpers",      &idx, &val)) store_bool (out->swap_triggers_and_bumpers,    idx, val);
		else if (parse_indexed(line, "VerticalLookSensitivity",     &idx, &val)) store_int  (out->vertical_look_sensitivity,    idx, val);
		else if (parse_indexed(line, "HorizontalLookSensitivity",   &idx, &val)) store_int  (out->horizontal_look_sensitivity,  idx, val);
		else if (parse_indexed(line, "LookAcceleration",            &idx, &val)) store_bool (out->look_acceleration,             idx, val);
		else if (parse_indexed(line, "LookAxialDeadZone",           &idx, &val)) store_float(out->look_axial_dead_zone,          idx, val);
		else if (parse_indexed(line, "LookRadialDeadZone",          &idx, &val)) store_float(out->look_radial_dead_zone,         idx, val);
		else if (parse_indexed(line, "ZoomLookSensitivityMultiplier",    &idx, &val)) store_float(out->zoom_look_sens_mult,    idx, val);
		else if (parse_indexed(line, "VehicleLookSensitivityMultiplier", &idx, &val)) store_float(out->vehicle_look_sens_mult, idx, val);
		else if (parse_indexed(line, "UseDoublePressJumpToJetpack",      &idx, &val)) store_bool(out->use_double_press_jump_to_jetpack, idx, val);
		else if (parse_indexed(line, "DualWieldInverted",                &idx, &val)) store_bool(out->dual_wield_inverted,                idx, val);
		else if (parse_indexed(line, "ControllerDualWieldInverted",      &idx, &val)) store_bool(out->controller_dual_wield_inverted,    idx, val);
		else if (parse_indexed(line, "ControllerHornetControlJoystick",  &idx, &val)) store_bool(out->controller_hornet_control_joystick, idx, val);
		else if (parse_indexed(line, "ControllerBansheeTrickButtonsSwapped", &idx, &val)) store_bool(out->controller_banshee_trick_buttons_swapped, idx, val);
		else if (parse_indexed(line, "FlyingCameraTurnSensitivity", &idx, &val)) store_int(out->flying_camera_turn_sensitivity, idx, val);
		else if (parse_indexed(line, "FlyingCameraPanning",         &idx, &val)) store_int(out->flying_camera_panning,         idx, val);
		else if (parse_indexed(line, "FlyingCameraSpeed",           &idx, &val)) store_int(out->flying_camera_speed,           idx, val);
		else if (parse_indexed(line, "FlyingCameraThrust",          &idx, &val)) store_int(out->flying_camera_thrust,          idx, val);
		else if (parse_indexed(line, "TheaterTurnSensitivity",      &idx, &val)) store_int(out->theater_turn_sensitivity,      idx, val);
		else if (parse_indexed(line, "TheaterPanning",              &idx, &val)) store_int(out->theater_panning,               idx, val);
		else if (parse_indexed(line, "TheaterSpeed",                &idx, &val)) store_int(out->theater_speed,                 idx, val);
		else if (parse_indexed(line, "TheaterThrust",               &idx, &val)) store_int(out->theater_thrust,                idx, val);
		else if (parse_indexed(line, "MKTheaterTurnSensitivity",    &idx, &val)) store_int(out->mk_theater_turn_sensitivity,   idx, val);
		else if (parse_indexed(line, "MKTheaterPanning",            &idx, &val)) store_int(out->mk_theater_panning,            idx, val);
		else if (parse_indexed(line, "MKTheaterSpeed",              &idx, &val)) store_int(out->mk_theater_speed,              idx, val);
		else if (parse_indexed(line, "MKTheaterThrust",             &idx, &val)) store_int(out->mk_theater_thrust,             idx, val);
		else if (parse_indexed(line, "CustomKeyboardMouseMappingV2", &idx, &val)) {
			if (idx >= 0 && idx < libmcc::k_game_count) parse_kbm_mapping(val, out->custom_kbm[idx]);
		}
		else if (parse_indexed(line, "GraphicsQualityThrottlesPerGame", &idx, &val)) {
			if (idx >= 0 && idx < libmcc::k_game_count) parse_quality_throttles(val, &out->graphics.quality_per_game[idx]);
		}
	}

	out->loaded = true;
	CONSOLE_LOG_INFO(
		"MCC GameUserSettings.ini: loaded %ls",
		path.c_str());
	{
		const auto& g = out->graphics;
		CONSOLE_LOG_INFO(
			"  graphics: %dx%d mode=%d vsync=%d framecap=%.0f hdr=%d dynRes=%d "
			"sg(res=%.0f%% view=%d aa=%d shadow=%d pp=%d tex=%d fx=%d foliage=%d) "
			"hdrBright=%.0f hdrCon=%.1f gameBright=%.2f",
			g.resolution_x, g.resolution_y, g.fullscreen_mode,
			(int)g.use_vsync, g.frame_rate_limit, (int)g.use_hdr, (int)g.use_dynamic_res,
			g.sg_resolution_quality, g.sg_view_distance, g.sg_anti_aliasing,
			g.sg_shadow, g.sg_post_process, g.sg_texture, g.sg_effects, g.sg_foliage,
			g.hdr_brightness, g.hdr_contrast, g.game_brightness);
		static const char* names[libmcc::k_game_count] = {
			"halo1", "halo2", "halo3", "halo4", "groundhog", "halo3odst", "haloreach"
		};
		for (int i = 0; i < libmcc::k_game_count; ++i) {
			const auto& q = g.quality_per_game[i].loaded ? g.quality_per_game[i] : g.quality_default;
			CONSOLE_LOG_INFO(
				"  quality[%-9s]: tex=%d texFilt=%d light=%d fx=%d shadow=%d details=%d pp=%d water=%d "
				"AA=%d MB=%d blood=%d noDynShadow=%d no1pShadow=%d noCheapFx=%d noPatchyFog=%d",
				names[i],
				q.texture_resolution, q.texture_filtering_quality, q.lighting_quality,
				q.effects_quality, q.shadow_quality, q.details_quality,
				q.post_processing_quality, q.water_quality,
				(int)q.anti_aliasing, (int)q.motion_blur, (int)q.blood,
				(int)q.disable_dynamic_lighting_shadows, (int)q.disable_first_person_shadow,
				(int)q.disable_cheap_particles, (int)q.disable_patchy_fog);
		}
	}
	for (int i = 0; i < libmcc::k_game_count; ++i) {
		static const char* names[libmcc::k_game_count] = {
			"halo1", "halo2", "halo3", "halo4", "groundhog", "halo3odst", "haloreach"
		};
		CONSOLE_LOG_INFO(
			"  %-9s sens=%.3f invertY=%d FOV=%d vehicleFOV=%d",
			names[i], out->mouse_sensitivity[i],
			(int)out->mouse_look_inverted[i],
			out->fov[i], out->vehicle_fov[i]);
	}
	return true;
}

// ---------------------------------------------------------------------------
// halox-owned settings file (TOML at %APPDATA%\HaloX\halox_settings.toml)
//
// Initialize order at boot:
//   1. Try TOML — if present, that's our state.
//   2. Otherwise, run the MCC ini importer for first-run bootstrap.
//   3. Save TOML so subsequent launches skip the MCC importer entirely
//      (i.e., the user's halox config no longer drifts back to MCC's on edit).
//
// On any in-UI edit that should survive relaunch, the caller invokes
// mcc_user_settings_save() — which rewrites the TOML in place atomically.
// ---------------------------------------------------------------------------

static std::wstring resolve_halox_settings_path() {
	PWSTR path = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)) || !path) {
		if (path) CoTaskMemFree(path);
		return L"";
	}
	std::wstring full = path;
	CoTaskMemFree(path);
	full += L"\\HaloX";
	CreateDirectoryW(full.c_str(), nullptr);  // ignore "already exists"
	full += L"\\halox_settings.toml";
	return full;
}

static const char* k_module_names[libmcc::k_game_count] = {
	"halo1", "halo2", "halo3", "halo4", "groundhog", "halo3odst", "haloreach"
};

namespace {
struct toml_writer {
	std::string out;
	void section(const char* name) { out += '['; out += name; out += "]\n"; }
	void kv_int   (const char* k, int v)         { out += k; out += " = "; out += std::to_string(v); out += '\n'; }
	void kv_bool  (const char* k, bool v)        { out += k; out += " = "; out += (v ? "true" : "false"); out += '\n'; }
	void kv_float (const char* k, float v) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.6g", v);
		out += k; out += " = "; out += buf; out += '\n';
	}
	void kv_string(const char* k, const char* v) {
		out += k; out += " = \""; out += v; out += "\"\n";
	}
	void blank() { out += '\n'; }
};

// Trim ASCII whitespace + quotes from both ends.
std::string trim_value(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
	while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
	if (b - a >= 2 && s[a] == '"' && s[b-1] == '"') { ++a; --b; }
	return s.substr(a, b - a);
}
}  // namespace

bool save_halox_user_settings_toml(const s_mcc_user_settings& s) {
	std::wstring path = resolve_halox_settings_path();
	if (path.empty()) return false;

	toml_writer w;
	w.out += "# halox_settings.toml — halox-owned settings (audio, FOV, bindings, ...).\n";
	w.out += "# Generated by HaloX. Edit by hand or via the in-engine settings UI.\n";
	w.blank();

	// [display]
	w.section("display");
	w.kv_bool ("vsync",            s.graphics.use_vsync);
	w.kv_int  ("resolution_x",     s.graphics.resolution_x);
	w.kv_int  ("resolution_y",     s.graphics.resolution_y);
	w.kv_int  ("fullscreen_mode",  s.graphics.fullscreen_mode);
	w.kv_float("frame_rate_limit", s.graphics.frame_rate_limit);
	w.kv_bool ("use_hdr",          s.graphics.use_hdr);
	w.kv_int  ("hdr_nits",         s.graphics.hdr_nits);
	w.kv_float("hdr_brightness",   s.graphics.hdr_brightness);
	w.kv_float("hdr_contrast",     s.graphics.hdr_contrast);
	w.kv_float("game_brightness",  s.graphics.game_brightness);
	w.blank();

	// [audio.<game>]  +  [input.<game>]  +  [profile.<game>]
	for (int i = 0; i < libmcc::k_game_count; ++i) {
		char hdr[64];
		std::snprintf(hdr, sizeof(hdr), "audio.%s", k_module_names[i]);
		w.section(hdr);
		w.kv_int ("master_volume", s.audio[i].master_volume);
		w.kv_int ("sfx_volume",    s.audio[i].sfx_volume);
		w.kv_int ("music_volume",  s.audio[i].music_volume);
		w.kv_int ("voice_volume",  s.audio[i].voice_volume);
		w.kv_bool("muted",         s.audio[i].muted);
		w.blank();

		std::snprintf(hdr, sizeof(hdr), "input.%s", k_module_names[i]);
		w.section(hdr);
		w.kv_float("mouse_sensitivity",     s.mouse_sensitivity[i]);
		w.kv_bool ("mouse_look_inverted",   s.mouse_look_inverted[i]);
		w.kv_bool ("mouse_smoothing",       s.mouse_smoothing[i]);
		w.kv_bool ("mouse_acceleration",    s.mouse_acceleration[i]);
		w.kv_bool ("mouse_aircraft_inverted",   s.mouse_aircraft_inverted[i]);
		w.kv_bool ("gamepad_look_inverted",     s.gamepad_look_inverted[i]);
		w.kv_bool ("gamepad_aircraft_inverted", s.gamepad_aircraft_inverted[i]);
		w.blank();

		std::snprintf(hdr, sizeof(hdr), "profile.%s", k_module_names[i]);
		w.section(hdr);
		w.kv_int ("fov",                 s.fov[i]);
		w.kv_int ("vehicle_fov",         s.vehicle_fov[i]);
		w.kv_bool("vibration_disabled",  s.vibration_disabled[i]);
		w.kv_bool("auto_center_enabled", s.auto_center_enabled[i]);
		w.kv_bool("hold_to_zoom",        s.hold_to_zoom[i]);
		w.kv_bool("use_elite_model",     s.use_elite_model[i]);
		w.kv_bool("use_modern_aim_control", s.use_modern_aim_control[i]);
		w.kv_int ("keyboard_mouse_button_preset", s.keyboard_mouse_button_preset[i]);
		w.kv_int ("button_preset",       s.button_preset[i]);
		w.kv_int ("stick_preset",        s.stick_preset[i]);
		w.blank();

		// [bindings.<game>] — custom KB/M mapping table (66 entries × up to 5
		// virtual-key codes each). Stored as one row per slot:
		//   slot_<idx> = "<abstract_button>:<vk0>,<vk1>,<vk2>,<vk3>,<vk4>"
		// Slots whose abstract_button is -1 (the in-memory "unset" sentinel)
		// are omitted entirely so the TOML stays small AND the loader can
		// distinguish "user-configured but cleared" from "engine default".
		std::snprintf(hdr, sizeof(hdr), "bindings.%s", k_module_names[i]);
		w.section(hdr);
		bool any_binding = false;
		for (int j = 0; j < 66; ++j) {
			const auto& e = s.custom_kbm[i][j];
			if (e.abstract_button < 0) continue;
			char key[16]; std::snprintf(key, sizeof(key), "slot_%d", j);
			char val[64];
			std::snprintf(val, sizeof(val), "%d:%d,%d,%d,%d,%d",
				e.abstract_button,
				e.virtual_key_codes[0], e.virtual_key_codes[1],
				e.virtual_key_codes[2], e.virtual_key_codes[3],
				e.virtual_key_codes[4]);
			w.kv_string(key, val);
			any_binding = true;
		}
		if (!any_binding) {
			w.out += "# (no custom bindings — engine defaults in effect)\n";
		}
		w.blank();
	}

	// Atomic write: temp + rename. A crash mid-write must not corrupt the
	// existing TOML — the rename is the commit point.
	std::wstring tmp = path + L".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(w.out.data(), (std::streamsize)w.out.size());
		if (!f.good()) return false;
	}
	// MoveFileExW with REPLACE_EXISTING is the closest thing Windows has to
	// atomic rename when the destination already exists.
	if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		return false;
	}
	return true;
}

bool load_halox_user_settings_toml(s_mcc_user_settings* out) {
	std::wstring path = resolve_halox_settings_path();
	if (path.empty()) return false;
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;

	std::string section;
	std::string line;
	int  module_idx = -1;
	enum class section_kind { other, display, audio, input, profile, bindings } kind = section_kind::other;
	while (std::getline(f, line)) {
		// strip trailing \r and # comments (only outside of values; values aren't quoted-with-#)
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
		size_t hash = line.find('#');
		if (hash != std::string::npos) line.resize(hash);
		// trim whitespace
		size_t a = 0; while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
		line.erase(0, a);
		while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
		if (line.empty()) continue;

		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			kind = section_kind::other;
			module_idx = -1;
			if (section == "display") kind = section_kind::display;
			else {
				auto dot = section.find('.');
				if (dot != std::string::npos) {
					std::string head = section.substr(0, dot);
					std::string tail = section.substr(dot + 1);
					for (int i = 0; i < libmcc::k_game_count; ++i) {
						if (tail == k_module_names[i]) { module_idx = i; break; }
					}
					if      (head == "audio")    kind = section_kind::audio;
					else if (head == "input")    kind = section_kind::input;
					else if (head == "profile")  kind = section_kind::profile;
					else if (head == "bindings") kind = section_kind::bindings;
				}
			}
			continue;
		}

		// key = value
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = line.substr(0, eq);
		while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
		std::string raw = line.substr(eq + 1);
		std::string val = trim_value(raw);

		auto as_bool = [&]() {
			return !val.empty() && (val[0] == 't' || val[0] == 'T' || val == "1");
		};
		auto as_int   = [&]() { return std::atoi(val.c_str()); };
		auto as_float = [&]() { return (float)std::atof(val.c_str()); };

		switch (kind) {
		case section_kind::display:
			if      (key == "vsync")            out->graphics.use_vsync       = as_bool();
			else if (key == "resolution_x")     out->graphics.resolution_x    = as_int();
			else if (key == "resolution_y")     out->graphics.resolution_y    = as_int();
			else if (key == "fullscreen_mode")  out->graphics.fullscreen_mode = as_int();
			else if (key == "frame_rate_limit") out->graphics.frame_rate_limit= as_float();
			else if (key == "use_hdr")          out->graphics.use_hdr         = as_bool();
			else if (key == "hdr_nits")         out->graphics.hdr_nits        = as_int();
			else if (key == "hdr_brightness")   out->graphics.hdr_brightness  = as_float();
			else if (key == "hdr_contrast")     out->graphics.hdr_contrast    = as_float();
			else if (key == "game_brightness")  out->graphics.game_brightness = as_float();
			break;
		case section_kind::audio:
			if (module_idx < 0) break;
			if      (key == "master_volume") out->audio[module_idx].master_volume = as_int();
			else if (key == "sfx_volume")    out->audio[module_idx].sfx_volume    = as_int();
			else if (key == "music_volume")  out->audio[module_idx].music_volume  = as_int();
			else if (key == "voice_volume")  out->audio[module_idx].voice_volume  = as_int();
			else if (key == "muted")         out->audio[module_idx].muted         = as_bool();
			break;
		case section_kind::input:
			if (module_idx < 0) break;
			if      (key == "mouse_sensitivity")        out->mouse_sensitivity[module_idx]        = as_float();
			else if (key == "mouse_look_inverted")      out->mouse_look_inverted[module_idx]      = as_bool();
			else if (key == "mouse_smoothing")          out->mouse_smoothing[module_idx]          = as_bool();
			else if (key == "mouse_acceleration")       out->mouse_acceleration[module_idx]       = as_bool();
			else if (key == "mouse_aircraft_inverted")  out->mouse_aircraft_inverted[module_idx]  = as_bool();
			else if (key == "gamepad_look_inverted")    out->gamepad_look_inverted[module_idx]    = as_bool();
			else if (key == "gamepad_aircraft_inverted") out->gamepad_aircraft_inverted[module_idx] = as_bool();
			break;
		case section_kind::profile:
			if (module_idx < 0) break;
			if      (key == "fov")                  out->fov[module_idx]                = as_int();
			else if (key == "vehicle_fov")          out->vehicle_fov[module_idx]        = as_int();
			else if (key == "vibration_disabled")   out->vibration_disabled[module_idx] = as_bool();
			else if (key == "auto_center_enabled")  out->auto_center_enabled[module_idx]= as_bool();
			else if (key == "hold_to_zoom")         out->hold_to_zoom[module_idx]       = as_bool();
			else if (key == "use_elite_model")      out->use_elite_model[module_idx]    = as_bool();
			else if (key == "use_modern_aim_control") out->use_modern_aim_control[module_idx] = as_bool();
			else if (key == "keyboard_mouse_button_preset") out->keyboard_mouse_button_preset[module_idx] = as_int();
			else if (key == "button_preset")        out->button_preset[module_idx]      = as_int();
			else if (key == "stick_preset")         out->stick_preset[module_idx]       = as_int();
			break;
		case section_kind::bindings: {
			if (module_idx < 0) break;
			// Key shape: "slot_<idx>" → value shape: "<abstract>:<vk0>,..,<vk4>"
			if (key.size() < 6 || key.compare(0, 5, "slot_") != 0) break;
			int slot = std::atoi(key.c_str() + 5);
			if (slot < 0 || slot >= 66) break;
			// Parse "abstract:vk0,vk1,vk2,vk3,vk4"
			size_t colon = val.find(':');
			if (colon == std::string::npos) break;
			std::string abs_s = val.substr(0, colon);
			std::string vks   = val.substr(colon + 1);
			auto& e = out->custom_kbm[module_idx][slot];
			e.abstract_button = std::atoi(abs_s.c_str());
			int vk_idx = 0;
			size_t p = 0;
			while (p < vks.size() && vk_idx < 5) {
				size_t comma = vks.find(',', p);
				std::string tok = (comma == std::string::npos)
					? vks.substr(p) : vks.substr(p, comma - p);
				e.virtual_key_codes[vk_idx++] = std::atoi(tok.c_str());
				if (comma == std::string::npos) break;
				p = comma + 1;
			}
			break;
		}
		case section_kind::other: break;
		}
	}
	out->loaded = true;
	return true;
}

bool mcc_user_settings_save() {
	return save_halox_user_settings_toml(g_settings);
}

// Heuristic: a TOML written by an older halox build (before bindings were
// serialized) loads with custom_kbm entries all at the unset sentinel
// (abstract_button == -1) for every game. That'd silently strip the user's
// MCC-imported bindings on every launch. If we detect that AND MCC's ini
// is reachable, re-pull the bindings from MCC and write them back out.
static bool any_custom_bindings(const s_mcc_user_settings& s) {
	for (int g = 0; g < libmcc::k_game_count; ++g) {
		for (int k = 0; k < 66; ++k) {
			if (s.custom_kbm[g][k].abstract_button >= 0) return true;
		}
	}
	return false;
}

void mcc_user_settings_initialize() {
	// Idempotent: this can be called twice (early — for window-size — and
	// again post-rasterizer). On the second call we already have data; do
	// nothing rather than reload and clobber any in-UI edits.
	if (g_settings.loaded) return;

	if (load_halox_user_settings_toml(&g_settings)) {
		CONSOLE_LOG_INFO("halox settings: loaded from halox_settings.toml");
		// Migration / recovery: if the loaded TOML has zero bindings across
		// every game, it was written by a build that didn't serialize
		// custom_kbm. Re-import bindings from MCC and re-save so this only
		// has to happen once.
		if (!any_custom_bindings(g_settings)) {
			s_mcc_user_settings tmp{};
			if (load_mcc_user_settings(&tmp) && any_custom_bindings(tmp)) {
				for (int g = 0; g < libmcc::k_game_count; ++g) {
					for (int k = 0; k < 66; ++k) {
						g_settings.custom_kbm[g][k] = tmp.custom_kbm[g][k];
					}
				}
				if (save_halox_user_settings_toml(g_settings)) {
					CONSOLE_LOG_INFO("halox settings: re-imported bindings from MCC ini "
						"(TOML lacked them — likely written by older halox build)");
				}
			}
		}
		return;
	}
	// Bootstrap: no halox toml yet → import from MCC's ini once and save out.
	load_mcc_user_settings(&g_settings);
	if (g_settings.loaded) {
		if (save_halox_user_settings_toml(g_settings)) {
			CONSOLE_LOG_INFO("halox settings: bootstrapped from MCC ini, wrote halox_settings.toml");
		} else {
			CONSOLE_LOG_WARN("halox settings: bootstrapped from MCC ini, but could not write halox_settings.toml");
		}
	} else {
		CONSOLE_LOG_WARN("halox settings: no MCC ini and no halox toml — using defaults");
	}
}

void mcc_user_settings_reload() {
	// Force-reload from disk. Useful if the user edited the toml externally.
	g_settings = s_mcc_user_settings{};
	mcc_user_settings_initialize();
}

void mcc_user_settings_stamp_profile(libmcc::e_module module, libmcc::s_player_profile* profile) {
	if (!profile) return;
	if (module < 0 || module >= libmcc::k_game_count) return;
	if (!g_settings.loaded) return;

	// FOV: 0 in the ini means "use the game's built-in default" — do NOT
	// stamp it on the profile or the projection matrix becomes degenerate
	// (which is what was breaking vehicle camera).
	if (g_settings.fov[module] > 0)
		profile->fov_setting = g_settings.fov[module];
	if (g_settings.vehicle_fov[module] > 0)
		profile->vehicle_fov_setting = g_settings.vehicle_fov[module];

	// Look inversion — separate gamepad/mouse channels.
	profile->look_controls_inverted           = g_settings.gamepad_look_inverted[module];
	profile->mouse_look_controls_inverted     = g_settings.mouse_look_inverted[module];
	profile->aircraft_controls_inverted       = g_settings.gamepad_aircraft_inverted[module];
	profile->mouse_aircraft_controls_inverted = g_settings.mouse_aircraft_inverted[module];

	// Customization toggles
	profile->vibration_disabled        = g_settings.vibration_disabled[module];
	profile->impulse_triggers_disabled = g_settings.impulse_triggers_disabled[module];
	profile->auto_center_enabled       = g_settings.auto_center_enabled[module];
	profile->crouch_lock_enabled       = g_settings.crouch_lock_enabled[module];
	profile->mk_crouch_lock_enabled    = g_settings.mk_crouch_lock_enabled[module];
	profile->clench_protection_enabled = g_settings.clench_protection_enabled[module];
	profile->use_female_voice          = g_settings.use_female_voice[module];
	profile->hold_to_zoom              = g_settings.hold_to_zoom[module];
	profile->use_elite_model           = g_settings.use_elite_model[module];
	profile->use_modern_aim_control    = g_settings.use_modern_aim_control[module];

	// Mirror mouse sensitivity onto the profile too. The raw-input pipeline
	// already scales by k_mcc_to_halox_mouse_scale; the game's own per-player
	// mouse sensitivity is a separate slider that vehicle code also reads, so
	// push the raw MCC value through unscaled.
	profile->mouse_sensitivity            = g_settings.mouse_sensitivity[module];
	profile->mouse_smoothing              = g_settings.mouse_smoothing[module];
	profile->mouse_acceleration           = g_settings.mouse_acceleration[module];
	profile->mouse_acceleration_min_rate  = g_settings.mouse_accel_min_rate[module];
	profile->mouse_acceleration_max_accel = g_settings.mouse_accel_max_accel[module];
	profile->mouse_acceleration_scale     = g_settings.mouse_accel_scale[module];
	profile->mouse_acceleration_exp       = g_settings.mouse_accel_exp[module];

	// Controller / look feel
	profile->keyboard_mouse_button_preset = g_settings.keyboard_mouse_button_preset[module];
	// libmcc declares these enum-like fields as bool; the ini holds 0/1/...
	// Cast preserves on/off; pure-int presets beyond 1 are lossy here, but
	// 0 (default) is by far the common case. Stamp the byte directly so a
	// non-zero preset index survives intact.
	*reinterpret_cast<uint8_t*>(&profile->button_preset)              = (uint8_t)g_settings.button_preset[module];
	*reinterpret_cast<uint8_t*>(&profile->stick_preset)               = (uint8_t)g_settings.stick_preset[module];
	profile->lefty_toggle                                             = g_settings.lefty_toggle[module];
	profile->swap_triggers_and_bumpers                                = g_settings.swap_triggers_and_bumpers[module];
	*reinterpret_cast<uint8_t*>(&profile->vertical_look_sensitivity)  = (uint8_t)g_settings.vertical_look_sensitivity[module];
	*reinterpret_cast<uint8_t*>(&profile->horizontal_look_sensitivity) = (uint8_t)g_settings.horizontal_look_sensitivity[module];
	profile->look_acceleration                                        = g_settings.look_acceleration[module];
	profile->look_axial_dead_zone                                     = g_settings.look_axial_dead_zone[module];
	profile->look_radial_dead_zone                                    = g_settings.look_radial_dead_zone[module];
	// Zoom / vehicle sensitivity multipliers default to 0.0f when MCC's ini
	// didn't provide an entry (struct zero-init). Halo 2 multiplies the look
	// delta by these → camera locks the moment the user scopes or boards a
	// vehicle. Fall back to 1.0f (identity multiplier) when zero so the base
	// sensitivity carries through. Any explicit non-zero value the user set
	// in MCC settings still wins.
	{
		float zm = g_settings.zoom_look_sens_mult[module];
		float vm = g_settings.vehicle_look_sens_mult[module];
		if (zm <= 0.0f) zm = 1.0f;
		if (vm <= 0.0f) vm = 1.0f;
		profile->zoom_look_sensitivity_multiplier    = zm;
		profile->vehicle_look_sensitivity_multiplier = vm;
	}
	profile->use_double_press_jump_to_jetpack                         = g_settings.use_double_press_jump_to_jetpack[module];
	profile->dual_wield_inverted                                      = g_settings.dual_wield_inverted[module];
	profile->controller_dual_wield_inverted                           = g_settings.controller_dual_wield_inverted[module];
	profile->controller_hornet_control_joystick                       = g_settings.controller_hornet_control_joystick[module];
	profile->controller_banshee_trick_buttons_swapped                 = g_settings.controller_banshee_trick_buttons_swapped[module];
	*reinterpret_cast<uint8_t*>(&profile->flying_camera_turn_sensitivity) = (uint8_t)g_settings.flying_camera_turn_sensitivity[module];
	*reinterpret_cast<uint8_t*>(&profile->flying_camera_panning)         = (uint8_t)g_settings.flying_camera_panning[module];
	*reinterpret_cast<uint8_t*>(&profile->flying_camera_speed)           = (uint8_t)g_settings.flying_camera_speed[module];
	*reinterpret_cast<uint8_t*>(&profile->flying_camera_thrust)          = (uint8_t)g_settings.flying_camera_thrust[module];
	*reinterpret_cast<uint8_t*>(&profile->theater_turn_sensitivity)      = (uint8_t)g_settings.theater_turn_sensitivity[module];
	*reinterpret_cast<uint8_t*>(&profile->theater_panning)               = (uint8_t)g_settings.theater_panning[module];
	*reinterpret_cast<uint8_t*>(&profile->theater_speed)                 = (uint8_t)g_settings.theater_speed[module];
	*reinterpret_cast<uint8_t*>(&profile->theater_thrust)                = (uint8_t)g_settings.theater_thrust[module];
	*reinterpret_cast<uint8_t*>(&profile->mk_theater_turn_sensitivity)   = (uint8_t)g_settings.mk_theater_turn_sensitivity[module];
	*reinterpret_cast<uint8_t*>(&profile->mk_theater_panning)            = (uint8_t)g_settings.mk_theater_panning[module];
	*reinterpret_cast<uint8_t*>(&profile->mk_theater_speed)              = (uint8_t)g_settings.mk_theater_speed[module];
	*reinterpret_cast<uint8_t*>(&profile->mk_theater_thrust)             = (uint8_t)g_settings.mk_theater_thrust[module];

	// Custom KB/M binding table — only stamp slots that the ini actually
	// supplied (abstract_button >= 0); leave the rest at the game's
	// built-in default so missing entries don't wipe usable bindings.
	for (int i = 0; i < 66; ++i) {
		const auto& src = g_settings.custom_kbm[module][i];
		if (src.abstract_button < 0) continue;
		profile->custom_keyboard_mouse_mapping_v2[i].abstract_button =
			(libmcc::e_game_abstract_button)src.abstract_button;
		for (int k = 0; k < 5; ++k) {
			profile->custom_keyboard_mouse_mapping_v2[i].virtual_key_codes[k] = src.virtual_key_codes[k];
		}
	}
}

void apply_mcc_settings_for_module(libmcc::e_module module) {
	if (module < 0 || module >= libmcc::k_game_count) return;
	if (!g_settings.loaded) return;

	// Mouse sensitivity & invert-Y go to win32_input (our raw-input pipeline).
	const float sens = g_settings.mouse_sensitivity[module] * k_mcc_to_halox_mouse_scale;
	win32_input_set_mouse_sensitivity(sens);
	win32_input_set_invert_y(g_settings.mouse_look_inverted[module]);

	// FOV + customization fields go into the per-game player profile that the
	// c_game_manager hands back when halo3/etc. calls get_player_profile().
	// All four local players share the same profile since this is a
	// single-window launcher.
	libmcc::s_player_profile profile{};
	for (int p = 0; p < libmcc::k_local_player_count; ++p) {
		auto local = (libmcc::e_local_player)p;
		player_manager()->get_player_profile(local, module, &profile);
		mcc_user_settings_stamp_profile(module, &profile);
		player_manager()->set_player_profile(local, module, &profile);
	}

	CONSOLE_LOG_INFO(
		"Applied MCC settings for module=%d: sens=%.3f (raw %.3f * %.4f), invertY=%d, FOV=%d, vehicleFOV=%d, autoCenter=%d, mouseAircraftInv=%d",
		(int)module, sens,
		g_settings.mouse_sensitivity[module], k_mcc_to_halox_mouse_scale,
		(int)g_settings.mouse_look_inverted[module],
		g_settings.fov[module], g_settings.vehicle_fov[module],
		(int)g_settings.auto_center_enabled[module],
		(int)g_settings.mouse_aircraft_inverted[module]);

	// Push current audio settings to the engine. Safe to call before audio is
	// up — apply_audio_for_module will return false and we can re-call later
	// (e.g., on slider drag, which always re-applies after any change).
	halox::audio::apply_audio_for_module(module);
}

void mcc_user_settings_reload_module(libmcc::e_module module) {
	if (module < 0 || module >= libmcc::k_game_count) return;
	// Re-parse the entire ini into a scratch copy and copy the fields for the
	// requested module index back over our singleton. This keeps other games'
	// in-memory edits intact while letting the user "Reset this game" cleanly.
	s_mcc_user_settings tmp{};
	if (!load_mcc_user_settings(&tmp)) return;

	const int m = (int)module;
	g_settings.mouse_sensitivity[m]                  = tmp.mouse_sensitivity[m];
	g_settings.mouse_look_inverted[m]                = tmp.mouse_look_inverted[m];
	g_settings.mouse_smoothing[m]                    = tmp.mouse_smoothing[m];
	g_settings.mouse_acceleration[m]                 = tmp.mouse_acceleration[m];
	g_settings.mouse_accel_min_rate[m]               = tmp.mouse_accel_min_rate[m];
	g_settings.mouse_accel_max_accel[m]              = tmp.mouse_accel_max_accel[m];
	g_settings.mouse_accel_scale[m]                  = tmp.mouse_accel_scale[m];
	g_settings.mouse_accel_exp[m]                    = tmp.mouse_accel_exp[m];
	g_settings.mouse_aircraft_inverted[m]            = tmp.mouse_aircraft_inverted[m];
	g_settings.gamepad_look_inverted[m]              = tmp.gamepad_look_inverted[m];
	g_settings.gamepad_aircraft_inverted[m]          = tmp.gamepad_aircraft_inverted[m];
	g_settings.fov[m]                                = tmp.fov[m];
	g_settings.vehicle_fov[m]                        = tmp.vehicle_fov[m];
	g_settings.vibration_disabled[m]                 = tmp.vibration_disabled[m];
	g_settings.impulse_triggers_disabled[m]          = tmp.impulse_triggers_disabled[m];
	g_settings.auto_center_enabled[m]                = tmp.auto_center_enabled[m];
	g_settings.crouch_lock_enabled[m]                = tmp.crouch_lock_enabled[m];
	g_settings.mk_crouch_lock_enabled[m]             = tmp.mk_crouch_lock_enabled[m];
	g_settings.clench_protection_enabled[m]          = tmp.clench_protection_enabled[m];
	g_settings.use_female_voice[m]                   = tmp.use_female_voice[m];
	g_settings.hold_to_zoom[m]                       = tmp.hold_to_zoom[m];
	g_settings.use_elite_model[m]                    = tmp.use_elite_model[m];
	g_settings.use_modern_aim_control[m]             = tmp.use_modern_aim_control[m];
	g_settings.keyboard_mouse_button_preset[m]       = tmp.keyboard_mouse_button_preset[m];
	g_settings.button_preset[m]                      = tmp.button_preset[m];
	g_settings.stick_preset[m]                       = tmp.stick_preset[m];
	g_settings.lefty_toggle[m]                       = tmp.lefty_toggle[m];
	g_settings.swap_triggers_and_bumpers[m]          = tmp.swap_triggers_and_bumpers[m];
	g_settings.vertical_look_sensitivity[m]          = tmp.vertical_look_sensitivity[m];
	g_settings.horizontal_look_sensitivity[m]        = tmp.horizontal_look_sensitivity[m];
	g_settings.look_acceleration[m]                  = tmp.look_acceleration[m];
	g_settings.look_axial_dead_zone[m]               = tmp.look_axial_dead_zone[m];
	g_settings.look_radial_dead_zone[m]              = tmp.look_radial_dead_zone[m];
	g_settings.zoom_look_sens_mult[m]                = tmp.zoom_look_sens_mult[m];
	g_settings.vehicle_look_sens_mult[m]             = tmp.vehicle_look_sens_mult[m];
	g_settings.use_double_press_jump_to_jetpack[m]   = tmp.use_double_press_jump_to_jetpack[m];
	g_settings.dual_wield_inverted[m]                = tmp.dual_wield_inverted[m];
	g_settings.controller_dual_wield_inverted[m]     = tmp.controller_dual_wield_inverted[m];
	g_settings.controller_hornet_control_joystick[m] = tmp.controller_hornet_control_joystick[m];
	g_settings.controller_banshee_trick_buttons_swapped[m] = tmp.controller_banshee_trick_buttons_swapped[m];
	g_settings.flying_camera_turn_sensitivity[m]     = tmp.flying_camera_turn_sensitivity[m];
	g_settings.flying_camera_panning[m]              = tmp.flying_camera_panning[m];
	g_settings.flying_camera_speed[m]                = tmp.flying_camera_speed[m];
	g_settings.flying_camera_thrust[m]               = tmp.flying_camera_thrust[m];
	g_settings.theater_turn_sensitivity[m]           = tmp.theater_turn_sensitivity[m];
	g_settings.theater_panning[m]                    = tmp.theater_panning[m];
	g_settings.theater_speed[m]                      = tmp.theater_speed[m];
	g_settings.theater_thrust[m]                     = tmp.theater_thrust[m];
	g_settings.mk_theater_turn_sensitivity[m]        = tmp.mk_theater_turn_sensitivity[m];
	g_settings.mk_theater_panning[m]                 = tmp.mk_theater_panning[m];
	g_settings.mk_theater_speed[m]                   = tmp.mk_theater_speed[m];
	g_settings.mk_theater_thrust[m]                  = tmp.mk_theater_thrust[m];
	for (int i = 0; i < 66; ++i) g_settings.custom_kbm[m][i] = tmp.custom_kbm[m][i];
}
