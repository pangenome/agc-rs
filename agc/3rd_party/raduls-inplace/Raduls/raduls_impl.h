#pragma once

#include <iostream> //TODO: remove

#include "instr_set_dispatcher.h"
#include "raduls.h"
#include "record.h"
#include "small_sort.h"
#include "defs.h"
#include "sort_params.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <queue>
#include <mutex>
#include <future>
#include <thread>
#include <cstring>
#include <condition_variable>
#include <memory>
#include <cassert>

#include "defs.h"
#include "vector_ext.h"

namespace raduls
{
	namespace NAMESPACE_NAME
	{
	//config	
//	constexpr int32_t BUFFER_WIDTHS[] = { -1, 32, 16, 16, 8, 8, 4, 8, 4 };
//	constexpr int32_t BUFFER_WIDTHS[] = { -1, 64, 16, 16, 8, 8, 4, 8, 4 };
	constexpr int32_t BUFFER_WIDTHS[] = { -1, 64, 64, 16, 8, 8, 4, 8, 4 }; //TODO: UWAGA: wazne te rozmiary buforow trzeba jeszcze dostosowac bo dla wiekszych rozmiarow rekordow nie bedzie dzialalo, na razie zapewnie dla 16B, chociaz nie wiem czy to nie jest juz za duzy bufor
	constexpr uint64_t small_sort_thresholds[] = { 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384, 384 };
	constexpr uint64_t wide_small_sort_thresholds[] = { 64, 48, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 };

	const uint32_t FIRST_PASS_THREADS_MULTIPLIER = 8;
	const uint64_t SMALL_RADIX_THRESHOLD = 256;

	//static_assert(MAX_REC_SIZE_IN_BYTES / 8 <= (sizeof(BUFFER_WIDTHS) / sizeof(BUFFER_WIDTHS[0]) - 1), "BUFFER_WIDTHS must be extended");
	static_assert(MAX_REC_SIZE_IN_BYTES % 8 == 0, "MAX_REC SIZE MUST BE A MULTIPLE OF 8");

	constexpr int32_t BUFFER_WIDTHS_ABOVE_CACHE_LINE_SIZE[] = { -1, 8, 4, 8, 2, 8, 4, 8, 1 };
	constexpr uint32_t GetBufferWidth(uint32_t index)
	{
		return (index <= 8) ? BUFFER_WIDTHS[index] : BUFFER_WIDTHS_ABOVE_CACHE_LINE_SIZE[(index - 1) % 8 + 1];
	}

	constexpr uint64_t GetSmallSortThreshold(uint32_t index)
	{
		return (index < sizeof(small_sort_thresholds) / sizeof(small_sort_thresholds[0])) ? small_sort_thresholds[index] : 384;
	}

	constexpr uint64_t GetWideSmallSortThreshold(uint32_t index)
	{
		return (index < sizeof(wide_small_sort_thresholds) / sizeof(wide_small_sort_thresholds[0])) ? wide_small_sort_thresholds[index] : 32;
	}


#ifdef __GNUG__
#if __GNUC__ < 6
	static_assert(false, "gcc 6 or higher is required.");
#endif
#endif
#ifdef _MSC_VER
#if _MSC_VER < 1900
	static_assert(false, "Visual Studio 2015 or higher is required.");
#endif
#endif

	inline bool check_narrowing(uint64_t x, uint64_t y) { return x < 16 * y; }
	
	class CRangeQueue
	{
		std::vector<std::tuple<uint64_t, uint64_t, uint32_t>> range_queue;
		std::mutex mtx;
		uint32_t cur_idx;
		bool done;
	public:
		CRangeQueue(uint32_t parts, uint64_t num_rec)
		{
			uint64_t delta = num_rec / parts;
			uint64_t N1 = 0, N2 = 0;
			uint64_t smallest_fraction = 8;
			uint64_t start_size = delta / smallest_fraction;
			uint64_t step = (2 * smallest_fraction - 2) * delta / smallest_fraction / parts;
			uint64_t cur_delta = start_size;

			for (uint32_t i = 0; i < parts; ++i)
			{
				N2 = N1 + cur_delta;
				cur_delta += step;
				if (i == parts - 1)
					N2 = num_rec;
				range_queue.emplace_back(N1, N2, parts - i - 1);
				N1 = N2;
			}
			std::reverse(range_queue.begin(), range_queue.end());

			cur_idx = 0;
			if (parts)
				done = false;
		}

		bool get(uint64_t& n1, uint64_t& n2, uint32_t& part_id)
		{
			std::lock_guard<std::mutex> lck(mtx);

			if (!done)
			{
				std::tie(n1, n2, part_id) = range_queue[cur_idx++];
				if (cur_idx == range_queue.size())
					done = true;
				return true;
			}
			return false;
		}

		void reset_indices()
		{
			cur_idx = 0;
			done = false;
		}
	};

	template<typename RECORD_T>
	struct CRadixMSDTaskskDesc
	{
		RECORD_T* data, *tmp;
		uint64_t n_recs;
		uint32_t byte;
		uint32_t last_byte_pos;
		bool is_narrow;

		CRadixMSDTaskskDesc(RECORD_T* data, RECORD_T* tmp, uint64_t n_recs, uint32_t byte, uint32_t last_byte_pos, bool is_narrow) :
			data(data),
			tmp(tmp),
			n_recs(n_recs),
			byte(byte),
			last_byte_pos(last_byte_pos),
			is_narrow(is_narrow)
		{ }

		bool operator<(const CRadixMSDTaskskDesc<RECORD_T>& rhs) const
		{
			return this->n_recs < rhs.n_recs;
		}
	};

	template<typename RECORD_T>
	class CRadixMSDTaskQueue
	{
		std::priority_queue<CRadixMSDTaskskDesc<RECORD_T>> tasks;
		std::condition_variable cv_pop;
		std::mutex mtx;
		uint64_t tasks_in_progress = 0;

	public:
		void push(RECORD_T* data, RECORD_T* tmp, uint64_t n_recs, uint32_t byte, uint32_t last_byte_pos, bool is_narrow)
		{
			std::lock_guard<std::mutex> lck(mtx);
			tasks_in_progress++;
			tasks.emplace(data, tmp, n_recs, byte, last_byte_pos, is_narrow);
			if (tasks.size() == 1) //was empty
				cv_pop.notify_all();
		}

		bool pop(RECORD_T*& data, RECORD_T*& tmp, uint64_t& n_recs, uint32_t& byte, uint32_t& last_byte_pos, bool& is_narrow)
		{
			std::unique_lock<std::mutex> lck(mtx);
			cv_pop.wait(lck, [this] {return tasks.size() || !tasks_in_progress; });
			if (!tasks_in_progress)
				return false;

			data = tasks.top().data;
			tmp = tasks.top().tmp;
			n_recs = tasks.top().n_recs;
			byte = tasks.top().byte;
			last_byte_pos = tasks.top().last_byte_pos;
			is_narrow = tasks.top().is_narrow;
			tasks.pop();
			return true;
		}

		void notify_task_finished()
		{
			std::lock_guard<std::mutex> lck(mtx);
			--tasks_in_progress;
			if (!tasks_in_progress)
				cv_pop.notify_all();
		}
	};

	// 64b copy function
	// size - in 8B words (determined during execution)
	// dest and src must be aligned to 8B
	inline void IntrCopy64fun(void* _dest, void* _src, uint32_t size)
	{
#if defined(ARCH_X64)
		__int64* dest = (__int64*)_dest;
		__int64* src = (__int64*)_src;

		for (unsigned i = 0; i < size; ++i)
			_mm_stream_si64(dest + i, src[i]);
#elif defined(ARCH_ARM)
		int64_t* dest = (int64_t*)_dest;
		int64_t* src = (int64_t*)_src;

		for (unsigned i = 0; i < size; ++i)
			vst1_s64(dest + i, vdup_n_s64(src[i]));
#endif
	}

	// 64bit copy function
	// SIZE - in 8B words
	template <unsigned SIZE> struct IntrCopy64
	{
		static inline void Copy(void* _dest, void* _src)
		{
#if defined(ARCH_X64)
			__int64* dest = (__int64*)_dest;
			__int64* src = (__int64*)_src;

			for (unsigned i = 0; i < SIZE; ++i)
				_mm_stream_si64(dest + i, src[i]);
#elif defined(ARCH_ARM)
			int64_t* dest = (int64_t*)_dest;
			int64_t* src = (int64_t*)_src;

			for (unsigned i = 0; i < SIZE; ++i)
				vst1_s64(dest + i, vdup_n_s64(src[i]));
#endif
		}
	};


	template <unsigned SIZE, unsigned MODE> struct IntrCopy128
	{

	};

	// 128bit copy function
	// SIZE - in 16B words
	// dest - aligned to 16B
	// src  - aligned to 16B
	template <unsigned SIZE> struct IntrCopy128<SIZE, 1>
	{
		static inline void Copy(void* _dest, void* _src)
		{
#if defined(ARCH_X64)
			__m128i* dest = (__m128i*) _dest;
			__m128i* src = (__m128i*) _src;

			for (unsigned i = 0; i < SIZE; ++i)
				_mm_stream_si128(dest + i, _mm_load_si128(src + i));
#elif defined(ARCH_ARM)
			int64x2_t* dest = (int64x2_t*) _dest;
			int64x2_t* src = (int64x2_t*) _src;

			for (unsigned i = 0; i < SIZE; ++i)
#if __has_builtin(__builtin_nontemporal_store)
				__builtin_nontemporal_store(*src, dest);
#else
				vst1q_s64((int64_t*)(dest+i), src[i]);
#endif
#endif
		}
	};

	// 128bit copy function
	// SIZE - in 16B words
	// dest - aligned to 8B
	// src  - aligned to 16B
	template <unsigned SIZE> struct IntrCopy128<SIZE, 0>
	{
		static inline void Copy(void* dest, void* src)
		{
			if ((uint64_t)dest % 16)	// if only 8B aligned use 64b copy
				IntrCopy64<SIZE * 2>::Copy(dest, src);
			else // if 16B aligned use 128b copy
				IntrCopy128<SIZE, 1>::Copy(dest, src);
		}
	};

