#pragma once

#include <libmcc/libmcc.h>

// One of MCC's `GraphicsQualityThrottles[PerGame]` blobs. The ini stores
// these as a nested-paren struct; we flatten the fields we care about. Most
// scale fields share a (ScaleVal, OffsetVal) shape — we keep both since the
// engine consumes them paired. Quality knobs (TextureResolution, etc.) are
// 0..3 (low → epic). Booleans like AntiAliasing/MotionBlur/Blood are direct.
struct s_mcc_quality_throttles {
	bool   loaded = false;

	// Scaled quality modifiers (scale + offset pairs)
	float  water_lod_scale            = 1.0f, water_lod_offset            = 0.0f;
	float  object_fade_scale          = 3.0f, object_fade_offset          = 0.0f;
	float  object_detail_scale        = 3.0f, object_detail_offset        = 0.0f;
	float  object_imposter_cutoff_scale = 3.0f, object_imposter_cutoff_offset = 0.0f;
	float  ss_dlight_max_scale        = 3.0f; int   ss_dlight_max_offset    = 0;
	float  ss_dlight_scale_scale      = 3.0f, ss_dlight_scale_offset      = 0.0f;
	float  cpu_dlight_max_scale       = 3.0f; int   cpu_dlight_max_offset   = 0;
	float  cpu_dlight_scale_scale     = 3.0f, cpu_dlight_scale_offset     = 0.0f;
	float  gpu_dlight_max_scale       = 3.0f; int   gpu_dlight_max_offset   = 0;
	float  gpu_dlight_scale_scale     = 3.0f, gpu_dlight_scale_offset     = 0.0f;
	float  shadow_generate_scale      = 3.0f; int   shadow_generate_offset = 0;
	float  shadow_quality_lod_scale   = 3.0f, shadow_quality_lod_offset   = 0.0f;
	float  effects_lod_distance_scale_scale   = 3.0f, effects_lod_distance_scale_offset   = 0.0f;
	float  decal_fade_distance_scale_scale    = 3.0f, decal_fade_distance_scale_offset    = 0.0f;
	float  decorator_fade_distance_scale_scale = 3.0f, decorator_fade_distance_scale_offset = 0.0f;
	float  structure_instance_lod_scale       = 3.0f, structure_instance_lod_offset       = 0.0f;
	float  instance_fade_scale                = 3.0f, instance_fade_offset                = 0.0f;

	// Bools
	bool   disable_dynamic_lighting_shadows = false;
	bool   disable_first_person_shadow      = false;
	bool   disable_cheap_particles          = false;
	bool   disable_patchy_fog               = false;
	bool   anti_aliasing                    = true;
	bool   motion_blur                      = true;
	bool   blood                            = true;

	// Quality enums (typically 0..3, low → epic)
	int    texture_resolution               = 2;
	int    texture_filtering_quality        = 2;
	int    lighting_quality                 = 2;
	int    effects_quality                  = 2;
	int    shadow_quality                   = 2;
	int    details_quality                  = 2;
	int    post_processing_quality          = 2;
	int    water_quality                    = 2;
};

// Display + rendering settings parsed from MCC's GameUserSettings.ini. These
// are NOT per-game — MCC stores one set for all titles. Halox applies the
// directly-relevant ones (vsync, resolution, window mode) to its own swap
// chain / window. The UE4 ScalabilityGroups are recorded for future use but
// halox doesn't have a UE4 pipeline to map them onto, so they're informational
// at present.
struct s_mcc_graphics_settings {
	bool   use_vsync           = true;     // [/Script/MCC.MCCGameUserSettings] bUseVSync
	bool   use_dynamic_res     = false;    // bUseDynamicResolution
	int    resolution_x        = 0;        // ResolutionSizeX (0 = leave halox default 1280)
	int    resolution_y        = 0;        // ResolutionSizeY (0 = leave halox default 720)
	int    fullscreen_mode     = -1;       // FullscreenMode: 0=fullscreen, 1=borderless, 2=windowed; -1=unset
	float  frame_rate_limit    = 0.0f;     // FrameRateLimit (0 = uncapped)
	bool   use_hdr             = false;    // bUseHDRDisplayOutput
	int    hdr_nits            = 1000;     // HDRDisplayOutputNits
	float  hdr_brightness      = 1000.0f;  // HDRBrightness
	float  hdr_contrast        = 5.0f;     // HDRContrast
	float  game_brightness     = 0.0f;     // GameBrightness

