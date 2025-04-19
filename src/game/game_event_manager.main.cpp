#include "game_event_manager.h"

using namespace libmcc;

static bool initialized = false;

static libmcc::i_game_event_manager_vftable __vftable;

c_game_event_manager::c_game_event_manager() {
	CoCreateGuid(&m_guid);
}

bool libmcc::c_game_event_manager::AchievementEarned(const wchar_t* xuid, const GUID* guid, const wchar_t*, int) {
	return false;
}

bool libmcc::c_game_event_manager::AshesToAshes(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Assist(const wchar_t* xuid, int, const GUID* guid, const wchar_t*, int, int, const GUID*, int, int, int, int, int, int, int, int, const wchar_t*, int, int, int, int) {
	return false;
}

bool libmcc::c_game_event_manager::AudioLogClaimed(const wchar_t* xuid, const GUID* guid, int) {
	return false;
}

bool libmcc::c_game_event_manager::Base(const wchar_t* xuid, const GUID* guid) {
	return false;
}

void libmcc::c_game_event_manager::Base(const wchar_t* xuid, int a3, const GUID* a4, const wchar_t* a5, int a6, int a7, const GUID* a8, int a9, int a10, int a11, int a12, int a13, int a14, int a15, int a16, const wchar_t* a17, int a18, int a19, int a20, int a21) {
}

bool libmcc::c_game_event_manager::BIFactControllerSettings(const wchar_t* xuid, const GUID* guid, int a4, const wchar_t* a5, int a6, int a7, int a8) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactDeepLink(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactDeepLinkRecieve(const wchar_t* xuid, const GUID* guid, const wchar_t*, const wchar_t*) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactDeepLinkSend(const wchar_t* xuid, const GUID* guid, const wchar_t*, const wchar_t*) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactDualWield(const wchar_t* xuid, const GUID* guid, int, const wchar_t*, int, int, int, const void*) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactGameSession(const wchar_t* a2, const GUID* a3, const void* a4, const void* a5, const wchar_t* a6, const void* a7, const void* a8, int a9, const wchar_t* a10, int a11, int a12, const void* a13, const wchar_t* a14, int a15, int a16, int a17, const void* a18, int a19, int a20, const void* a21, const void* a22, int a23, int a24, int a25, int a26, int a27, int a28, int a29) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactLoadout(const wchar_t* xuid, const GUID* guid, int a4, const wchar_t* a5, int a6, int a7, int a8, int a9, int a10) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactMatchmaking(const wchar_t* xuid, const GUID* guid, int a4, const wchar_t* a5, const void* a6, int a7, const void* a8, int a9, const void* a10, int a11, int a12, const GUID* a13) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactMatchmakingDetails(const wchar_t* xuid, const GUID* guid, int a4, int a5, int a6, int a7, int a8, const void* a9, const void* a10, int a11, const wchar_t* a12, int a13, int a14, int a15, int a16) {
	return false;
}

bool libmcc::c_game_event_manager::BIFactMedia(const wchar_t* xuid, const GUID* guid, int a4, const wchar_t* a5, const void* a6, int a7, const void* a8, const wchar_t* a9) {
	return false;
}

bool libmcc::c_game_event_manager::BirdOfPrey(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BitsAndPiecesDestroyed(const wchar_t* xuid, const GUID* guid) {
	return false;
}

void libmcc::c_game_event_manager::BroadcastingAssist(const wchar_t* xuid, const GUID* guid, const void* a4, unsigned int a5) {
}

void libmcc::c_game_event_manager::BroadcastingDeath(const wchar_t* xuid, const GUID* guid) {
}

bool libmcc::c_game_event_manager::BroadcastingHeartbeat(const wchar_t* xuid, const GUID* guid, const void*) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingKill(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingMatchEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingMatchRoundEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingMatchRoundStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingMatchStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingMedal(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingPlayerJoined(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingPlayerLeft(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::BroadcastingPlayerSpawn(const wchar_t* xuid, const GUID* guid) {
	return false;
}

void libmcc::c_game_event_manager::BroadcastingPlayerSwitchedTeams(const wchar_t* xuid, const GUID*, const FILETIME*, int index, int) {
}

void libmcc::c_game_event_manager::BroadcastingScore(const wchar_t* xuid, const GUID*, int index, const FILETIME*, int, int, int, int) {
}

bool libmcc::c_game_event_manager::BroadcastingStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::CampaignDifficulty(const wchar_t* xuid, const GUID* guid, const wchar_t*, const wchar_t*) {
	return false;
}

bool libmcc::c_game_event_manager::ChallengeCompleted(const wchar_t* xuid, const GUID* guid, const wchar_t*, int) {
	return false;
}

bool libmcc::c_game_event_manager::ClassicModeSwitched(const wchar_t* xuid, const GUID* guid, int) {
	return false;
}

bool libmcc::c_game_event_manager::CleverGirl(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ClueClaimed(const wchar_t* xuid, const GUID* guid, int) {
	return false;
}

bool libmcc::c_game_event_manager::CompletionCount(const wchar_t* xuid, const GUID* guid, int) {
	return false;
}

bool libmcc::c_game_event_manager::CoopMissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::CoopSpartanOpsMissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Customization(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::DashboardContext(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Death(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::DollFound(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::EliteWin(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Emblem(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::EnemyDefeated(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::FriendsBestedOnHeroLeaderboard(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::GameProgress(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::GameVarSaved(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::GrenadeStick(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::HelloNurse(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::InGamePresence(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ISeeYou(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Joinability(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Lobby(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MainMenuPresence(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MapVarSaved(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MatchmakingHopper(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MediaUsage(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MeldOfferPresented(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MeldOfferResponded(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MeldPageAction(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MeldPageView(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MortardomWraithsKilled(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MultiplayerGameEngine(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MultiplayerMap(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MultiplayerRoundEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::MultiplayerRoundStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::NappersCaught(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::NewsStoryRead(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ObjectiveEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ObjectiveStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PageAction(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PageView(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PhantomHunter(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PigsCanFly(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerCheckedInToday(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerDefeated(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResults(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResultsDamageStat(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResultsGriefingStat(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResultsGriefingStats(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResultsInterestStats(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerGameResultsMedal(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerSessionEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerSessionPause(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerSessionResume(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerSessionStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlayerSpawned(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlaylistCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::PlaylistProgress(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RankedStatsDNFInfo(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RankedStatsOverride(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RankedStatsPenalty(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RankedStatsUpdate(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RankedUpSpartanIv(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RealtimeFlagCaptured(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RealtimeMedal(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RealtimePilotedVehicle(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::RivalID(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SectionEnd(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SectionStart(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SectionStats(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SessionSizeUpdate(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SizeIsEverything(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SkeetShooter(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SkullClaimed(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SoloMissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SoloSpartanOpsMissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SpartanOpsMissionCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::Supercombine(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::SurvivalSpace(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::TerminalFound(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::TerminalId(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::TicketsEarned(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::TitleCompleted(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::TitleLaunched(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ValhallaSign(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ViewOffer(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::VIPStatusEarned(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::WhatAboutTanksDestroyed(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::WonWarGame(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ZanzibarSign(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::FirefightGameResults(const wchar_t* xuid, const GUID* guid) {
	return false;
}

bool libmcc::c_game_event_manager::ScavengerHuntObjectFound(const wchar_t* xuid, const GUID* guid) {
	return false;
}

void libmcc::c_game_event_manager::_() {
}

void libmcc::c_game_event_manager::__() {
}

void libmcc::c_game_event_manager::___() {
}

void libmcc::c_game_event_manager::____() {
}

void libmcc::c_game_event_manager::_____() {
}

void libmcc::c_game_event_manager::______() {
}

void libmcc::c_game_event_manager::_______() {
}

void libmcc::c_game_event_manager::________() {
}

void libmcc::c_game_event_manager::_________() {
}

void libmcc::c_game_event_manager::__________() {
}

int libmcc::c_game_event_manager::___________() {
	return 0;
}

void libmcc::c_game_event_manager::SetBoolTrue() {
}

void libmcc::c_game_event_manager::SetBoolFalse() {
}

bool libmcc::c_game_event_manager::GetBool() {
	return false;
}

void libmcc::c_game_event_manager::SetBool(bool, bool) {
}

void libmcc::c_game_event_manager::MetaGameUpdateGameTicks(float game_ticks) {

}

void libmcc::c_game_event_manager::GameResultUpdatePlayerTime(int player, float time) {

}

void libmcc::c_game_event_manager::_____________(bool) {
}

bool libmcc::c_game_event_manager::MetaGameGetFlags() {
	return false;
}

// In odst there are some cases that the code will read the value from the pointer,
// so we need to return a valid pointer to prevent crahing.
GUID* libmcc::c_game_event_manager::GetGUID() {
	return &m_guid;
}

void libmcc::c_game_event_manager::______________(bool*) {
}

void libmcc::c_game_event_manager::_______________() {
}

void libmcc::c_game_event_manager::________________() {
}
