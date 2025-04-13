#include "game_manager.h"

using namespace libmcc;

uint32_t __fastcall c_game_manager::network_sendto_unreliable(network_id id, const char* buf, uint32_t len, uint32_t port) {
	return false;
}

uint32_t __fastcall c_game_manager::network_sendto_reliable(network_id id, const char* buf, uint32_t len, uint32_t port) {
	return false;
}

uint32_t __fastcall c_game_manager::network_recvfrom(char* buf, uint32_t len, network_id* id, uint32_t* port) {
	return false;
}

void __fastcall c_game_manager::network_send(const char* buf) {
}