	template<typename RECORD_T, typename COUNTER_TYPE>
	FORCE_INLINE void BuildHisto(COUNTER_TYPE* histo, uint64_t n, uint8_t*& ptr)
	{
		switch (n % 4)
		{
		case 3:
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		case 2:
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		case 1:
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		}

		auto n_iters = n / 4;
		for (uint64_t i = 0; i < n_iters; ++i)
		{
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		}
	}

	template<typename RECORD_T, typename COUNTER_TYPE>
	FORCE_INLINE void SimpleScatter(RECORD_T* src, RECORD_T* tmp, COUNTER_TYPE* histo, uint64_t n, uint8_t*& ptr)
	{
		switch (n % 4)
		{
		case 3:
			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		case 2:
			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		case 1:
			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		}

		for (uint64_t i = n % 4; i < n; i += 4)
		{
			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);

			tmp[histo[*ptr]] = *src++;
			histo[*ptr]++;
			ptr += sizeof(RECORD_T);
		}
	}

	template<typename RECORD_T, typename COUNTER_TYPE,
		uint32_t BUFFER_WIDTH, uint32_t BUFFER_WIDTH_IN_128BIT_WORDS>
		FORCE_INLINE void BufferedScatterStep(RECORD_T*& src, RECORD_T* tmp,
			COUNTER_TYPE* histo, COUNTER_TYPE* copy_histo, uint8_t*& ptr, uint8_t& byteValue,
			RECORD_T* buffer, int& index_x, uint8_t* first_store)
	{
		byteValue = *ptr;
		index_x = histo[byteValue] % BUFFER_WIDTH;
		buffer[byteValue * BUFFER_WIDTH + index_x] = *src++;
		histo[byteValue]++;
		if (index_x == (BUFFER_WIDTH - 1))
		{
			if (first_store[byteValue])
			{
				first_store[byteValue] = false;
				int64_t offset = copy_histo[byteValue] % BUFFER_WIDTH;
				IntrCopy64fun(&tmp[histo[byteValue] - BUFFER_WIDTH + offset], &buffer[byteValue * BUFFER_WIDTH + offset], RECORD_T::RECORD_SIZE * (BUFFER_WIDTH - offset));
			}
			else
				IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, 1>::Copy(&tmp[histo[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
		}
		ptr += sizeof(RECORD_T);
	}

	template<typename RECORD_T, typename COUNTER_TYPE, uint32_t BUFFER_WIDTH>
	FORCE_INLINE void BufferedScattterCorrectionStep(RECORD_T* tmp, COUNTER_TYPE* histo, COUNTER_TYPE* copy_histo, RECORD_T* buffer)
	{
		int64_t elemInBuffer, index_stop, index_start, elemWrittenIntoBuffer;
		for (uint32_t i = 0; i < 256; ++i)
		{
			index_stop = histo[i] % BUFFER_WIDTH;
			index_start = copy_histo[i] % BUFFER_WIDTH;
			elemWrittenIntoBuffer = histo[i] - copy_histo[i];

			if ((index_stop - elemWrittenIntoBuffer) <= 0)
				elemInBuffer = index_stop;
			else
				elemInBuffer = index_stop - index_start;

			if (elemInBuffer != 0)
				IntrCopy64fun(&tmp[histo[i] - elemInBuffer],
					&buffer[i * BUFFER_WIDTH + (histo[i] - elemInBuffer) % BUFFER_WIDTH], elemInBuffer * sizeof(RECORD_T) / 8);
		}
	}

	template<typename RECORD_T, typename COUNTER_TYPE>
	void FirstPassStage1(RECORD_T* data, std::vector<COUNTER_TYPE[256]>& histos, uint32_t byte, CRangeQueue& rq)
	{
		alignas(ALIGNMENT)COUNTER_TYPE myHisto[256] = {};
		uint64_t idx1, idx2;
		uint32_t part_id;

		while (rq.get(idx1, idx2, part_id))
		{
			std::memset(myHisto, 0, sizeof(myHisto));

			auto ptr = reinterpret_cast<uint8_t*>(data + idx1) + byte;
			uint64_t n = idx2 - idx1;

			BuildHisto<RECORD_T, COUNTER_TYPE>(myHisto, n, ptr);

			for (uint32_t i = 0; i < 256; ++i)
				histos[part_id][i] = myHisto[i];
		}
	}
	template<typename RECORD_T, typename COUNTER_TYPE>
	void BigBinsScatter(RECORD_T* data, RECORD_T* tmp,
		uint32_t byte, std::vector<COUNTER_TYPE[256]>& histos,
		std::vector<uint8_t*>& buffers, std::vector<COUNTER_TYPE[256]>& threads_histos,
		CRangeQueue& rq)
	{
		alignas(ALIGNMENT)COUNTER_TYPE myHisto[256];

		uint64_t idx1, idx2;
		uint32_t part_id;

		uint8_t* ptr;
		uint64_t n;
		RECORD_T* src;

		//constexpr uint32_t BUFFER_WIDTH = BUFFER_WIDTHS[sizeof(RECORD_T) / 8];
		constexpr uint32_t BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);
		constexpr uint32_t BUFFER_WIDTH_IN_128BIT_WORDS = BUFFER_WIDTH * sizeof(RECORD_T) / 16;

		uint8_t byteValue = 0;
		int index_x = 0;

		while (rq.get(idx1, idx2, part_id))
		{
			for (int i = 0; i < 256; ++i)
				myHisto[i] = histos[part_id][i];

			auto copy_histo = histos[part_id];

			ptr = reinterpret_cast<uint8_t*>(data + idx1) + byte;
			n = idx2 - idx1;

			auto buffer = reinterpret_cast<RECORD_T*>(buffers[part_id]);

			src = data + idx1;
			byteValue = 0;
			index_x = 0;

			uint8_t first_store[256];
			std::fill_n(first_store, 256, true);

			switch (n % 4)
			{
			case 3:
				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);
			case 2:
				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);
			case 1:
				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);
			}

			for (uint64_t i = n % 4; i < n; i += 4)
			{
				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);

				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);

				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);

				BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
					(src, tmp, myHisto, copy_histo, ptr, byteValue, buffer, index_x, first_store);
			}

			for (uint32_t i = 0; i < 256; ++i)
				threads_histos[part_id][i] = myHisto[i];
		}
	}

	template<typename RECORD_T, typename COUNTER_TYPE>
	void FirstPassStage2(RECORD_T* data, RECORD_T* tmp,
		uint32_t byte, std::vector<COUNTER_TYPE[256]>& histos,
		std::vector<uint8_t*>& buffers, std::vector<COUNTER_TYPE[256]>& threads_histos,
		CRangeQueue& rq)
	{
		alignas(ALIGNMENT)COUNTER_TYPE myHisto[256];

		uint64_t idx1, idx2;
		uint32_t part_id;

		uint8_t* ptr;
		uint64_t n;
		RECORD_T* src;

		//constexpr uint32_t BUFFER_WIDTH = BUFFER_WIDTHS[sizeof(RECORD_T) / 8];
		constexpr uint32_t BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);
		constexpr uint32_t BUFFER_WIDTH_IN_128BIT_WORDS = BUFFER_WIDTH * sizeof(RECORD_T) / 16;
		constexpr uint32_t BUFFER_16B_ALIGNED = 1;

		uint8_t byteValue = 0;
		int index_x = 0;

		while (rq.get(idx1, idx2, part_id))
		{
			for (int i = 0; i < 256; ++i)
				myHisto[i] = histos[part_id][i];

			ptr = reinterpret_cast<uint8_t*>(data + idx1) + byte;
			n = idx2 - idx1;

			auto buffer = reinterpret_cast<RECORD_T*>(buffers[part_id]);

			src = data + idx1;
			byteValue = 0;
			index_x = 0;

			switch (n % 4)
			{
			case 3:
				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[(n % 4) - 3];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);
			case 2:
				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[(n % 4) - 2];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);
			case 1:
				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[(n % 4) - 1];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);
			}

			for (uint64_t i = n % 4; i < n; i += 4)
			{
				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[i];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);

				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[i + 1];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);

				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[i + 2];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);

				byteValue = *ptr;
				index_x = myHisto[byteValue] % BUFFER_WIDTH;
				buffer[byteValue * BUFFER_WIDTH + index_x] = src[i + 3];
				myHisto[byteValue]++;
				if (index_x == (BUFFER_WIDTH - 1))
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, BUFFER_16B_ALIGNED>::Copy(&tmp[myHisto[byteValue] - BUFFER_WIDTH], &buffer[byteValue * BUFFER_WIDTH]);
				ptr += sizeof(RECORD_T);
			}

			for (uint32_t i = 0; i < 256; ++i)
				threads_histos[part_id][i] = myHisto[i];
		}
	}

	template<typename RECORD_T, typename COUNTER_TYPE>
	void FirstPassStage3(RECORD_T* tmp,
		std::vector<COUNTER_TYPE[256]>& histos,
		std::vector<uint8_t*>& buffers, std::vector<COUNTER_TYPE[256]>& threads_histos,
		CRangeQueue& rq)
	{
		uint64_t idx1, idx2;
		uint32_t part_id;
		alignas(ALIGNMENT)COUNTER_TYPE myHisto[256];

		//const uint32_t BUFFER_WIDTH = BUFFER_WIDTHS[sizeof(RECORD_T) / 8];
		const uint32_t BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);

		while (rq.get(idx1, idx2, part_id))
		{
			auto buffer = reinterpret_cast<RECORD_T*>(buffers[part_id]);

			for (int i = 0; i < 256; ++i)
				myHisto[i] = threads_histos[part_id][i];

			BufferedScattterCorrectionStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH>(tmp, myHisto, histos[part_id], buffer);
		}
	}

	template<typename RECORD_T>
	void SmallSortDispatch(RECORD_T* data, RECORD_T* tmp, uint64_t size)
	{
		small_sort::HybridSmallSort<RECORD_T>{}(data, size);
		//std::sort(data, data + size);		
	}

	template<typename RECORD_T, typename COUNTER_TYPE>
	class CRadixSorterMSD
	{
		CRadixMSDTaskQueue<RECORD_T>& tasks_queue;
		uint64_t use_queue_min_recs = 0;
		uint8_t* _buffer;
		void Sort(RECORD_T* data, RECORD_T* tmp, uint64_t n_recs, uint32_t byte, uint32_t last_byte_pos, bool is_narrow)
		{
			auto ptr = reinterpret_cast<uint8_t*>(data) + byte;
			alignas(ALIGNMENT)COUNTER_TYPE globalHisto[256] = {};
			alignas(ALIGNMENT)COUNTER_TYPE copy_globalHisto[257];
			uint64_t largest_bin_size = 0;

			BuildHisto<RECORD_T, COUNTER_TYPE>(globalHisto, n_recs, ptr);

			COUNTER_TYPE prevSum = 0;
			for (int i = 0; i < 256; ++i)
			{
				COUNTER_TYPE tmp = globalHisto[i];
				globalHisto[i] = prevSum;
				copy_globalHisto[i] = prevSum;
				prevSum += tmp;

				if (tmp > largest_bin_size)
					largest_bin_size = tmp;
			}
			copy_globalHisto[256] = static_cast<COUNTER_TYPE>(n_recs);

			auto src = data;
			ptr = reinterpret_cast<uint8_t*>(data) + byte;

			//if fits in L2 cache
			if (n_recs * sizeof(RECORD_T) < (1ull << 16))
				SimpleScatter(src, tmp, globalHisto, n_recs, ptr);
			else
			{
				//constexpr uint32_t BUFFER_WIDTH = BUFFER_WIDTHS[sizeof(RECORD_T) / 8];
				constexpr uint32_t BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);
				constexpr uint32_t BUFFER_WIDTH_IN_128BIT_WORDS = BUFFER_WIDTH * sizeof(RECORD_T) / 16;

				auto buffer = reinterpret_cast<RECORD_T*>(_buffer);

				uint8_t byteValue = 0;
				int index_x = 0;

				uint8_t first_store[256];
				std::fill_n(first_store, 256, true);

				// Move back tmp pointer - to be aligned to 64B
				uint64_t tmp_moved_by = 0;
				while ((uint64_t)tmp % 64)
					tmp--, tmp_moved_by++;

				// Update histograms - they must point correct places after tmp alignment
				for (int i = 0; i < 256; ++i)
				{
					globalHisto[i] += tmp_moved_by;
					copy_globalHisto[i] += tmp_moved_by;
				}
				copy_globalHisto[256] += tmp_moved_by;

				switch (n_recs % 4)
				{
				case 3:
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
				case 2:
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
				case 1:
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
				}

				for (uint64_t i = n_recs % 4; i < n_recs; i += 4)
				{
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
					BufferedScatterStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH, BUFFER_WIDTH_IN_128BIT_WORDS>
						(src, tmp, globalHisto, copy_globalHisto, ptr, byteValue, buffer, index_x, first_store);
				}

				BufferedScattterCorrectionStep<RECORD_T, COUNTER_TYPE, BUFFER_WIDTH>(tmp, globalHisto, copy_globalHisto, buffer);

				// Bring back tmp pointer to the original (unaligned) value
				tmp += tmp_moved_by;
				for (int i = 0; i < 256; ++i)
				{
					globalHisto[i] -= tmp_moved_by;
					copy_globalHisto[i] -= tmp_moved_by;
				}
				copy_globalHisto[256] -= tmp_moved_by;
			}

			if (byte > last_byte_pos)
			{
				bool must_copy_tmp = (byte - last_byte_pos) % 2;

				//uint64_t narrow_small_sort_threshold = small_sort_thresholds[RECORD_T::RECORD_SIZE];
				uint64_t narrow_small_sort_threshold = GetSmallSortThreshold(RECORD_T::RECORD_SIZE);
				//uint64_t wide_small_sort_threshold = wide_small_sort_thresholds[RECORD_T::RECORD_SIZE];
				uint64_t wide_small_sort_threshold = GetWideSmallSortThreshold(RECORD_T::RECORD_SIZE);

				if (largest_bin_size <= wide_small_sort_threshold || (is_narrow && largest_bin_size <= narrow_small_sort_threshold))
				{
					// All bins are small
					for (int i = 0; i < 256; ++i)
					{
						uint64_t new_n = copy_globalHisto[i + 1] - copy_globalHisto[i];
						if (new_n > 1)
							SmallSortDispatch<RECORD_T>(tmp + copy_globalHisto[i], data + copy_globalHisto[i], new_n);
					}

					IntrCopy64fun(data, tmp, n_recs * sizeof(RECORD_T) / 8);
				}
				else
					for (int i = 0; i < 256; ++i)
					{
						uint64_t new_n = copy_globalHisto[i + 1] - copy_globalHisto[i];

						if (new_n <= wide_small_sort_threshold || (is_narrow && new_n <= narrow_small_sort_threshold))
						{
							if (new_n > 1)
								SmallSortDispatch(tmp + copy_globalHisto[i], data + copy_globalHisto[i], new_n);
							if (must_copy_tmp && new_n)
								for (COUNTER_TYPE j = copy_globalHisto[i]; j < copy_globalHisto[i] + static_cast<COUNTER_TYPE>(new_n); ++j)
									data[j] = tmp[j];
						}
						else
						{
							if (new_n >= use_queue_min_recs)
								tasks_queue.push(tmp + copy_globalHisto[i], data + copy_globalHisto[i], new_n, byte - 1, last_byte_pos, is_narrow);
							else
								if (new_n < SMALL_RADIX_THRESHOLD)
									SmallRadixSort(tmp + copy_globalHisto[i], data + copy_globalHisto[i], new_n, byte - 1, last_byte_pos);
								else
									Sort(tmp + copy_globalHisto[i], data + copy_globalHisto[i], new_n, byte - 1, last_byte_pos, check_narrowing(n_recs, new_n));
						}
					}
			}

			our_fence();
		}

		void SmallRadixSort(RECORD_T* data, RECORD_T* tmp, uint64_t n_recs, uint32_t byte, uint32_t last_byte_pos)
		{
			auto ptr = reinterpret_cast<uint8_t*>(data) + byte;
			alignas(ALIGNMENT)uint32_t globalHisto[256] = {};
			alignas(ALIGNMENT)uint32_t copy_globalHisto[257];
			int to_sort[256];		// bins to sort (at least 2 elements in bin)
			int idx_to_sort = 0;
			bool must_copy_tmp = (byte - last_byte_pos) % 2;

			BuildHisto<RECORD_T, uint32_t>(globalHisto, n_recs, ptr);

			uint32_t prevSum = 0;
			for (int i = 0; i < 256; ++i)
			{
				uint32_t n_elems = globalHisto[i];
				globalHisto[i] = prevSum;
				copy_globalHisto[i] = prevSum;
				prevSum += n_elems;

				to_sort[idx_to_sort] = i;
				idx_to_sort += n_elems > 1;
			}
			copy_globalHisto[256] = static_cast<COUNTER_TYPE>(n_recs);

			auto src = data;
			ptr = reinterpret_cast<uint8_t*>(data) + byte;

			SimpleScatter(src, tmp, globalHisto, n_recs, ptr);

			if (byte > last_byte_pos)
			{
				for (int ii = 0; ii < idx_to_sort; ++ii)
				{
					int i = to_sort[ii];
					uint64_t new_n = copy_globalHisto[i + 1] - copy_globalHisto[i];

					SmallSortDispatch(tmp + copy_globalHisto[i], tmp + copy_globalHisto[i], new_n);
				}
				if (must_copy_tmp)
					IntrCopy64fun(data, tmp, n_recs * sizeof(RECORD_T) / 8);
			}

			our_fence();
		}

	public:
		CRadixSorterMSD(CRadixMSDTaskQueue<RECORD_T>& tasks_queue, uint64_t use_queue_min_recs, uint8_t* _buffer)
			:
			tasks_queue(tasks_queue),
			use_queue_min_recs(use_queue_min_recs),
			_buffer(_buffer)
		{}

		void operator()()
		{
			RECORD_T* data, *tmp;
			uint64_t n_recs;
			uint32_t byte;
			uint32_t last_byte_pos;
			bool is_narrow;

			while (tasks_queue.pop(data, tmp, n_recs, byte, last_byte_pos, is_narrow))
			{
				Sort(data, tmp, n_recs, byte, last_byte_pos, is_narrow);
				tasks_queue.notify_task_finished();
			}
		}
	};

	template<typename RECORD_T>
	class CRaduls
	{
		uint32_t n_threads;

		void join_threads(std::vector<std::thread>& threads)
		{
			for (auto& th : threads)
				th.join();
			threads.clear();
		}

		void join_futures(std::vector<std::future<void>>& futures)
		{
			for (auto& fut : futures)
				fut.get();
			futures.clear();
		}

	public:
		CRaduls(uint32_t n_threads) :
			n_threads(n_threads)
		{
		}

		template<typename COUNTER_TYPE>
		void Sort(RECORD_T* data, RECORD_T* tmp, uint64_t n_recs, uint32_t byte, uint32_t last_byte_pos,
			uint32_t n_threads, bool is_first_level, uint64_t is_big_threshold, uint64_t n_total_recs)
		{
			//uint64_t current_small_sort_threshold = small_sort_thresholds[RECORD_T::RECORD_SIZE];
			uint64_t current_small_sort_threshold = GetSmallSortThreshold(RECORD_T::RECORD_SIZE);

			if (n_recs <= current_small_sort_threshold)
			{
				SmallSortDispatch(data, tmp, n_recs);
				if ((byte - last_byte_pos) % 2 == 0)
					for (uint64_t j = 0; j < n_recs; ++j)
						tmp[j] = data[j];
				return;
			}

			//stage 1
			const auto n_parts = FIRST_PASS_THREADS_MULTIPLIER * n_threads;
			CRangeQueue range_queue(n_parts, n_recs);
			std::vector<COUNTER_TYPE[256]> histos(n_parts);
			alignas(ALIGNMENT)COUNTER_TYPE globalHisto[257] = {};

			std::vector<std::future<void>> futures;

			for (uint32_t th_id = 0; th_id < n_threads; ++th_id)
				futures.emplace_back(std::async(FirstPassStage1<RECORD_T, COUNTER_TYPE>,
					data, std::ref(histos), byte, std::ref(range_queue)));

			join_futures(futures);

			// ***** collecting counters
			for (int i = 0; i < 256; ++i)
			{
				COUNTER_TYPE prevSum = 0;
				for (uint32_t n = 0; n < n_parts; ++n)
				{
					auto tmp = histos[n][i];
					histos[n][i] = prevSum;
					prevSum += tmp;
				}
				globalHisto[i] = prevSum;
			}

			COUNTER_TYPE prevSum = 0;
			for (int i = 0; i < 256; ++i)
			{
				COUNTER_TYPE tmp = globalHisto[i];
				globalHisto[i] = prevSum;
				prevSum += tmp;
			}

			for (uint32_t n = 0; n < n_parts; ++n)
				for (int i = 0; i < 256; ++i)
					histos[n][i] += globalHisto[i];

			//stage 2
			range_queue.reset_indices();

			//uint64_t single_part_size = 256 * BUFFER_WIDTHS[sizeof(RECORD_T) / 8] * sizeof(RECORD_T);
			uint64_t single_part_size = 256 * GetBufferWidth(sizeof(RECORD_T) / 8) * sizeof(RECORD_T);
			auto _raw_buffers = std::make_unique<uint8_t[]>(single_part_size * n_parts + ALIGNMENT);
			auto s = _raw_buffers.get();
			while ((uint64_t)s % ALIGNMENT)
				++s;
			std::vector<uint8_t*> buffers(n_parts);
			for (uint32_t i = 0; i < n_parts; ++i)
				buffers[i] = s + single_part_size * i;

			std::vector<COUNTER_TYPE[256]> threads_histos(n_parts);

			uint64_t tmp_moved_by = 0;
			if (!is_first_level)
			{
				while ((uint64_t)tmp % 64)
					tmp--, tmp_moved_by++;

				for (uint32_t n = 0; n < n_parts; ++n)
					for (int i = 0; i < 256; ++i)
						histos[n][i] += tmp_moved_by;
			}

			auto fun = is_first_level ? FirstPassStage2<RECORD_T, COUNTER_TYPE> : BigBinsScatter<RECORD_T, COUNTER_TYPE>;

			for (uint32_t th_id = 0; th_id < n_threads; ++th_id)
				futures.emplace_back(std::async(fun,
					data, tmp, byte,
					std::ref(histos), std::ref(buffers), std::ref(threads_histos),
					std::ref(range_queue)));

			join_futures(futures);

			//stage 3
			range_queue.reset_indices();
			for (uint32_t th_id = 0; th_id < n_threads; ++th_id)
				futures.emplace_back(std::async(FirstPassStage3<RECORD_T, COUNTER_TYPE>,
					tmp, std::ref(histos), std::ref(buffers), std::ref(threads_histos),
					std::ref(range_queue)));

			join_futures(futures);

			if (!is_first_level)
			{
				tmp += tmp_moved_by;
				for (uint32_t n = 0; n < n_parts; ++n)
					for (int i = 0; i < 256; ++i)
						histos[n][i] -= tmp_moved_by;
			}

			if (byte > last_byte_pos)
			{
				CRadixMSDTaskQueue<RECORD_T> tasks_queue;

				auto data_ptr = data;
				auto ptr = tmp;

				std::vector<std::tuple<RECORD_T*, RECORD_T*, uint64_t>> big_bins;
				uint64_t n_recs_in_big_bins = 0;

				globalHisto[256] = n_recs;
				for (uint32_t i = 1; i < 257; ++i)
				{
					auto n = static_cast<uint64_t>(globalHisto[i] - globalHisto[i - 1]);
					if (n > 0)
					{
						if (n > is_big_threshold)
						{
							if (!is_first_level)
								Sort<COUNTER_TYPE>(ptr, data_ptr, n, byte - 1, last_byte_pos, n_threads, false, is_big_threshold, n_total_recs);
							else
								big_bins.emplace_back(ptr, data_ptr, n),
								n_recs_in_big_bins += n;
						}
						else
							tasks_queue.push(ptr, data_ptr, n, byte - 1, last_byte_pos, check_narrowing(n_recs, n));
					}

					ptr += n;
					data_ptr += n;
				}

				std::sort(big_bins.begin(), big_bins.end(), [](const auto& x, const auto& y) {return std::get<2>(x) > std::get<2>(y); });

				auto n_threads_for_big_bins = std::min(n_threads, static_cast<uint32_t>(ceil(n_threads * n_recs_in_big_bins * 5.0 / (4 * n_total_recs))));

				uint32_t n_threads_for_small_bins_running;
				auto n_threads_for_small_bins = n_threads - n_threads_for_big_bins;

				using SORTER_T = CRadixSorterMSD<RECORD_T, COUNTER_TYPE>;
				std::vector<std::unique_ptr<SORTER_T>> sorters;
				auto use_queue_min_recs = n_recs / 4096;
				for (n_threads_for_small_bins_running = 0; n_threads_for_small_bins_running < n_threads_for_small_bins; ++n_threads_for_small_bins_running)
				{
					sorters.emplace_back(std::make_unique<SORTER_T>(tasks_queue,
						use_queue_min_recs, buffers[n_threads_for_small_bins_running]));
					futures.emplace_back(std::async(std::ref(*sorters.back().get())));
				}

				for (auto& big_bin : big_bins)
					Sort<COUNTER_TYPE>(std::get<0>(big_bin), std::get<1>(big_bin), std::get<2>(big_bin), byte - 1, last_byte_pos, n_threads_for_big_bins,
						false, is_big_threshold, n_total_recs);

				for (; n_threads_for_small_bins_running < n_threads; ++n_threads_for_small_bins_running)
				{
					sorters.emplace_back(std::make_unique<SORTER_T>(tasks_queue,
						use_queue_min_recs, buffers[n_threads_for_small_bins_running]));
					futures.emplace_back(std::async(std::ref(*sorters.back().get())));
				}

				join_futures(futures);
			}
		}
	};

	//TODO: poki co spradzam sobie po kolei wszystkie buckety, ale mylse ze daloby sie na bierzaco gdzies zliczac ile jest niepustych 
	inline bool AnyBucketNotEmpty(uint64_t* start, uint64_t* end)
	{
		for (uint32_t byte = 0; byte < 256; ++byte)
			if (end[byte] - start[byte])
				return true;
		return false;
	}

	inline uint64_t GetNumberOfElementsLeft(const std::vector<uint64_t[256]>& start, const std::vector<uint64_t[256]>& end, uint32_t n_threads)
	{
		uint64_t res{};
		for (uint32_t tid = 0; tid < n_threads; ++tid)
		{
			for (uint32_t i = 0; i < 256; ++i)
				res += end[tid][i] - start[tid][i];
		}
		return res;
	}

	template<typename RECORD_T, unsigned BUFFER_WIDTH>
	void CacheRecordToMainMemory(
		RECORD_T* data,
		uint8_t byte,
		uint8_t key,
		bool* first_store,
		uint64_t* histo,
		uint64_t* histo_begin,
		uint64_t* histo_end,
		uint64_t& read_pos,
		uint64_t& tail,
		RECORD_T* cache_buff)
	{
		constexpr uint32_t BUFFER_WIDTH_IN_128BIT_WORDS = BUFFER_WIDTH * sizeof(RECORD_T) / 16;

		if (first_store[key])
		{
			first_store[key] = false;
			int64_t offset = histo_begin[key] % BUFFER_WIDTH;

			if (key == byte)
			{
				//TODO: convert to intrcopy
				std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, BUFFER_WIDTH - offset, data + histo[key] - BUFFER_WIDTH + offset);
			}
			else
			{
				auto write_start = histo[key] - BUFFER_WIDTH + offset;
				auto recs_to_write = BUFFER_WIDTH - offset;
				if (write_start + recs_to_write > histo_end[key]) //cannot perform store
				{
					first_store[key] = true;

					auto n_recs_left_to_read_for_current_byte = tail - read_pos;
					if ((int64_t) n_recs_left_to_read_for_current_byte >= recs_to_write)
					{
						read_pos -= recs_to_write;
						tail -= recs_to_write;
						std::copy_n(data + tail, recs_to_write, data + read_pos);
						std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, recs_to_write, data + tail);
					}
					else // na koncu zakresu aktualnej cyfry nie da sie juz wpisac tego co jest w buforze cache
					{
						for (uint64_t i = read_pos; i < tail; ++i)
							data[i - recs_to_write] = data[i];
						read_pos -= recs_to_write;
						tail -= recs_to_write;
						std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, recs_to_write, data + tail);
					}
					histo[key] -= recs_to_write;
				}
				else //OK store is possible
				{
					read_pos -= BUFFER_WIDTH - offset;
					std::copy_n(data + histo[key] - BUFFER_WIDTH + offset, BUFFER_WIDTH - offset, data + read_pos); //robimy miejsce
					std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, BUFFER_WIDTH - offset, data + histo[key] - BUFFER_WIDTH + offset); //wlasciwe kopiowanie
				}
			}
		}
		else
		{
			//_mm_prefetch((const char*)(data + histo[key] - BUFFER_WIDTH), _MM_HINT_T0);
			if (key == byte)
			{
				IntrCopy128< BUFFER_WIDTH_IN_128BIT_WORDS, 1>::Copy(data + histo[key] - BUFFER_WIDTH, cache_buff + key * BUFFER_WIDTH);
			}
			else
			{
				auto write_start = histo[key] - BUFFER_WIDTH;
				auto recs_to_write = BUFFER_WIDTH;
				if (write_start + recs_to_write > histo_end[key])
				{
					auto n_recs_left_to_read_for_current_byte = tail - read_pos;
					if (n_recs_left_to_read_for_current_byte >= recs_to_write)
					{
						read_pos -= recs_to_write;
						tail -= recs_to_write;
						std::copy_n(data + tail, recs_to_write, data + read_pos);
						std::copy_n(cache_buff + BUFFER_WIDTH * key, recs_to_write, data + tail);
					}
					else
					{
						for (uint64_t i = read_pos; i < tail; ++i)
							data[i - recs_to_write] = data[i];
						read_pos -= recs_to_write;
						tail -= recs_to_write;
						std::copy_n(cache_buff + BUFFER_WIDTH * key, recs_to_write, data + tail);
					}

					histo[key] -= BUFFER_WIDTH;
				}
				else
				{
					read_pos -= BUFFER_WIDTH;

					//make a place for a buffer
					std::copy_n(data + histo[key] - BUFFER_WIDTH, BUFFER_WIDTH, data + read_pos);
					
					
					//std::copy_n(cache_buff + BUFFER_WIDTH * key, BUFFER_WIDTH, data + histo[key] - BUFFER_WIDTH);
					IntrCopy128<BUFFER_WIDTH_IN_128BIT_WORDS, 1>::Copy(data + histo[key] - BUFFER_WIDTH, cache_buff + BUFFER_WIDTH * key);
				}
			}
		}
	}

	template<typename RECORD_T, unsigned BUFFER_WIDTH>
	FORCE_INLINE void CleanCache(RECORD_T* data, uint8_t byte, bool* first_store, uint64_t* histo, uint64_t* histo_begin, RECORD_T* cache_buff)
	{
		uint64_t real_current_write_pos = 0;
		if (first_store[byte])
			real_current_write_pos = histo_begin[byte];
		else
			real_current_write_pos = histo[byte] / BUFFER_WIDTH * BUFFER_WIDTH;

		auto tmp_cp = real_current_write_pos;

		auto curByte = byte;
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			int64_t offset = 0;
			if (first_store[byte])
				offset = histo_begin[byte] % BUFFER_WIDTH;
			uint32_t here{};
			auto end = histo[byte] % BUFFER_WIDTH;
			for (uint64_t i = offset; i < end; ++i)
			{
				data[real_current_write_pos++] = (cache_buff + byte * BUFFER_WIDTH)[i];
				if (byte != curByte)
					--histo[byte]; //correct write pos
				++here;
			}
		}
		histo[byte] = tmp_cp;
	}

	template<typename RECORD_T, unsigned BUFFER_WIDTH>
	void RSCacheScatter(RECORD_T* data, uint64_t* histo, uint64_t* histo_end, uint32_t rec_pos, RECORD_T* cache_buff)
	{

		//TODO: uwaga, na razie ten algorytm dziala tak:
		//1. lecimy sobie po rekordach dla kazego bajtu
		//2. przez cache wymieniamy jest tak zeby poszly na docelowe pozycje, na tyle na ile to jest mozliwe
		//3. to co nam zostanie w cache wrzucamy spowrotem do cyfry z ktorej czytalismy
		//4. Na koncu w kazdej cyfrze moga zostac jeszcze takie dodatkowe koncowki do przetworzenia i potem trzeba je poprawiac, to sie dzieje juz poza t¹ funkcj¹
		//   Pojawia sie pytanie:czy daloby sie to zrobic w ten sposob, zeby nie bylo koniecznosci czyszczenia tego cache po kazdej z cyfr
		// Druga kwestia jest taka zeby sobie pomierzyc ile tych rekordow tam leci
		// Skutkiem tego ze zostaja w kazdej cyfrze takie ogony, ktorych sie dla tego rozmiaru cache nie udalo rozeslac jest to, ze czasem (zwlaszcza chyba w wariancie jendowatkowym)
		// warto jest odpalic jeszcze raz wersje cache z mniejszym BUFFER_WIDTH
		// ogolnie warto by bylo tez poeksperymentowac z buffer width w stosunku do rozmiaru rekordu, jak rowniez liczby rekordow, no i jeszcze oczywicie kwestia rozkladow
		// Ponadto moze daloby siê jednak zrobic w ten sposob zeby czyszczenie bufora cache nie bylo konieczne za kazdym razem, tylko to moze mocno skomplikowac kod, ale moze na ktorym etpie rozwoju
		// algorytmu warto byloby o tym pomyslec


		bool first_store[256];
		uint64_t histo_begin[256];

		uint64_t data_moved_by = MoveToAlignment(data, histo, histo_end);
		
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			std::copy(histo, histo + 256, histo_begin);
			std::fill_n(first_store, 256, true);
			auto read_pos = histo[byte];
			auto tail = histo_end[byte];
			while (read_pos < tail)
			{
				uint8_t key = *(reinterpret_cast<uint8_t*>(data + read_pos) + rec_pos);
				int index_x = histo[key] % BUFFER_WIDTH;
				cache_buff[key * BUFFER_WIDTH + index_x] = data[read_pos];
				++read_pos;
				++histo[key];
				if (index_x == BUFFER_WIDTH - 1) //cache buff is full (except first_store -> then it is possible there is additional offset without records to store)
					CacheRecordToMainMemory<RECORD_T, BUFFER_WIDTH>(data, byte, key, first_store, histo, histo_begin, histo_end, read_pos, tail, cache_buff);
			}

			our_fence();

			//store records that are still in cache buffer back to source digit (byte) range 
			CleanCache<RECORD_T, BUFFER_WIDTH>(data, byte, first_store, histo, histo_begin, cache_buff);
		}
		
		MoveBack(data, histo, histo_end, data_moved_by);
	}

	template<typename RECORD_T>
	void RS_permute_without_cache(RECORD_T* data, uint64_t* histo, uint64_t* histo_end, uint32_t rec_pos)
	{
		for (uint32_t _byte = 0; _byte < 256; ++_byte)
		{
			uint8_t byte = static_cast<uint8_t>(_byte);
			auto head = histo[byte];
			auto tail = histo_end[byte];
			while (head < tail)
			{
				auto v = data[head];
				uint8_t k = *(reinterpret_cast<uint8_t*>(&v) + rec_pos);
				while (k != byte && histo[k] < histo_end[k])
				{
					std::swap(v, data[histo[k]++]);
					k = *(reinterpret_cast<uint8_t*>(&v) + rec_pos);
				}
				if (k == byte)
				{
					data[head++] = data[histo[byte]]; //przepisz smiecia tutaj, bo histo_start[byte] moze tutaj wskazywac na rekord ktory ma nie byc w tej cyfrze
					data[histo[byte]++] = v; //wpisz rekord ok
				}
				else
					data[head++] = v;
			}
		}
	}

	template<typename RECORD_T>
	void RSPermuteThread_first_store(RECORD_T* data, std::vector<uint64_t[256]>& _histo_start, std::vector<uint64_t[256]>& _histo_end, uint32_t rec_pos, uint64_t tid, uint64_t n_recs_per_this_thread, RECORD_T* cache)
	{
		//TODO: to trzeba zrefaktoryzowac tak zeby nie bylo tu sztywnych sta³ych, wycniagnac to gdzies jakos ladnie
//		auto constexpr BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);

		uint64_t* histo = _histo_start[tid];
		uint64_t* histo_end = _histo_end[tid];

		if (n_recs_per_this_thread < 400000) //400k wyznaczone eksperymentalnie dla rekordu 8B
		{
			RS_permute_without_cache(data, histo, histo_end, rec_pos);
		}
		else if (n_recs_per_this_thread < 3 * 1000 * 1000)//3M
		{
			RSCacheScatter<RECORD_T, 16>(data, histo, histo_end, rec_pos, cache);
			RS_permute_without_cache(data, histo, histo_end, rec_pos);
		}
		else if (n_recs_per_this_thread < 10 * 1000 * 1000) //10M
		{
			RSCacheScatter<RECORD_T, 32>(data, histo, histo_end, rec_pos, cache);
			RS_permute_without_cache(data, histo, histo_end, rec_pos);
		}
		else
		{
			//RSCacheScatter<RECORD_T, 128>(data, histo, histo_end, rec_pos, cache);
			RSCacheScatter<RECORD_T, 64>(data, histo, histo_end, rec_pos, cache);
			RSCacheScatter<RECORD_T, 32>(data, histo, histo_end, rec_pos, cache);
			RSCacheScatter<RECORD_T, 16>(data, histo, histo_end, rec_pos, cache);
			RSCacheScatter<RECORD_T, 8>(data, histo, histo_end, rec_pos, cache);
			RS_permute_without_cache(data, histo, histo_end, rec_pos);
		}
	}

	template<typename RECORD_T>
	void RSRepair(
		RECORD_T* data, 
		std::vector<uint64_t[256]>& histo_start, 
		std::vector<uint64_t[256]>& histo_end, 
		uint32_t byte_offset, 
		uint64_t byte, 
		uint64_t* gloabl_start, 
		uint64_t* gloabl_end, 
		uint32_t n_threads)
	{
		auto tail = gloabl_end[byte];
		for (uint32_t tid = 0; tid < n_threads; ++tid)
		{
			auto head = histo_start[tid][byte];
			while (head < histo_end[tid][byte] && head < tail)
			{
				auto v = data[head];
				uint8_t k = *(reinterpret_cast<uint8_t*>(&v) + byte_offset);
				if (k != byte)
				{
					while (head < tail)
					{
						auto w = data[--tail];
						auto k2 = *(reinterpret_cast<uint8_t*>(&w) + byte_offset);
						if (k2 == byte)
						{
							data[head++] = w;
							data[tail] = v;
							break;
						}
					}
				}
			}
		}
		gloabl_start[byte] = tail;
	}
	
	template<typename RECORD_T>
	void RS_BuildHisto(RECORD_T* A, uint64_t n_recs, uint64_t* histo, uint32_t n_threads, uint32_t rec_pos, uint32_t rec_size)
	{
		uint8_t* src = reinterpret_cast<uint8_t*>(A);
		auto data = src + rec_pos;
		
		std::vector<std::future<void>> futures;
		std::vector<uint64_t[256]> histos(n_threads);
		uint64_t start = 0;
		uint64_t r = n_recs % n_threads;
		for (uint32_t tid = 0; tid < n_threads; ++tid)
		{
			uint64_t n = n_recs / n_threads + (tid < r);
			futures.emplace_back(std::async([tid, start, n, d = data, rec_size](uint64_t outHist[]) {
				uint64_t myHist[256]{};

				auto data = d + start * rec_size;
				BuildHisto<RECORD_T, uint64_t>(myHist, n, data);

				std::copy(myHist, myHist + 256, outHist);
			}, histos[tid]));
			start += n;
		}
		//assert(start == N);
		for (auto& fut : futures)
			fut.get();

		for (uint32_t i = 0; i < 256; ++i)
			for (uint32_t tid = 0; tid < n_threads; ++tid)
				histo[i] += histos[tid][i];
	}

	template<typename RECORD_T>
	FORCE_INLINE void RSSimple_SwapRecords(RECORD_T* data, uint64_t* histo, uint64_t* hist_end, uint32_t rec_pos)
	{
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			while (histo[byte] < hist_end[byte])
			{
				auto v = data[histo[byte]];
				auto k = *(reinterpret_cast<uint8_t*>(data + histo[byte]) + rec_pos);
				while (k != byte)
				{
					std::swap(v, data[histo[k]++]);
					k = *(reinterpret_cast<uint8_t*>(&v) + rec_pos);
				}
				data[histo[byte]++] = v;
			}
		}
	}

	template<typename RECORD_T>
	void RSSimple(RECORD_T* data, uint64_t n_recs, uint32_t rec_pos, uint32_t last_byte_pos)
	{
		uint64_t histo[256]{};
		for (uint64_t i = 0; i < n_recs; ++i)
		{
			uint8_t byte = *(reinterpret_cast<uint8_t*>(data + i) + rec_pos);
			++histo[byte];
		}

		auto prev = 0ull;
		for (uint32_t i = 0; i < 256; ++i)
		{
			auto tmp = histo[i];
			histo[i] = prev;
			prev += tmp;
		}
		uint64_t histo_cpy[256];
		std::copy(histo, histo + 256, histo_cpy);
		uint64_t hist_end[256];
		for (uint32_t i = 0; i < 255; ++i)
			hist_end[i] = histo[i + 1];
		hist_end[255] = n_recs;

		RSSimple_SwapRecords(data, histo, hist_end, rec_pos);

		if (rec_pos > last_byte_pos)
		{
			for (uint32_t byte = 0; byte < 256; ++byte)
			{
				auto n = histo[byte] - histo_cpy[byte];
				if (n < 64) //TODO: czy to nie powinno zostaæ pobrane z ustawien i ewentualnie rozwazone wide i nie wide
					SmallSortDispatch(data + histo_cpy[byte], (RECORD_T*)nullptr, histo[byte] - histo_cpy[byte]);
				else
					RSSimple(data + histo_cpy[byte], n, rec_pos - 1, last_byte_pos);
			}
		}
	}

	template<typename RECORD_T, unsigned BUFFER_WIDTH>
	void CacheRecordToMainMemory_single_thread(
		RECORD_T* data,
		uint8_t byte,
		uint8_t key,
		bool* first_store,
		uint64_t* histo,
		uint64_t* histo_begin,
		uint64_t& read_pos,
		RECORD_T* cache_buff
	)
	{
		constexpr uint32_t BUFFER_WIDTH_IN_128BIT_WORDS = BUFFER_WIDTH * sizeof(RECORD_T) / 16;
		if (first_store[key])
		{
			first_store[key] = false;
			int64_t offset = histo_begin[key] % BUFFER_WIDTH;

			if (key == byte)
			{
				//ok na razie zrobie std copy potem przejdziemy na IntCopy
				//std::copy(cache_buff + key_)
				std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, BUFFER_WIDTH - offset, data + histo[key] - BUFFER_WIDTH + offset);
			}
			else
			{
				read_pos -= BUFFER_WIDTH - offset;
				std::copy_n(data + histo[key] - BUFFER_WIDTH + offset, BUFFER_WIDTH - offset, data + read_pos); //robimy miejsce
				std::copy_n(cache_buff + BUFFER_WIDTH * key + offset, BUFFER_WIDTH - offset, data + histo[key] - BUFFER_WIDTH + offset); //wlasciwe kopiowanie
			}
			//write_pos[key] += BUFFER_WIDTH - offset;
		}
		else
		{
			if (key == byte)
			{
				//std::cerr << "*";
				//std::copy_n(cache_buff + key * BUFFER_WIDTH, BUFFER_WIDTH, data + histo[key] - BUFFER_WIDTH);
				IntrCopy128< BUFFER_WIDTH_IN_128BIT_WORDS, 1>::Copy(data + histo[key] - BUFFER_WIDTH, cache_buff + key * BUFFER_WIDTH);
			}
			else
			{
				read_pos -= BUFFER_WIDTH;

				//make a place for a buffer
				std::copy_n(data + histo[key] - BUFFER_WIDTH, BUFFER_WIDTH, data + read_pos);

				//std::copy_n(cache_buff + BUFFER_WIDTH * key, BUFFER_WIDTH, data + histo[key] - BUFFER_WIDTH);
				IntrCopy128< BUFFER_WIDTH_IN_128BIT_WORDS, 1>::Copy(data + histo[key] - BUFFER_WIDTH, cache_buff + BUFFER_WIDTH * key);
			}
		}
	}

	template<typename RECORD_T>
	uint64_t MoveToAlignment(RECORD_T*& data, uint64_t* histo, uint64_t* histo_end)
	{
		uint64_t moved_by = 0;
		while (uint64_t(data) % ALIGNMENT)
			--data, ++moved_by;
		if(moved_by)
			for (uint32_t byte = 0; byte < 256; ++byte)
			{
				histo[byte] += moved_by;
				histo_end[byte] += moved_by;
			}
		return moved_by;
	}

	template<typename RECORD_T>
	void MoveBack(RECORD_T*& data, uint64_t* histo, uint64_t* histo_end, uint64_t was_moved_by)
	{
		if (!was_moved_by)
			return;
		
		data += was_moved_by;
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			histo[byte] -= was_moved_by;
			histo_end[byte] -= was_moved_by;
		}		
	}

	template<typename RECORD_T, unsigned BUFFER_WIDTH>
	void RSCacheScatter_single_thread(RECORD_T* data, uint64_t* histo, uint64_t* histo_end, uint32_t rec_pos, RECORD_T* cache_buff)
	{
		bool first_store[256];
		uint64_t histo_begin[256];

		uint64_t data_moved_by = MoveToAlignment(data, histo, histo_end);
		
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			std::copy(histo, histo + 256, histo_begin);
			std::fill_n(first_store, 256, true);
			auto read_pos = histo[byte];
			while (read_pos < histo_end[byte])
			{
				uint8_t key = *(reinterpret_cast<uint8_t*>(data + read_pos) + rec_pos);
				int index_x = histo[key] % BUFFER_WIDTH;
				cache_buff[key * BUFFER_WIDTH + index_x] = data[read_pos];
				++read_pos;
				++histo[key];				
				if (index_x == BUFFER_WIDTH - 1)
					CacheRecordToMainMemory_single_thread<RECORD_T, BUFFER_WIDTH>(data, byte, key, first_store, histo, histo_begin, read_pos, cache_buff);				
			}

			our_fence();

			//store records that are still in cache buffer back to source digit (byte) range 
			CleanCache<RECORD_T, BUFFER_WIDTH>(data, byte, first_store, histo, histo_begin, cache_buff);
		}		

		MoveBack(data, histo, histo_end, data_moved_by);
	}

	template<typename RECORD_T>
	class RSSortTask
	{
		CRadixMSDTaskQueue<RECORD_T>& tasks_queue;
		uint64_t use_queue_min_recs = 0;
		RECORD_T* cache_buff;

		/*
			Pokuszam siê o lekki opis tego co tu siê dzieje.
			Na poczatku oczywiscie budowany jest histogram.
			Rekordy rozsylamy z wykorzystaniem cache.
			Rekordy sa "przemielane" przez kolejne cyfry podobnie jak ma to miejsce w trywialnej implementacji.

			Przetwarzanie dzia³a tak ze dla kazdej cyfry robimy co nastepuje.
			Kopiujemy sobie pocz¹tki nieposortowanych jeszcze zakresów dla wszystkich cyfr.
			Pocz¹tkowo caly zakres kazdej cyfry jest nieposortowny, ale w miare iscia po cyfrach zakresy staja sie coraz bardziej posortowane.
			Moze to by sie dalo jakos zoptymalizowac w ten sposob zeby nie analizywac tych cyfr gdzie jest stosunkowo malo rekordow wzgedem BUFFER_WIDTH.
			Generalnie dla analizy danej cyfry konieczne jest aby bufor w cache by³ ca³y pusty (to jest w zasadzie do przemyslenai bo moze daloby sie bez tego tylko trzeba sie dokladniej zastanowic, tylko ze to moze nie miec zbyt duzego znaczenia w praktyce, wiec na razie to zostawiam)
			Poczatkowo zakladamy ze dla kazdej cyfry aktualnei jestesmy w niewyrownanym zakresie.
			Sciagamy sobie kolejne rekordy dla danej wartosci bajtu i dodajemy do cache.
			Jak dobijemy do konca bufora w cache to sprawdzamy czy to jest pierwszy (nieodpowiednio wyrownany zapis) -> wtedy tylko czesc z tego bufora trzeba zapisac.
			Rozrozniane sa dwie sytuacje:
				1. Zapis nastapi do tego bajtu z ktorego czytamy, wtedy po prostu zapisujemy z cache, to jest mozliwe bo na pewno tam jest miejsce (no bo skoro cache by³ pusty, a czytalismy do cache, to nawet jezeli czytalismy same rekordy o wartosci bajtu rownej aktualnej wartoœci to musi byc odpowiednia ilosc miejsca).
				2. Zapis do innego bajtu niz czytamy. Jezeli w buforze cache mamy X rekordow do zapisu, to najpierw z docelowej pozycji sciagamy je sobie do zakresu (dziury) w buforze wejsciowym danej wartosci bajtu, a potem wykonujemy zapis.
			Jezeli jednak to nie jest pierwszy zapis to w zasadzie robimy tak samo tylko ze mamy pewnosc ze trzeba zapisac BUFFER_WIDTH rekordow. To jest to co w zasadzie powinno dziaæ siê najczêœciej.
			Tylko jeszcze jest taka kwestia, ¿e dochodzi tutaj do œciagania rekordów do aktualnego zakresu. To ma tak¹ wadê, ¿e sciagamy pod niewyrownany adres, fajnie by bylo sobie te rekordy kopiowac szybciej tylko nie iwiem czy sie tak da
		*/		
		void RSCache_firstStore_single_thread(RECORD_T* data, uint64_t n_recs, uint32_t rec_pos, uint32_t last_byte_pos, bool is_narrow)
		{
			if (n_recs < 2)
				return;
			////TODO: odkomentowac
			//if (n_recs * sizeof(RECORD_T) < 1 << 16)
			////if (n_recs * sizeof(RECORD_T) < 1 << 18) //!!!!!! wazne: dla 1G elementow, dla kluczy 3B, rekord 8B, to dziala lepiej, wiec moze inaczje nalezy postawic, albo uzasadnic ta granice, albo moze pomoze gdy BUFFER_WIDTH bedzie zalezne jakos od liczby elementow
			//{
			//	RSSimple(data, n_recs, rec_pos, last_byte_pos);
			//	return;
			//}

			//TODO: to sa wyznaczone eksperymentalnie wartosci, dla rekordu 8B
			if (n_recs < 400 * 1000) //400k
			{
				RSSimple(data, n_recs, rec_pos, last_byte_pos);
				return;
			}

			uint64_t histo[256]{};
			
			auto ptr = reinterpret_cast<uint8_t*>(data) + rec_pos;
			BuildHisto<RECORD_T, uint64_t>(histo, n_recs, ptr);
			

			auto prev = 0ull;
			uint64_t histo_end[256];
			uint64_t largest_bin_size = 0;
			for (uint32_t i = 0; i < 256; ++i)
			{
				auto tmp = histo[i];
				histo[i] = prev;
				prev += tmp;
				histo_end[i] = prev;
				if (tmp > largest_bin_size)
					largest_bin_size = tmp;
			}

			uint64_t histo_copy[257];
			std::copy(histo, histo + 256, histo_copy);
			histo_copy[256] = n_recs;
			
//			constexpr uint32_t BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);

			if (n_recs < 1 * 1000 * 1000) //1M
			{
				RSCacheScatter_single_thread<RECORD_T, 8>(data, histo, histo_end, rec_pos, cache_buff);
			}
			else if(n_recs < 2 * 7000 * 1000) //2,7M
			{
				RSCacheScatter_single_thread<RECORD_T, 16>(data, histo, histo_end, rec_pos, cache_buff);
			}
			else if (n_recs < 15 * 1000 * 1000) //15M
			{
				RSCacheScatter_single_thread<RECORD_T, 32>(data, histo, histo_end, rec_pos, cache_buff);
			}
			else
			{
				//tutaj zdaje sie mogloby byc tez wywolanie kilkukrotne 64, 32, 16, i byc moze to by dzialalo nieco szybciej
				RSCacheScatter_single_thread<RECORD_T, 64>(data, histo, histo_end, rec_pos, cache_buff);
				RSCacheScatter_single_thread<RECORD_T, 32>(data, histo, histo_end, rec_pos, cache_buff);
				RSCacheScatter_single_thread<RECORD_T, 16>(data, histo, histo_end, rec_pos, cache_buff);
				RSCacheScatter_single_thread<RECORD_T, 8>(data, histo, histo_end, rec_pos, cache_buff);
			}
			//UWAGA: na razie sobie 3 krotnie wywoluje z coraz mniejszym cache buff, zeby ograniczyc to ile rekordow potem w ostatecznej korekcie jest wolanych, ale z tym trzebaby potestowac
			//RSCacheScatter_single_thread<RECORD_T, BUFFER_WIDTH>(data, histo, histo_end, rec_pos, cache_buff);
			//RSCacheScatter_single_thread<RECORD_T, BUFFER_WIDTH / 2>(data, histo, histo_end, rec_pos, cache_buff);
			//RSCacheScatter_single_thread<RECORD_T, BUFFER_WIDTH / 4>(data, histo, histo_end, rec_pos, cache_buff);

			
			RSSimple_SwapRecords(data, histo, histo_end, rec_pos);

			if (rec_pos > last_byte_pos)
			{
				uint64_t constexpr narrow_small_sort_threshold = GetSmallSortThreshold(RECORD_T::RECORD_SIZE);
				uint64_t constexpr wide_small_sort_threshold = GetWideSmallSortThreshold(RECORD_T::RECORD_SIZE);

				if (largest_bin_size <= wide_small_sort_threshold || (is_narrow && largest_bin_size <= narrow_small_sort_threshold))
				{
					//All bins are small 
					for (uint32_t i = 0; i < 256; ++i)
					{
						uint64_t new_n = histo_copy[i + 1] - histo_copy[i];
						if (new_n)
							SmallSortDispatch<RECORD_T>(data + histo_copy[i], nullptr, new_n);
					}
				}
				else
				{
					for (uint32_t i = 0; i < 256; ++i)
					{
						uint64_t new_n = histo_copy[i + 1] - histo_copy[i];
						if (new_n <= wide_small_sort_threshold || (is_narrow && new_n <= narrow_small_sort_threshold))
						{
							if (new_n > 1)
								SmallSortDispatch<RECORD_T>(data + histo_copy[i], nullptr, new_n);
						}
						else
						{
							if (new_n >= use_queue_min_recs)
								tasks_queue.push(data + histo_copy[i], nullptr, new_n, rec_pos - 1, last_byte_pos, is_narrow); //TODO: a nie wiem dlaczego tutaj nie jest wywolane sprawdzenie czy nie jest narrowing, tzn check_narrowing
							else
								//TODO: w radulsie mamy jeszcze rozroznienie na small radix threshold, wiec tutaj tez by dobrze bylo dodac taka implementacje
								//if (new_n < SMALL_RADIX_THRESHOLD)
								//	;//TODO: !!!!!!!!!!!!!!!!!!!!!
								//else
								RSCache_firstStore_single_thread(data + histo_copy[i], new_n, rec_pos - 1,
									last_byte_pos, check_narrowing(n_recs, new_n));
						}						
					}
				}
			}
		}

	public:
		RSSortTask(CRadixMSDTaskQueue<RECORD_T>& tasks_queue, uint64_t use_queue_min_recs, RECORD_T* cache_buff) :
			tasks_queue(tasks_queue),
			use_queue_min_recs(use_queue_min_recs),
			cache_buff(cache_buff)
		{
		}

		void operator()()
		{
			RECORD_T* data, *tmp;
			uint64_t n_recs;
			uint32_t rec_pos;
			uint32_t last_byte_pos;
			bool is_narrow;
			while (tasks_queue.pop(data, tmp, n_recs, rec_pos, last_byte_pos, is_narrow))
			{
				this->RSCache_firstStore_single_thread(data, n_recs, rec_pos, last_byte_pos, is_narrow);
				tasks_queue.notify_task_finished();
			}
		}
	};

	template<typename RECORD_T>
	FORCE_INLINE void RS_multithreaded_permute(RECORD_T* data,
		uint64_t* histo_start,
		uint64_t* histo_end,
		uint32_t rec_pos,
		uint32_t n_threads,
		RECORD_T** thread_cache,
		std::vector<uint64_t[256]>& threads_histo_start,
		std::vector<uint64_t[256]>& threads_histo_end)
	{

		//TODO: generalnie moze byc lepiej podzielic to na wiecej obszarow, podobnie jak to jest robione w radulsie, tzn podzielic sobie na 8t, tylko to ma tez pewna wade
		//mianowicie wtedy generalnie wiecej zostanie smieci w cache zawsze 
		std::vector<uint64_t> per_thread_recs(n_threads);
		for (uint32_t byte = 0; byte < 256; ++byte)
		{
			auto n = histo_end[byte] - histo_start[byte];
			auto per_thread = n / n_threads;
			threads_histo_start[0][byte] = histo_start[byte];
			for (uint32_t tid = 0; tid < n_threads - 1; ++tid)
			{
				auto additional = (tid < (n % n_threads)) ? 1 : 0;
				auto in_fact_per_thread = per_thread + additional;
				threads_histo_end[tid][byte] = threads_histo_start[tid][byte] + in_fact_per_thread;
				threads_histo_start[tid + 1u][byte] = threads_histo_end[tid][byte];

				per_thread_recs[tid] += threads_histo_end[tid][byte] - threads_histo_start[tid][byte];
			}
			threads_histo_end[n_threads - 1][byte] = histo_end[byte];
			per_thread_recs[n_threads - 1] += threads_histo_end[n_threads - 1][byte] - threads_histo_start[n_threads - 1][byte];
		}

		std::vector<std::thread> threads;

		for (uint32_t tid = 0; tid < n_threads; ++tid)
			threads.emplace_back(RSPermuteThread_first_store<RECORD_T>, data, std::ref(threads_histo_start), std::ref(threads_histo_end), rec_pos, tid, per_thread_recs[tid], thread_cache[tid]);

		for (auto& th : threads)
			th.join();
	}

	template<typename RECORD_T>
	FORCE_INLINE void RS_multithreaded_repair(RECORD_T* data,
		uint32_t n_threads,
		uint64_t* histo_start,
		uint64_t* histo_end,
		uint32_t rec_pos,
		std::vector<uint64_t[256]>& threads_histo_start,
		std::vector<uint64_t[256]>& threads_histo_end)
	{
		std::vector<std::thread> threads;
		uint32_t start = 0;
		uint32_t per_thread = 256 / n_threads;
		for (uint32_t tid = 0; tid < n_threads; ++tid)
		{
			auto additional = (tid < (256 % n_threads)) ? 1 : 0;
			auto in_fact = per_thread + additional;
			auto end = start + in_fact;
			threads.emplace_back([data, &threads_histo_start, &threads_histo_end, histo_start = histo_start, histo_end = histo_end, rec_pos, n_threads](uint32_t s, uint32_t e) {
				for (uint32_t byte = s; byte < e; ++byte)
					RSRepair<RECORD_T>(data, threads_histo_start, threads_histo_end, rec_pos, byte, histo_start, histo_end, n_threads);
			}, start, end);

			start = end;
		}

		for (auto& th : threads)
			th.join();
		threads.clear();
	}

	//TODO: docelowo zrobic taka hybryde sortowania zeby sobie mogla potem kolejne podprzedzialiki sortowac z wykorzystaniem dodatkowej pamiêci
	template<typename RECORD_T>
	void RS_first_store(RECORD_T* A, uint64_t n_recs, uint32_t rec_size, uint32_t rec_pos, uint32_t last_byte_pos, uint32_t n_threads,
		bool is_first_level, uint64_t is_big_threshold, RECORD_T** thread_cache, uint64_t n_total_recs)
	{
		constexpr uint64_t current_small_sort_threshold = GetSmallSortThreshold(RECORD_T::RECORD_SIZE);
		if (n_recs <= current_small_sort_threshold)
		{
			SmallSortDispatch<RECORD_T>(A, nullptr, n_recs);
			return;
		}
		if (n_threads == 1)
		{
			CRadixMSDTaskQueue<RECORD_T> tmp_queue;
			RSSortTask<RECORD_T> task(tmp_queue, std::numeric_limits<uint64_t>::max(), *thread_cache);
			tmp_queue.push(A, nullptr, n_recs, rec_pos, last_byte_pos, false);
			task();			
			return;
		}

		auto N = n_recs;
		uint64_t histo[256]{};
		RS_BuildHisto(A, n_recs, histo, n_threads, rec_pos, rec_size);

		//accumulate histo 
		uint64_t prev = 0;
		uint64_t histo_end[256];
		for (uint32_t i = 0; i < 256; ++i)
		{
			auto t = histo[i];
			histo[i] = prev;
			prev += t;
			histo_end[i] = prev;
		}

		uint64_t histo_copy[257];
		std::copy(std::begin(histo), std::end(histo), std::begin(histo_copy));
		histo_copy[256] = N; //TODO: byc moze niepotrzebne

		//TODO: Partition for repair?

		std::vector<uint64_t[256]> threads_histo_start(n_threads);
		std::vector<uint64_t[256]> threads_histo_end(n_threads);

		while (AnyBucketNotEmpty(histo, histo_end))
		{
			RS_multithreaded_permute(A, histo, histo_end, rec_pos, n_threads, thread_cache, threads_histo_start, threads_histo_end);
			RS_multithreaded_repair(A, n_threads, histo, histo_end, rec_pos, threads_histo_start, threads_histo_end);
		}
		
		if (rec_pos > last_byte_pos)
		{
			CRadixMSDTaskQueue<RECORD_T> tasks_queue;
			std::vector<std::tuple<RECORD_T*, uint64_t>> big_bins;
			uint64_t n_recs_in_big_bins = 0;
			for (uint32_t i = 0; i < 256; ++i)
			{
				auto n = static_cast<uint64_t>(histo_copy[i + 1] - histo_copy[i]);
				if (n > 0)
				{
					if (n > is_big_threshold)
					{
						if (!is_first_level)
							RS_first_store(A + histo_copy[i], n, rec_size, rec_pos - 1, last_byte_pos,
								n_threads, false, is_big_threshold, thread_cache, n_total_recs);
						else
							big_bins.emplace_back(A + histo_copy[i], n),
							n_recs_in_big_bins += n;
					}
					else
						tasks_queue.push(A + histo_copy[i], nullptr, n, rec_pos - 1, last_byte_pos, check_narrowing(n_recs, n));
				}
			}

			std::sort(big_bins.begin(), big_bins.end(), [](const auto& x, const auto& y) {return std::get<1>(x) > std::get<1>(y); });
			
			auto n_threads_for_big_bins = std::min(n_threads, static_cast<uint32_t>(ceil(n_threads * n_recs_in_big_bins * 5.0 / (4u * n_total_recs))));

			uint32_t n_threads_for_small_bins_running;
			auto n_threads_for_small_bins = n_threads - n_threads_for_big_bins;

			std::vector<std::thread> threads;
			using SORTER_T = RSSortTask<RECORD_T>;
			std::vector<std::unique_ptr<SORTER_T>> sorters;
			auto use_queue_min_recs = n_recs / 4096;
			for (n_threads_for_small_bins_running = 0; n_threads_for_small_bins_running < n_threads_for_small_bins; ++n_threads_for_small_bins_running)
			{
				sorters.emplace_back(std::make_unique<SORTER_T>(tasks_queue, use_queue_min_recs,
					thread_cache[n_threads_for_small_bins_running]));
				threads.emplace_back(std::ref(*sorters.back().get()));
			}
			for (auto& big_bin : big_bins)
				RS_first_store(std::get<0>(big_bin), std::get<1>(big_bin), rec_size,
					rec_pos - 1, last_byte_pos, n_threads_for_big_bins, false, is_big_threshold, 
					thread_cache + n_threads_for_small_bins_running, n_total_recs);

			for (; n_threads_for_small_bins_running < n_threads; ++n_threads_for_small_bins_running)
			{
				sorters.emplace_back(std::make_unique<SORTER_T>(tasks_queue, use_queue_min_recs,
					thread_cache[n_threads_for_small_bins_running]));
				threads.emplace_back(std::ref(*sorters.back().get()));
			}
			for (auto& th : threads)
				th.join();
		}
	}

	template<typename RECORD_T>
	class CacheBuffManager
	{
		uint8_t** _raw_caches;		
		uint32_t n_threads;
	public:
		RECORD_T ** thread_cache;
		CacheBuffManager(uint32_t n_threads):n_threads(n_threads)
		{
			auto constexpr BUFFER_WIDTH = GetBufferWidth(sizeof(RECORD_T) / 8);
			auto constexpr per_threads_cache_buff_size = sizeof(RECORD_T) * 256 * BUFFER_WIDTH + ALIGNMENT;
			_raw_caches = new uint8_t *[n_threads];
			thread_cache = new RECORD_T *[n_threads];
			for (uint32_t tid = 0; tid < n_threads; ++tid)
			{
				auto _raw_cache = new uint8_t[per_threads_cache_buff_size];
				_raw_caches[tid] = _raw_cache;

				uint8_t* al_cache = _raw_cache;
				while ((uint64_t)al_cache % ALIGNMENT)
					++al_cache;
				RECORD_T* cache = reinterpret_cast<RECORD_T*>(al_cache);
				thread_cache[tid] = cache;
			}
		}
		~CacheBuffManager()
		{
			for (uint32_t tid = 0; tid < n_threads; ++tid)
				delete[] _raw_caches[tid];

			delete[] _raw_caches;
			delete[] thread_cache;
		}
	};

	//TODO: mozna jeszcze rozpatrzec counter size, podobnie jak jest w radulsie
	template<typename RECORD_T>
	void RadixSortMSD_template(RECORD_T* data, RECORD_T* /*tmp not used for RS*/, uint64_t n_recs, uint32_t rec_size_in_bytes, uint32_t key_size_in_bytes, uint32_t last_byte_pos, uint32_t n_threads)
	{
		uint64_t is_big_threshold = 2 * n_recs / (3 * n_threads);
		if (is_big_threshold < 4 * n_recs / 256)
			is_big_threshold = 4 * n_recs / 256;

		CacheBuffManager<RECORD_T> cache_buff_manager(n_threads);
		RS_first_store(data, n_recs, rec_size_in_bytes, key_size_in_bytes - 1, last_byte_pos, n_threads, true, is_big_threshold, cache_buff_manager.thread_cache, n_recs);
	}