	// [ScalabilityGroups] (UE4 quality scalars: 0..3 typically, 100% for resolution scale)
	float  sg_resolution_quality = 100.0f; // sg.ResolutionQuality
	int    sg_view_distance      = 3;      // sg.ViewDistanceQuality
	int    sg_anti_aliasing      = 3;      // sg.AntiAliasingQuality
	int    sg_shadow             = 3;      // sg.ShadowQuality
	int    sg_post_process       = 3;      // sg.PostProcessQuality
	int    sg_texture            = 3;      // sg.TextureQuality
	int    sg_effects            = 3;      // sg.EffectsQuality
	int    sg_foliage            = 3;      // sg.FoliageQuality

	// Per-game throttles from GraphicsQualityThrottlesPerGame[i]; falls back
	// to `quality_default` (parsed from GraphicsQualityThrottles=...) when a
	// per-game block isn't present. Indexed by libmcc::e_module.
	s_mcc_quality_throttles quality_default;
	s_mcc_quality_throttles quality_per_game[libmcc::k_game_count];
};

// Per-game audio settings. Halox-owned (no equivalent in MCC's ini). Volumes
// are 0..100; 100 = unattenuated. Live edits propagate to the running game's
// audio engine via halox::audio::apply_audio_for_module.
struct s_halox_audio_settings {
	int  master_volume = 100;   // overall game volume
	int  sfx_volume    = 100;   // weapon/world sfx (per-engine bus, may be ignored if engine has no separate bus)
	int  music_volume  = 100;
	int  voice_volume  = 100;   // dialog / VO
	bool muted         = false; // master mute, multiplies through master_volume
};

// Parsed subset of MCC's GameUserSettings.ini that HaloX cares about, plus
// halox-only fields (audio, etc.). Indexed by libmcc::e_module — same ordering
// MCC uses. Persisted as TOML at %APPDATA%\HaloX\halox_settings.toml.
struct s_mcc_user_settings {
	bool   loaded = false;

	// Display + rendering — single set, not per-game.
	s_mcc_graphics_settings graphics;

	// Per-game audio — halox-owned, not from MCC.
	s_halox_audio_settings  audio[libmcc::k_game_count];

	// Mouse / aim
	float  mouse_sensitivity[libmcc::k_game_count]      = { 1.6f, 1.6f, 1.6f, 1.6f, 1.6f, 1.6f, 1.6f };
	bool   mouse_look_inverted[libmcc::k_game_count]    = {};
	bool   mouse_smoothing[libmcc::k_game_count]        = {};
	bool   mouse_acceleration[libmcc::k_game_count]     = {};
	float  mouse_accel_min_rate[libmcc::k_game_count]   = {};
	float  mouse_accel_max_accel[libmcc::k_game_count]  = {};
	float  mouse_accel_scale[libmcc::k_game_count]      = {};
	float  mouse_accel_exp[libmcc::k_game_count]        = {};
	bool   mouse_aircraft_inverted[libmcc::k_game_count] = {};
	bool   gamepad_look_inverted[libmcc::k_game_count]   = {};
	bool   gamepad_aircraft_inverted[libmcc::k_game_count] = {};

	// Field of view (0 in MCC means "use the game's built-in default")
	int    fov[libmcc::k_game_count]         = {};
	int    vehicle_fov[libmcc::k_game_count] = {};

	// Customization-section toggles (per game, indexed by libmcc::e_module)
	bool   vibration_disabled[libmcc::k_game_count]      = {};
	bool   impulse_triggers_disabled[libmcc::k_game_count] = {};
	bool   auto_center_enabled[libmcc::k_game_count]     = {};
	bool   crouch_lock_enabled[libmcc::k_game_count]     = {};
	bool   mk_crouch_lock_enabled[libmcc::k_game_count]  = {};
	bool   clench_protection_enabled[libmcc::k_game_count] = {};
	bool   use_female_voice[libmcc::k_game_count]        = {};
	bool   hold_to_zoom[libmcc::k_game_count]            = {};
	bool   use_elite_model[libmcc::k_game_count]         = {};
	bool   use_modern_aim_control[libmcc::k_game_count]  = {};

