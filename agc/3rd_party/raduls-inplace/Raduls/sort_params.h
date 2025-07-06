#pragma once
#include "defs.h"
namespace raduls
{
	struct SortParams
	{
		uint8_t* input, *tmp;
		uint64_t n_recs;
		uint32_t key_size, rec_size, n_threads, last_byte_pos;
	};
}