#ifdef DISPATCH_ONLY_REC_SIZE	
	template<uint32_t REC_SIZE_IN_UINT64>
	class RecSizeDispatcher
	{
		friend void RunWrapper(const SortParams& p);
		friend class RecSizeDispatcher<REC_SIZE_IN_UINT64 + 1>;
		static void Dispatch(const SortParams& p)
		{
			if (p.rec_size / 8 == REC_SIZE_IN_UINT64)
			{
				using record_type = Record<REC_SIZE_IN_UINT64, REC_SIZE_IN_UINT64>;
				record_type* data = reinterpret_cast<record_type*>(p.input);
				record_type* tmp = reinterpret_cast<record_type*>(p.tmp);
				RadixSortMSD_template(data, tmp, p.n_recs, p.rec_size, p.key_size, p.last_byte_pos, p.n_threads);
			}
			else
				RecSizeDispatcher<REC_SIZE_IN_UINT64 - 1>::Dispatch(p);
		}
	};

	template<>
	class RecSizeDispatcher<1>
	{
		friend void RunWrapper(const SortParams& p);
		friend class RecSizeDispatcher<2>;
		static void Dispatch(const SortParams& p)
		{
			using record_type = Record<1, 1>;
			record_type* data = reinterpret_cast<record_type*>(p.input);
			record_type* tmp = reinterpret_cast<record_type*>(p.tmp);
			RadixSortMSD_template(data, tmp, p.n_recs, p.rec_size, p.key_size, p.last_byte_pos, p.n_threads);
		}
	};
