#include "game_event_manager.h"

using namespace libmcc;

static bool initialized = false;

static libmcc::i_game_event_manager_vftable __vftable;

static void* function() {
	//auto stack_walker = i_stack_walker::create();

	//if (stack_walker == nullptr) {
	//	return nullptr;
	//}

	//do {
	//	printf("%s\n", stack_walker->get_name());

	//} while (stack_walker->next());

	//stack_walker->free();

	return nullptr;
}

c_game_event_manager::c_game_event_manager() {
	if (!initialized) {
		for (int i = 0; i < sizeof(__vftable) / sizeof(void*); ++i) {
			__vftable.__data[i] = function;
		}
		initialized = true;
	}

	*reinterpret_cast<libmcc::i_game_event_manager_vftable**>(this) = &__vftable;
}

bool c_game_event_manager::AchievementEarned() {
	return false;
}
