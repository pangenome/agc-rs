#include "raduls.h"

#include <vector>
#include <thread>
#include "sort_params.h"
#include "instr_set_dispatcher.h"

namespace raduls
{
	void CleanTmpArray(uint8_t* tmp, uint64_t n_recs, uint32_t rec_size, uint32_t n_threads)
	{
		std::vector<std::thread> cleaning_threads;
		auto n_bytes = rec_size * n_recs;
		auto part_size = n_bytes / n_threads;
		for (uint32_t th_id = 0; th_id < n_threads; ++th_id)
		{
			cleaning_threads.emplace_back([th_id, n_bytes, part_size, tmp]
			{
				auto start = th_id * part_size;
				auto end = start + part_size;
				if (end > n_bytes)
					end = n_bytes;
				for (uint64_t i = start; i < end; i += 4096)
					tmp[i] = 0;
			});
		}
		for (auto& t : cleaning_threads)
			t.join();
	}

	void RadixSortMSD(uint8_t* input, uint8_t* tmp, uint64_t n_recs, uint32_t rec_size, uint32_t key_size, uint32_t n_threads)
	{
		uint32_t n_phases = key_size; //default number of phases equals number of bytes in a keys
		PartialRadixSortMSD(input, tmp, n_recs, rec_size, key_size, n_phases, n_threads);
	}

	void PartialRadixSortMSD(uint8_t* input, uint8_t* tmp, uint64_t n_recs, uint32_t rec_size, uint32_t key_size, uint32_t n_phases, uint32_t n_threads)
	{
		//asserts
		if (reinterpret_cast<std::uintptr_t>(input) % ALIGNMENT)
			throw exceptions::InputNotAlignedException(ALIGNMENT);
		if (reinterpret_cast<std::uintptr_t>(tmp) % ALIGNMENT)
			throw exceptions::TempNotAlignedException(ALIGNMENT);
		if (rec_size % 8)
			throw exceptions::RecSizeNotMultipleOf8Exception();
		if (key_size > rec_size)
			throw exceptions::KeySizeGreaterThanRecSizeException();
		if (rec_size > MAX_REC_SIZE_IN_BYTES)
			throw exceptions::UsupportedRecSizeException();		
		if(n_phases > key_size)
			throw exceptions::TooManyPhases();

		if (!n_phases) //not even one byte to sort? maybe should throw an exception in this case...
			return;

		//let's go
		SortParams p;
		p.input = input;
		p.tmp = tmp;
		p.n_recs = n_recs;
		p.rec_size = rec_size;
		p.key_size = key_size;
		p.n_threads = n_threads;		
		p.last_byte_pos = key_size - n_phases;

		CInstrSetDispatcher::GetInst().Run(p);
	}
}