#else

	template<uint32_t REC_SIZE_IN_UINT64> class RecSizeDispatcher;

	template<uint32_t REC_SIZE_IN_UINT64, uint32_t KEY_SIZE_IN_UINT64>
	class KeySizeDispatcher
	{
		friend class RecSizeDispatcher<REC_SIZE_IN_UINT64>;
		friend class KeySizeDispatcher<REC_SIZE_IN_UINT64, KEY_SIZE_IN_UINT64 + 1>;
		static void Dispatch(const SortParams& p)
		{
			if ((p.key_size + 7) / 8 == KEY_SIZE_IN_UINT64)
			{
				using record_type = Record<REC_SIZE_IN_UINT64, KEY_SIZE_IN_UINT64>;
				record_type* data = reinterpret_cast<record_type*>(p.input);
				record_type* tmp = reinterpret_cast<record_type*>(p.tmp);
				RadixSortMSD_template(data, tmp, p.n_recs, p.rec_size, p.key_size, p.last_byte_pos, p.n_threads);
			}
			else
				KeySizeDispatcher<REC_SIZE_IN_UINT64, KEY_SIZE_IN_UINT64 - 1>::Dispatch(p);
		}
	};

	template<uint32_t REC_SIZE_IN_UINT64>
	class KeySizeDispatcher<REC_SIZE_IN_UINT64, 1>
	{
		friend class RecSizeDispatcher<REC_SIZE_IN_UINT64>;
		friend class KeySizeDispatcher<REC_SIZE_IN_UINT64, 2>;
		static void Dispatch(const SortParams& p)
		{
			using record_type = Record<REC_SIZE_IN_UINT64, 1>;
			record_type* data = reinterpret_cast<record_type*>(p.input);
			record_type* tmp = reinterpret_cast<record_type*>(p.tmp);
			RadixSortMSD_template(data, tmp, p.n_recs, p.rec_size, p.key_size, p.last_byte_pos, p.n_threads);
		}
	};

	template<uint32_t REC_SIZE_IN_UINT64>
	class RecSizeDispatcher
	{
		friend void RunWrapper(const SortParams& p);
		friend class RecSizeDispatcher<REC_SIZE_IN_UINT64 + 1>;
		static void Dispatch(const SortParams& p)
		{
			if (p.rec_size / 8 == REC_SIZE_IN_UINT64)
				KeySizeDispatcher<REC_SIZE_IN_UINT64, REC_SIZE_IN_UINT64>::Dispatch(p);
			else
				RecSizeDispatcher<REC_SIZE_IN_UINT64 - 1>::Dispatch(p);
		}
	};

	template<>
	class RecSizeDispatcher<1>
	{
		friend void RunWrapper(const SortParams& p);
		friend class RecSizeDispatcher<2>;
		static void Dispatch(const SortParams& p)
		{
			KeySizeDispatcher<1, 1>::Dispatch(p);
		}
	};

#endif

	void RunWrapper(const SortParams& p)
	{
		RecSizeDispatcher<MAX_REC_SIZE_IN_BYTES / 8>::Dispatch(p);
	}
	}
}
