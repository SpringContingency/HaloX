#pragma once

#include <libmcc/libmcc.h>

namespace libmcc::halo3 {
	struct s_tag_cache {
		s_tag_cache() {
			header = &g_cache_file_globals()->header;

			tag_cache_minimum_address = reinterpret_cast<int*>(
				physical_memory_globals()->allocation_base_address -
				k_tag_cache_minimum_address
				);
		}

		template<typename T>
		std::initializer_list<T> get_tag_block(c_typed_tag_block<T> block) {
			auto block_base_address = reinterpret_cast<T*>(tag_cache_minimum_address + block.address);
			return std::initializer_list<T>(block_base_address, block_base_address + block.count);
		}

		template<typename T>
		T* get_tag_reference(c_typed_tag_reference<T> reference) {
			if (reference.index == -1) {
				return nullptr;
			}

			auto element = header->tags_header->tag_instances.m_elements + reference.index;

			if (element->address == 0) {
				return nullptr;
			}

			return reinterpret_cast<T*>(tag_cache_minimum_address + element->address);
		}

		s_cache_file_header* header;
		int* tag_cache_minimum_address;
	};
}
