#pragma once

#include <libmcc/libmcc.h>

struct s_map_entry {
	libmcc::e_map_id id;
	libmcc::e_module module;
	const char* name;
	// Engine-internal "legacy" map id stored inside .mvar files. Distinct from
	// the libmcc e_map_id enum used everywhere else in halox. -1 if unknown
	// (no mvars exist for the map, or we haven't filled in the table yet).
	int legacy_map_id = -1;
};

// Friendly names for libmcc::e_map_id, with the owning module so we can filter
// the launcher dropdown by which game is selected. Order doesn't matter.
inline const s_map_entry k_map_entries[] = {
	// ---- Halo 1 ----
	{ libmcc::_map_id_halo1_pillar_of_autumn,           libmcc::_module_halo1, "Pillar of Autumn" },
	{ libmcc::_map_id_halo1_halo,                       libmcc::_module_halo1, "Halo" },
	{ libmcc::_map_id_halo1_truth_and_reconciliation,   libmcc::_module_halo1, "Truth and Reconciliation" },
	{ libmcc::_map_id_halo1_silent_cartographer,        libmcc::_module_halo1, "The Silent Cartographer" },
	{ libmcc::_map_id_halo1_assault_on_the_control_room,libmcc::_module_halo1, "Assault on the Control Room" },
	{ libmcc::_map_id_halo1_343_guilty_spark,           libmcc::_module_halo1, "343 Guilty Spark" },
	{ libmcc::_map_id_halo1_the_library,                libmcc::_module_halo1, "The Library" },
	{ libmcc::_map_id_halo1_two_betrayals,              libmcc::_module_halo1, "Two Betrayals" },
	{ libmcc::_map_id_halo1_keyes,                      libmcc::_module_halo1, "Keyes" },
	{ libmcc::_map_id_halo1_the_maw,                    libmcc::_module_halo1, "The Maw" },
	{ libmcc::_map_id_halo1_battle_creek,               libmcc::_module_halo1, "MP: Battle Creek" },
	{ libmcc::_map_id_halo1_sidewinder,                 libmcc::_module_halo1, "MP: Sidewinder" },
	{ libmcc::_map_id_halo1_damnation,                  libmcc::_module_halo1, "MP: Damnation" },
	{ libmcc::_map_id_halo1_rat_race,                   libmcc::_module_halo1, "MP: Rat Race" },
	{ libmcc::_map_id_halo1_prisoner,                   libmcc::_module_halo1, "MP: Prisoner" },
	{ libmcc::_map_id_halo1_hang_em_high,               libmcc::_module_halo1, "MP: Hang 'em High" },
	{ libmcc::_map_id_halo1_chill_out,                  libmcc::_module_halo1, "MP: Chill Out" },
	{ libmcc::_map_id_halo1_derelict,                   libmcc::_module_halo1, "MP: Derelict" },
	{ libmcc::_map_id_halo1_boarding_action,            libmcc::_module_halo1, "MP: Boarding Action" },
	{ libmcc::_map_id_halo1_chiron,                     libmcc::_module_halo1, "MP: Chiron TL-34" },
	{ libmcc::_map_id_halo1_blood_gulch,                libmcc::_module_halo1, "MP: Blood Gulch" },
	{ libmcc::_map_id_halo1_wizard,                     libmcc::_module_halo1, "MP: Wizard" },
	{ libmcc::_map_id_halo1_longest,                    libmcc::_module_halo1, "MP: Longest" },
	{ libmcc::_map_id_halo1_death_island,               libmcc::_module_halo1, "MP: Death Island" },
	{ libmcc::_map_id_halo1_danger_canyon,              libmcc::_module_halo1, "MP: Danger Canyon" },
	{ libmcc::_map_id_halo1_infinity,                   libmcc::_module_halo1, "MP: Infinity" },
	{ libmcc::_map_id_halo1_timberland,                 libmcc::_module_halo1, "MP: Timberland" },
	{ libmcc::_map_id_halo1_ice_fields,                 libmcc::_module_halo1, "MP: Ice Fields" },
	{ libmcc::_map_id_halo1_gephyrophobia,              libmcc::_module_halo1, "MP: Gephyrophobia" },

	// ---- Halo 2 ----
	{ libmcc::_map_id_halo2_the_heretic,                libmcc::_module_halo2, "The Heretic" },
	{ libmcc::_map_id_halo2_the_armory,                 libmcc::_module_halo2, "The Armory" },
	{ libmcc::_map_id_halo2_cairo_station,              libmcc::_module_halo2, "Cairo Station" },
	{ libmcc::_map_id_halo2_outskirts,                  libmcc::_module_halo2, "Outskirts" },
	{ libmcc::_map_id_halo2_metropolis,                 libmcc::_module_halo2, "Metropolis" },
	{ libmcc::_map_id_halo2_the_arbiter,                libmcc::_module_halo2, "The Arbiter" },
	{ libmcc::_map_id_halo2_the_oracle,                 libmcc::_module_halo2, "The Oracle" },
	{ libmcc::_map_id_halo2_delta_halo,                 libmcc::_module_halo2, "Delta Halo" },
	{ libmcc::_map_id_halo2_regret,                     libmcc::_module_halo2, "Regret" },
	{ libmcc::_map_id_halo2_sacred_icon,                libmcc::_module_halo2, "Sacred Icon" },
	{ libmcc::_map_id_halo2_quarantine_zone,            libmcc::_module_halo2, "Quarantine Zone" },
	{ libmcc::_map_id_halo2_gravemind,                  libmcc::_module_halo2, "Gravemind" },
	{ libmcc::_map_id_halo2_uprising,                   libmcc::_module_halo2, "Uprising" },
	{ libmcc::_map_id_halo2_high_charity,               libmcc::_module_halo2, "High Charity" },
	{ libmcc::_map_id_halo2_the_great_journey,          libmcc::_module_halo2, "The Great Journey" },
	{ libmcc::_map_id_halo2_lockout,                    libmcc::_module_halo2, "MP: Lockout" },
	{ libmcc::_map_id_halo2_ascension,                  libmcc::_module_halo2, "MP: Ascension" },
	{ libmcc::_map_id_halo2_midship,                    libmcc::_module_halo2, "MP: Midship" },
	{ libmcc::_map_id_halo2_ivory_tower,                libmcc::_module_halo2, "MP: Ivory Tower" },
	{ libmcc::_map_id_halo2_beaver_creek,               libmcc::_module_halo2, "MP: Beaver Creek" },
	{ libmcc::_map_id_halo2_burial_mounds,              libmcc::_module_halo2, "MP: Burial Mounds" },
	{ libmcc::_map_id_halo2_colossus,                   libmcc::_module_halo2, "MP: Colossus" },
	{ libmcc::_map_id_halo2_zanzibar,                   libmcc::_module_halo2, "MP: Zanzibar" },
	{ libmcc::_map_id_halo2_coagulation,                libmcc::_module_halo2, "MP: Coagulation" },
	{ libmcc::_map_id_halo2_headlong,                   libmcc::_module_halo2, "MP: Headlong" },
	{ libmcc::_map_id_halo2_waterworks,                 libmcc::_module_halo2, "MP: Waterworks" },
	{ libmcc::_map_id_halo2_foundation,                 libmcc::_module_halo2, "MP: Foundation" },
	{ libmcc::_map_id_halo2_containment,                libmcc::_module_halo2, "MP: Containment" },
	{ libmcc::_map_id_halo2_warlock,                    libmcc::_module_halo2, "MP: Warlock" },
	{ libmcc::_map_id_halo2_sanctuary,                  libmcc::_module_halo2, "MP: Sanctuary" },
	{ libmcc::_map_id_halo2_turf,                       libmcc::_module_halo2, "MP: Turf" },
	{ libmcc::_map_id_halo2_backwash,                   libmcc::_module_halo2, "MP: Backwash" },
	{ libmcc::_map_id_halo2_elongation,                 libmcc::_module_halo2, "MP: Elongation" },
	{ libmcc::_map_id_halo2_gemini,                     libmcc::_module_halo2, "MP: Gemini" },
	{ libmcc::_map_id_halo2_relic,                      libmcc::_module_halo2, "MP: Relic" },
	{ libmcc::_map_id_halo2_terminal,                   libmcc::_module_halo2, "MP: Terminal" },
	{ libmcc::_map_id_halo2_desolation,                 libmcc::_module_halo2, "MP: Desolation" },
	{ libmcc::_map_id_halo2_tombstone,                  libmcc::_module_halo2, "MP: Tombstone" },
	{ libmcc::_map_id_halo2_district,                   libmcc::_module_halo2, "MP: District" },
	{ libmcc::_map_id_halo2_uplift,                     libmcc::_module_halo2, "MP: Uplift" },

	// ---- Halo 3 ----
	{ libmcc::_map_id_halo3_arrival,                    libmcc::_module_halo3, "Arrival" },
	{ libmcc::_map_id_halo3_sierra_117,                 libmcc::_module_halo3, "Sierra 117" },
	{ libmcc::_map_id_halo3_crows_nest,                 libmcc::_module_halo3, "Crow's Nest" },
	{ libmcc::_map_id_halo3_tsavo_highway,              libmcc::_module_halo3, "Tsavo Highway" },
	{ libmcc::_map_id_halo3_the_storm,                  libmcc::_module_halo3, "The Storm" },
	{ libmcc::_map_id_halo3_floodgate,                  libmcc::_module_halo3, "Floodgate" },
	{ libmcc::_map_id_halo3_the_ark,                    libmcc::_module_halo3, "The Ark" },
	{ libmcc::_map_id_halo3_the_covenant,               libmcc::_module_halo3, "The Covenant" },
	{ libmcc::_map_id_halo3_cortana,                    libmcc::_module_halo3, "Cortana" },
	{ libmcc::_map_id_halo3_halo,                       libmcc::_module_halo3, "Halo" },
	{ libmcc::_map_id_halo3_construct,                  libmcc::_module_halo3, "MP: Construct" },
	{ libmcc::_map_id_halo3_epitaph,                    libmcc::_module_halo3, "MP: Epitaph" },
	{ libmcc::_map_id_halo3_guardian,                   libmcc::_module_halo3, "MP: Guardian" },
	{ libmcc::_map_id_halo3_high_ground,                libmcc::_module_halo3, "MP: High Ground" },
	{ libmcc::_map_id_halo3_isolation,                  libmcc::_module_halo3, "MP: Isolation" },
	{ libmcc::_map_id_halo3_last_resort,                libmcc::_module_halo3, "MP: Last Resort" },
	{ libmcc::_map_id_halo3_narrows,                    libmcc::_module_halo3, "MP: Narrows" },
	{ libmcc::_map_id_halo3_sandtrap,                   libmcc::_module_halo3, "MP: Sandtrap" },
	{ libmcc::_map_id_halo3_snowbound,                  libmcc::_module_halo3, "MP: Snowbound" },
	{ libmcc::_map_id_halo3_the_pit,                    libmcc::_module_halo3, "MP: The Pit" },
	{ libmcc::_map_id_halo3_valhalla,                   libmcc::_module_halo3, "MP: Valhalla" },
	{ libmcc::_map_id_halo3_foundry,                    libmcc::_module_halo3, "MP: Foundry" },
	{ libmcc::_map_id_halo3_rats_nest,                  libmcc::_module_halo3, "MP: Rat's Nest" },
	{ libmcc::_map_id_halo3_standoff,                   libmcc::_module_halo3, "MP: Standoff" },
	{ libmcc::_map_id_halo3_avalanche,                  libmcc::_module_halo3, "MP: Avalanche" },
	{ libmcc::_map_id_halo3_blackout,                   libmcc::_module_halo3, "MP: Blackout" },
	{ libmcc::_map_id_halo3_ghost_town,                 libmcc::_module_halo3, "MP: Ghost Town" },
	{ libmcc::_map_id_halo3_cold_storage,               libmcc::_module_halo3, "MP: Cold Storage" },
	{ libmcc::_map_id_halo3_assembly,                   libmcc::_module_halo3, "MP: Assembly" },
	{ libmcc::_map_id_halo3_orbital,                    libmcc::_module_halo3, "MP: Orbital" },
	{ libmcc::_map_id_halo3_sandbox,                    libmcc::_module_halo3, "MP: Sandbox" },
	{ libmcc::_map_id_halo3_citadel,                    libmcc::_module_halo3, "MP: Citadel" },
	{ libmcc::_map_id_halo3_heretic,                    libmcc::_module_halo3, "MP: Heretic" },
	{ libmcc::_map_id_halo3_longshore,                  libmcc::_module_halo3, "MP: Longshore" },
	{ libmcc::_map_id_halo3_epilogue,                   libmcc::_module_halo3, "Epilogue" },

	// ---- Halo 4 ----
	{ libmcc::_map_id_halo4_prologue,                   libmcc::_module_halo4, "Prologue" },
	{ libmcc::_map_id_halo4_dawn,                       libmcc::_module_halo4, "Dawn" },
	{ libmcc::_map_id_halo4_requiem,                    libmcc::_module_halo4, "Requiem" },
	{ libmcc::_map_id_halo4_forerunner,                 libmcc::_module_halo4, "Forerunner" },
	{ libmcc::_map_id_halo4_infinity,                   libmcc::_module_halo4, "Infinity" },
	{ libmcc::_map_id_halo4_reclaimer,                  libmcc::_module_halo4, "Reclaimer" },
	{ libmcc::_map_id_halo4_shutdown,                   libmcc::_module_halo4, "Shutdown" },
	{ libmcc::_map_id_halo4_composer,                   libmcc::_module_halo4, "Composer" },
	{ libmcc::_map_id_halo4_midnight,                   libmcc::_module_halo4, "Midnight" },
	{ libmcc::_map_id_halo4_epilogue,                   libmcc::_module_halo4, "Epilogue" },
	{ libmcc::_map_id_halo4_adrift,                     libmcc::_module_halo4, "MP: Adrift" },
	{ libmcc::_map_id_halo4_abandon,                    libmcc::_module_halo4, "MP: Abandon" },
	{ libmcc::_map_id_halo4_complex,                    libmcc::_module_halo4, "MP: Complex" },
	{ libmcc::_map_id_halo4_exile,                      libmcc::_module_halo4, "MP: Exile" },
	{ libmcc::_map_id_halo4_haven,                      libmcc::_module_halo4, "MP: Haven" },
	{ libmcc::_map_id_halo4_longbow,                    libmcc::_module_halo4, "MP: Longbow" },
	{ libmcc::_map_id_halo4_meltdown,                   libmcc::_module_halo4, "MP: Meltdown" },
	{ libmcc::_map_id_halo4_ragnarok,                   libmcc::_module_halo4, "MP: Ragnarok" },
	{ libmcc::_map_id_halo4_solace,                     libmcc::_module_halo4, "MP: Solace" },
	{ libmcc::_map_id_halo4_vortex,                     libmcc::_module_halo4, "MP: Vortex" },
	{ libmcc::_map_id_halo4_ravine,                     libmcc::_module_halo4, "MP: Ravine" },
	{ libmcc::_map_id_halo4_impact,                     libmcc::_module_halo4, "MP: Impact" },
	{ libmcc::_map_id_halo4_erosion,                    libmcc::_module_halo4, "MP: Erosion" },
	{ libmcc::_map_id_halo4_forge_island,               libmcc::_module_halo4, "MP: Forge Island" },
	{ libmcc::_map_id_halo4_wreckage,                   libmcc::_module_halo4, "MP: Wreckage" },
	{ libmcc::_map_id_halo4_harvest,                    libmcc::_module_halo4, "MP: Harvest" },
	{ libmcc::_map_id_halo4_shatter,                    libmcc::_module_halo4, "MP: Shatter" },
	{ libmcc::_map_id_halo4_landfall,                   libmcc::_module_halo4, "MP: Landfall" },
	{ libmcc::_map_id_halo4_monolith,                   libmcc::_module_halo4, "MP: Monolith" },
	{ libmcc::_map_id_halo4_skyline,                    libmcc::_module_halo4, "MP: Skyline" },
	{ libmcc::_map_id_halo4_daybreak,                   libmcc::_module_halo4, "MP: Daybreak" },
	{ libmcc::_map_id_halo4_outcast,                    libmcc::_module_halo4, "MP: Outcast" },
	{ libmcc::_map_id_halo4_perdition,                  libmcc::_module_halo4, "MP: Perdition" },
	{ libmcc::_map_id_halo4_pitfall,                    libmcc::_module_halo4, "MP: Pitfall" },
	{ libmcc::_map_id_halo4_vertigo,                    libmcc::_module_halo4, "MP: Vertigo" },
	{ libmcc::_map_id_halo4_ff_chopperbowl,             libmcc::_module_halo4, "FF: Chopper Bowl" },
	{ libmcc::_map_id_halo4_ff_sniperalley,             libmcc::_module_halo4, "FF: Sniper Alley" },
	{ libmcc::_map_id_halo4_ff_fortsw,                  libmcc::_module_halo4, "FF: Fort SW" },
	{ libmcc::_map_id_halo4_ff_temple,                  libmcc::_module_halo4, "FF: Temple" },
	{ libmcc::_map_id_halo4_ff_scurve,                  libmcc::_module_halo4, "FF: S-Curve" },
	{ libmcc::_map_id_halo4_ff_courtyard,               libmcc::_module_halo4, "FF: Courtyard" },
	{ libmcc::_map_id_halo4_ff_complex,                 libmcc::_module_halo4, "FF: Complex" },
	{ libmcc::_map_id_halo4_ff_valhalla,                libmcc::_module_halo4, "FF: Valhalla" },
	{ libmcc::_map_id_halo4_ff_factory,                 libmcc::_module_halo4, "FF: Factory" },
	{ libmcc::_map_id_halo4_ff_mezzanie,                libmcc::_module_halo4, "FF: Mezzanine" },
	{ libmcc::_map_id_halo4_ff_caverns,                 libmcc::_module_halo4, "FF: Caverns" },
	{ libmcc::_map_id_halo4_ff_vortex,                  libmcc::_module_halo4, "FF: Vortex" },
	{ libmcc::_map_id_halo4_ff_breach,                  libmcc::_module_halo4, "FF: Breach" },
	{ libmcc::_map_id_halo4_ff_hillside,                libmcc::_module_halo4, "FF: Hillside" },
	{ libmcc::_map_id_halo4_ff_engine,                  libmcc::_module_halo4, "FF: Engine" },

	// ---- Groundhog (Halo 2 Anniversary) ----
	{ libmcc::_map_id_groundhog_lockout,                libmcc::_module_groundhog, "MP: Lockout" },
	{ libmcc::_map_id_groundhog_ascension,              libmcc::_module_groundhog, "MP: Ascension" },
	{ libmcc::_map_id_groundhog_zanzibar,               libmcc::_module_groundhog, "MP: Zanzibar" },
	{ libmcc::_map_id_groundhog_coagulation,            libmcc::_module_groundhog, "MP: Coagulation" },
	{ libmcc::_map_id_groundhog_warlock,                libmcc::_module_groundhog, "MP: Warlock" },
	{ libmcc::_map_id_groundhog_sanctuary,              libmcc::_module_groundhog, "MP: Sanctuary" },
	{ libmcc::_map_id_groundhog_relic,                  libmcc::_module_groundhog, "MP: Relic" },
	{ libmcc::_map_id_groundhog_forge_skybox01,         libmcc::_module_groundhog, "Forge Skybox 01" },
	{ libmcc::_map_id_groundhog_forge_skybox02,         libmcc::_module_groundhog, "Forge Skybox 02" },
	{ libmcc::_map_id_groundhog_forge_skybox03,         libmcc::_module_groundhog, "Forge Skybox 03" },

	// ---- Halo 3: ODST ----
	{ libmcc::_map_id_halo3odst_prepare_to_drop,        libmcc::_module_halo3odst, "Prepare to Drop" },
	{ libmcc::_map_id_halo3odst_mombasa_streets,        libmcc::_module_halo3odst, "Mombasa Streets" },
	{ libmcc::_map_id_halo3odst_tayari_plaza,           libmcc::_module_halo3odst, "Tayari Plaza" },
	{ libmcc::_map_id_halo3odst_uplift_reserve,         libmcc::_module_halo3odst, "Uplift Reserve" },
	{ libmcc::_map_id_halo3odst_kizingo_boulevard,      libmcc::_module_halo3odst, "Kizingo Boulevard" },
	{ libmcc::_map_id_halo3odst_oni_alpha_site,         libmcc::_module_halo3odst, "ONI Alpha Site" },
	{ libmcc::_map_id_halo3odst_nmpd_hq,                libmcc::_module_halo3odst, "NMPD HQ" },
	{ libmcc::_map_id_halo3odst_kikowani_station,       libmcc::_module_halo3odst, "Kikowani Station" },
	{ libmcc::_map_id_halo3odst_data_hive,              libmcc::_module_halo3odst, "Data Hive" },
	{ libmcc::_map_id_halo3odst_coastal_highway,        libmcc::_module_halo3odst, "Coastal Highway" },
	{ libmcc::_map_id_halo3odst_epilogue,               libmcc::_module_halo3odst, "Epilogue" },

	// ---- Halo Reach ----
	{ libmcc::_map_id_haloreach_noble_actual,           libmcc::_module_haloreach, "Noble Actual" },
	{ libmcc::_map_id_haloreach_winter_contingency,     libmcc::_module_haloreach, "Winter Contingency" },
	{ libmcc::_map_id_haloreach_oni_sword_base,         libmcc::_module_haloreach, "ONI: Sword Base" },
	{ libmcc::_map_id_haloreach_nightfall,              libmcc::_module_haloreach, "Nightfall" },
	{ libmcc::_map_id_haloreach_tip_of_the_spear,       libmcc::_module_haloreach, "Tip of the Spear" },
	{ libmcc::_map_id_haloreach_long_night_of_solace,   libmcc::_module_haloreach, "Long Night of Solace" },
	{ libmcc::_map_id_haloreach_exodus,                 libmcc::_module_haloreach, "Exodus" },
	{ libmcc::_map_id_haloreach_new_alexandria,         libmcc::_module_haloreach, "New Alexandria" },
	{ libmcc::_map_id_haloreach_the_package,            libmcc::_module_haloreach, "The Package" },
	{ libmcc::_map_id_haloreach_the_pillar_of_autumn,   libmcc::_module_haloreach, "The Pillar of Autumn" },
	{ libmcc::_map_id_haloreach_the_pillar_of_autumn_credits, libmcc::_module_haloreach, "Epilogue (Pillar Credits)" },
	{ libmcc::_map_id_haloreach_lone_wolf,              libmcc::_module_haloreach, "Lone Wolf" },
	{ libmcc::_map_id_haloreach_boardwalk,              libmcc::_module_haloreach, "MP: Boardwalk", 1035 },
	{ libmcc::_map_id_haloreach_boneyard,               libmcc::_module_haloreach, "MP: Boneyard", 1080 },
	{ libmcc::_map_id_haloreach_countdown,              libmcc::_module_haloreach, "MP: Countdown", 1020 },
	{ libmcc::_map_id_haloreach_powerhouse,             libmcc::_module_haloreach, "MP: Powerhouse", 1055 },
	{ libmcc::_map_id_haloreach_reflection,             libmcc::_module_haloreach, "MP: Reflection", 1150 },
	{ libmcc::_map_id_haloreach_spire,                  libmcc::_module_haloreach, "MP: Spire", 1200 },
	{ libmcc::_map_id_haloreach_sword_base,             libmcc::_module_haloreach, "MP: Sword Base", 1000 },
	{ libmcc::_map_id_haloreach_zealot,                 libmcc::_module_haloreach, "MP: Zealot", 1040 },
	{ libmcc::_map_id_haloreach_anchor_9,               libmcc::_module_haloreach, "MP: Anchor 9", 2001 },
	{ libmcc::_map_id_haloreach_breakpoint,             libmcc::_module_haloreach, "MP: Breakpoint", 2002 },
	{ libmcc::_map_id_haloreach_tempest,                libmcc::_module_haloreach, "MP: Tempest", 2004 },
	{ libmcc::_map_id_haloreach_condemned,              libmcc::_module_haloreach, "MP: Condemned", 1500 },
	{ libmcc::_map_id_haloreach_highlands,              libmcc::_module_haloreach, "MP: Highlands", 1510 },
	{ libmcc::_map_id_haloreach_battle_canyon,          libmcc::_module_haloreach, "MP: Battle Canyon", 10020 },
	{ libmcc::_map_id_haloreach_penance,                libmcc::_module_haloreach, "MP: Penance", 10010 },
	{ libmcc::_map_id_haloreach_ridgeline,              libmcc::_module_haloreach, "MP: Ridgeline", 10030 },
	{ libmcc::_map_id_haloreach_solitary,               libmcc::_module_haloreach, "MP: Solitary", 10070 },
	{ libmcc::_map_id_haloreach_high_noon,              libmcc::_module_haloreach, "MP: High Noon", 10060 },
	{ libmcc::_map_id_haloreach_breakneck,              libmcc::_module_haloreach, "MP: Breakneck", 10050 },
	{ libmcc::_map_id_haloreach_forge_world,            libmcc::_module_haloreach, "Forge World", 3006 },
	{ libmcc::_map_id_haloreach_beachhead,              libmcc::_module_haloreach, "FF: Beachhead", 7060 },
	{ libmcc::_map_id_haloreach_corvette,               libmcc::_module_haloreach, "FF: Corvette", 7110 },
	{ libmcc::_map_id_haloreach_courtyard,              libmcc::_module_haloreach, "FF: Courtyard", 7020 },
	{ libmcc::_map_id_haloreach_glacier,                libmcc::_module_haloreach, "FF: Glacier", 7130 },
	{ libmcc::_map_id_haloreach_holdout,                libmcc::_module_haloreach, "FF: Holdout", 7080 },
	{ libmcc::_map_id_haloreach_outpost,                libmcc::_module_haloreach, "FF: Outpost", 7030 },
	{ libmcc::_map_id_haloreach_overlook,               libmcc::_module_haloreach, "FF: Overlook", 7000 },
	{ libmcc::_map_id_haloreach_waterfront,             libmcc::_module_haloreach, "FF: Waterfront", 7040 },
	{ libmcc::_map_id_haloreach_unearthed,              libmcc::_module_haloreach, "FF: Unearthed" },
	{ libmcc::_map_id_haloreach_installation_04,        libmcc::_module_haloreach, "Installation 04" },

	// ---- Halo 3 anniversary remasters ----
	{ libmcc::_map_id_halo3_s3d_waterfall,              libmcc::_module_halo3, "MP: Waterfall (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_edge,                   libmcc::_module_halo3, "MP: Edge (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_turf,                   libmcc::_module_halo3, "MP: Turf (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_reactor,                libmcc::_module_halo3, "MP: Reactor (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_powerhouse,             libmcc::_module_halo3, "MP: Powerhouse (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_avalanche,              libmcc::_module_halo3, "MP: Avalanche (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_lockout,                libmcc::_module_halo3, "MP: Lockout (Anniv.)" },
	{ libmcc::_map_id_halo3_s3d_sky_bridgenew,          libmcc::_module_halo3, "MP: Sky Bridge (Anniv.)" },
};

inline constexpr int k_map_entry_count = sizeof(k_map_entries) / sizeof(k_map_entries[0]);

inline const char* find_map_name(libmcc::e_map_id id) {
	for (auto& e : k_map_entries) if (e.id == id) return e.name;
	return "(unknown)";
}

// Translate libmcc e_map_id → engine-internal "legacy" map id stored in mvars.
// Returns -1 when the table doesn't have a value for this map (campaign-only,
// or another module we haven't filled in). The mvar dropdown filter falls
// back to "show all" when this returns -1.
inline int find_legacy_map_id(libmcc::e_map_id id) {
	for (auto& e : k_map_entries) if (e.id == id) return e.legacy_map_id;
	return -1;
}

// Derive which game-mode buckets a map can be launched in from its display
// name. The naming convention in k_map_entries is:
//   "MP: <name>"        — multiplayer / forge
//   "FF: <name>"        — firefight (haloreach + halo4)
//   "<name> (Anniv.)"   — anniversary remasters of MP maps (multiplayer)
//   "Forge ..." / "Forge World" — multiplayer / forge
//   <anything else>     — campaign
//
// Returns 0 if the map shouldn't appear under the given mode.
inline bool map_entry_matches_mode(const s_map_entry& e, libmcc::e_game_mode mode) {
	const bool is_mp = (e.name[0] == 'M' && e.name[1] == 'P' && e.name[2] == ':');
	const bool is_ff = (e.name[0] == 'F' && e.name[1] == 'F' && e.name[2] == ':');
	const bool is_forge = (strncmp(e.name, "Forge", 5) == 0);
	const bool is_anniv = (strstr(e.name, "(Anniv.)") != nullptr);
	switch (mode) {
	case libmcc::_game_mode_campaign:
		// Campaign-only modules (halo3odst etc.) shouldn't show MP/FF maps.
		return !is_mp && !is_ff && !is_forge && !is_anniv;
	case libmcc::_game_mode_multiplayer:
		return is_mp || is_forge || is_anniv;
	case libmcc::_game_mode_firefight:
		return is_ff;
	case libmcc::_game_mode_spartan_ops:
		// Halo4 spartan ops re-uses MP/FF maps; no first-class entries here yet.
		return false;
	default:
		// UI shell / unspecified — show everything so the dropdown isn't empty.
		return true;
	}
}

// Find the first map matching (module, mode). Used to seed a sensible default
// when the user changes the mode and the current selection no longer fits.
inline libmcc::e_map_id find_first_map_for(libmcc::e_module module, libmcc::e_game_mode mode) {
	for (auto& e : k_map_entries) {
		if (e.module != module) continue;
		if (!map_entry_matches_mode(e, mode)) continue;
		return e.id;
	}
	return (libmcc::e_map_id)(-1);
}

// Case-insensitive substring lookup of a map id by name fragment, optionally
// scoped to a module. Used by the --launch CLI parser. Returns (libmcc::e_map_id)(-1)
// if no match.
inline libmcc::e_map_id find_map_id_by_name_fragment(const char* fragment, libmcc::e_module module = libmcc::k_module_none) {
	if (!fragment || !*fragment) return (libmcc::e_map_id)(-1);
	// Normalize both haystack and needle: lowercase ASCII, collapse runs of
	// any of [space _ - ' . :] into a single underscore, drop "MP:"/"FF:"
	// prefixes from the table entries so "lockout" matches "MP: Lockout"
	// and "cairo_station" matches "Cairo Station".
	auto normalize = [](const char* in, char* out, size_t out_cap) {
		size_t o = 0; bool prev_sep = true;  // start collapses leading seps
		for (const char* p = in; *p && o + 1 < out_cap; ++p) {
			unsigned char c = (unsigned char)*p;
			if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
			bool is_sep = (c == ' ' || c == '_' || c == '-' || c == '\'' ||
				c == '.' || c == ':' || c == ',' || c == '(' || c == ')');
			if (is_sep) {
				if (!prev_sep) { out[o++] = '_'; prev_sep = true; }
			} else {
				out[o++] = (char)c; prev_sep = false;
			}
		}
		// Trim trailing separator.
		while (o > 0 && out[o - 1] == '_') --o;
		out[o] = 0;
	};
	auto strip_prefix = [](const char* s) -> const char* {
		// Drop "MP: " / "FF: " genre prefix so the user can search by bare name.
		if ((s[0] == 'M' || s[0] == 'F') && s[1] == 'P' || s[1] == 'F') {
			// fall through — generic check below covers it
		}
		if (s[0] && s[1] == 'P' && s[2] == ':' && s[3] == ' ') return s + 4;
		if (s[0] && s[1] == 'F' && s[2] == ':' && s[3] == ' ') return s + 4;
		return s;
	};
	char nfrag[160]; normalize(fragment, nfrag, sizeof(nfrag));
	if (nfrag[0] == 0) return (libmcc::e_map_id)(-1);

	for (auto& e : k_map_entries) {
		if (module != libmcc::k_module_none && e.module != module) continue;
		char nname[160]; normalize(strip_prefix(e.name), nname, sizeof(nname));
		// Substring match.
		if (strstr(nname, nfrag)) return e.id;
	}
	return (libmcc::e_map_id)(-1);
}
