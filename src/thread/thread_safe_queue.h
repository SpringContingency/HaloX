#pragma once

#include "../common.h"
#include <Windows.h>
#include <functional>

template<typename T, size_t S = 256>
class __declspec(align(16)) c_fixed_thread_safe_queue {
public:
	c_fixed_thread_safe_queue() {
		InitializeSListHead(&m_data_header);
		InitializeSListHead(&m_pool_header);

		for (int i = 0; i < S; ++i) {
			InterlockedPushEntrySList(&m_pool_header, m_items + i);
		}
	}

	static c_fixed_thread_safe_queue* create() {
		return new(_aligned_malloc(sizeof(c_fixed_thread_safe_queue), MEMORY_ALLOCATION_ALIGNMENT)) c_fixed_thread_safe_queue();
	}

	void free() {
		_aligned_free(this);
	}

	bool push(const T * value) {
		auto item = reinterpret_cast<s_item*>(InterlockedPopEntrySList(&m_pool_header));

		if (item == nullptr) {
			return false;
		}

		memcpy(&item->value, value, sizeof(T));

		InterlockedPushEntrySList(&m_data_header, item);

		return true;
	}

	void flush(void(*callback)(const T*)) {
		auto item = InterlockedFlushSList(&m_data_header);

		// flip linked list
		SLIST_ENTRY* tail = nullptr;

		while (item) {
			auto next = item->Next;
			item->Next = tail;
			tail = item;
			item = next;
		}

		item = tail;

		while (item) {
			auto next = item->Next;
			if (callback) {
				callback(&reinterpret_cast<s_item*>(item)->value);
			}
			InterlockedPushEntrySList(&m_pool_header, item);
			item = next;
		}
	}

private:
	struct s_item : SLIST_ENTRY {
		T value;
	};

	SLIST_HEADER m_data_header;
	SLIST_HEADER m_pool_header;
	s_item m_items[S];
};