	// Controller layout / look feel
	int    keyboard_mouse_button_preset[libmcc::k_game_count] = {};
	int    button_preset[libmcc::k_game_count]                = {};
	int    stick_preset[libmcc::k_game_count]                 = {};
	bool   lefty_toggle[libmcc::k_game_count]                 = {};
	bool   swap_triggers_and_bumpers[libmcc::k_game_count]    = {};
	int    vertical_look_sensitivity[libmcc::k_game_count]    = {};
	int    horizontal_look_sensitivity[libmcc::k_game_count]  = {};
	bool   look_acceleration[libmcc::k_game_count]            = {};
	float  look_axial_dead_zone[libmcc::k_game_count]         = {};
	float  look_radial_dead_zone[libmcc::k_game_count]        = {};
	float  zoom_look_sens_mult[libmcc::k_game_count]          = {};
	float  vehicle_look_sens_mult[libmcc::k_game_count]       = {};
	bool   use_double_press_jump_to_jetpack[libmcc::k_game_count] = {};
	bool   dual_wield_inverted[libmcc::k_game_count]              = {};
	bool   controller_dual_wield_inverted[libmcc::k_game_count]   = {};
	bool   controller_hornet_control_joystick[libmcc::k_game_count] = {};
	bool   controller_banshee_trick_buttons_swapped[libmcc::k_game_count] = {};
	int    flying_camera_turn_sensitivity[libmcc::k_game_count] = {};
	int    flying_camera_panning[libmcc::k_game_count]          = {};
	int    flying_camera_speed[libmcc::k_game_count]            = {};
	int    flying_camera_thrust[libmcc::k_game_count]           = {};
	int    theater_turn_sensitivity[libmcc::k_game_count]    = {};
	int    theater_panning[libmcc::k_game_count]             = {};
	int    theater_speed[libmcc::k_game_count]               = {};
	int    theater_thrust[libmcc::k_game_count]              = {};
	int    mk_theater_turn_sensitivity[libmcc::k_game_count] = {};
	int    mk_theater_panning[libmcc::k_game_count]          = {};
	int    mk_theater_speed[libmcc::k_game_count]            = {};
	int    mk_theater_thrust[libmcc::k_game_count]           = {};

	// Per-game custom keyboard/mouse mapping table. The runtime profile holds
	// 66 entries indexed by libmcc::e_game_abstract_button, each with up to 5
	// alternative virtual-key codes; the ini stores the same shape.
	struct s_kbm_entry {
		int abstract_button = -1;            // -1 = unset (use built-in default)
		int virtual_key_codes[5] = {};
	};
	s_kbm_entry custom_kbm[libmcc::k_game_count][66] = {};
};

// Loads %USERPROFILE%\AppData\LocalLow\MCC\Saved\Config\WindowsNoEditor\GameUserSettings.ini.
// Returns false if the file is missing or unreadable; an empty default-init
// struct is left in *out so callers can use baselines.
bool load_mcc_user_settings(s_mcc_user_settings* out);

// Halox-owned TOML at %APPDATA%\HaloX\halox_settings.toml.  This is the
// source of truth at runtime — `mcc_user_settings_initialize` reads this
// first, and only falls back to the MCC ini importer on first launch (or
// when the toml is missing/corrupt).  load_halox_user_settings_toml returns
// false if the file is absent. save_halox_user_settings_toml writes
// atomically (temp + rename) so a crash mid-write can't leave a half file.
bool load_halox_user_settings_toml(s_mcc_user_settings* out);
bool save_halox_user_settings_toml(const s_mcc_user_settings& s);

// Singleton accessor — populated once at startup.
const s_mcc_user_settings* mcc_user_settings();
// Mutable accessor — for the imgui input panel that lets the user live-edit
// the parsed settings. Edits propagate immediately because every
// c_game_manager::get_player_profile call re-stamps the profile from this
// singleton (see mcc_user_settings_stamp_profile).
s_mcc_user_settings* mcc_user_settings_mut();
void mcc_user_settings_initialize();
void mcc_user_settings_reload();
// Re-parse the ini for a single game only — used by "Reset this game to MCC
// defaults" so a per-game reset doesn't wipe other games' edits.
void mcc_user_settings_reload_module(libmcc::e_module module);

// Persist current g_settings to the halox toml. UI calls this after any edit
// that should survive a relaunch. Returns false on I/O failure.
bool mcc_user_settings_save();

// MCC's MouseSensitivity[N] is on a scale where ~1.6 is the in-game default.
// Our raw-input pipeline expects a multiplier near 0.10 to feel similar, so
// translate by this constant when applying to win32_input.
constexpr float k_mcc_to_halox_mouse_scale = 0.0625f;  // 0.10 / 1.6

// Apply the parsed settings for the given game to the live win32 input state
// (sensitivity, invert-Y). Called by the launcher when the title selection
// changes and again right before launch_game.
void apply_mcc_settings_for_module(libmcc::e_module module);

// Overlay the parsed MCC settings for `module` onto an in-memory profile,
// without going through player_manager. Used by both apply_mcc_settings_for_module
// (which loops local players + writes back via set_player_profile) and by
// c_game_manager::get_player_profile (which calls this on every fetch so live
// imgui edits to the singleton propagate to the game on the next frame).
//
// Safe to call when settings have not been loaded — it will short-circuit and
// leave *profile untouched.
void mcc_user_settings_stamp_profile(libmcc::e_module module, libmcc::s_player_profile* profile);
