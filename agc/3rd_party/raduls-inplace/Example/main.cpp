#define _CRT_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS

#ifdef _MSC_VER 
#include <ppl.h>
#elif __GNUG__
#include <omp.h>
#include <parallel/algorithm>
#endif

#include "raduls.h"
#include "Opt.h"

#include <memory>
#include <random>
#include <vector>
#include <chrono>


template<typename Rec>
struct RecUnderlyingType
{
	using Type = uint64_t;
	using GeneratorType = std::mt19937_64;
	static uint64_t GetMostSignificantPartMask(uint32_t key_size)
	{
		uint64_t most_significant_uint64_mask = ~0;
		if (key_size % 8)
			most_significant_uint64_mask >>= (8 - key_size % 8) * 8;
		return most_significant_uint64_mask;
	}

};
#ifdef ENABLE_4BYTE_INT_VARIANT
template<unsigned REC_SIZE, unsigned KEY_SIZE> struct Rec;
template<>
struct RecUnderlyingType<Rec<0, 0>>
{
	using Type = uint32_t;
	using GeneratorType = std::mt19937;

	static uint32_t GetMostSignificantPartMask(uint32_t key_size)
	{
		uint32_t most_significant_uint32_mask = ~0;
		if (key_size % 4)
			most_significant_uint32_mask >>= (4 - key_size) * 8;
		return most_significant_uint32_mask;
	}
};
#endif // ENABLE_4BYTE_INT_VARIANT

template<typename T>
void RunExperiment(const Params& params)
{
	auto mem_needed = params.rec_size.val * params.n_recs.val + raduls::ALIGNMENT;
	std::cerr << "Alloc input memory...";
	auto _raw_input = new uint8_t[mem_needed];
	std::cerr << "done.\n";
	//std::cerr << "Alloc tmp memory...";
	//auto _raw_tmp = new uint8_t[mem_needed];
	//std::cerr << "done.\n";

	uint8_t* _raw_res_val = nullptr;
	uint8_t* res_val = nullptr;
	if (params.full_validation.val)
	{
		std::cerr << "Alloc memory for full validation...";
		_raw_res_val = new uint8_t[mem_needed];
		res_val = _raw_res_val;
		while (reinterpret_cast<uintptr_t>(res_val) % raduls::ALIGNMENT)
			++res_val;
		std::cerr << "done.\n";
	}

	auto input = _raw_input;
	while (reinterpret_cast<uintptr_t>(input) % raduls::ALIGNMENT)
		++input;

	//auto tmp = _raw_tmp;
	//while (reinterpret_cast<uintptr_t>(tmp) % raduls::ALIGNMENT)
	//	++tmp;

	if (params.input.val != "")
	{
		std::cerr << "Reading input data from file: " << params.input.val << "...";
		auto f = fopen(params.input.val.c_str(), "rb");
		if (!f)
		{
			std::cerr << "Error: cannot open file: " << params.input.val << "\n";
			exit(1);
		}
		fread(input, params.rec_size.val, params.n_recs.val, f);
		fclose(f);
		std::cerr << "done.\n";
	}
	else
	{
		std::vector<std::thread> threads;
		auto n_threads = params.n_threads.val;
		auto n_recs = params.n_recs.val;
		auto d = reinterpret_cast<T*>(input);

		auto n_generate_threads = n_threads;
		if (params.all_thread_to_gen_data.val)
		{
			n_generate_threads = std::thread::hardware_concurrency();

		}
		std::cerr << "Randomly fill input memory using " << n_generate_threads << " threads...";
		auto start = std::chrono::high_resolution_clock::now();

		auto most_significant_part_mask = RecUnderlyingType<T>::GetMostSignificantPartMask(params.key_size.val);
		for (uint32_t th_id = 0; th_id < n_generate_threads; ++th_id)
		{
			threads.push_back(std::thread([th_id, n_generate_threads, n_recs, &d, most_significant_part_mask]
			{
				typename RecUnderlyingType<T>::GeneratorType gen;
				//std::mt19937_64 gen;
				using underlying_type = typename RecUnderlyingType<T>::Type;
				std::uniform_int_distribution<underlying_type> dis(0, std::numeric_limits< underlying_type>::max());

				//std::geometric_distribution<RecUnderlyingType<T>::Type> dis(0.8);

				uint64_t part = n_recs / n_generate_threads;
				uint64_t start_i = th_id * part;
				uint64_t end_i = (th_id + 1) * part;
				if (th_id == n_generate_threads - 1)
					end_i = n_recs;
				gen.seed(th_id * part);

				for (uint64_t i = start_i; i < end_i; ++i)
					d[i].RandomFill(gen, dis, most_significant_part_mask);
			}));
		}

		for (auto& x : threads)
			x.join();
		threads.clear();

		std::cerr << "done.\n";
		std::cerr << "Time: " << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "\n";
	}

	if (params.full_validation.val)
	{
		std::cerr << "Copy input data for full result validation...";
		auto start = std::chrono::high_resolution_clock::now();
		std::copy(input, input + params.n_recs.val * params.rec_size.val, res_val);
		std::cerr << "done.\n";
		std::cerr << "Time: " << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "\n";
	}

	//if (params.clear_tmp.val)
	//{		
	//	std::cerr << "Cleaning tmp array...";
	//	auto start = std::chrono::high_resolution_clock::now();
	//	raduls::CleanTmpArray(tmp, params.n_recs.val, params.rec_size.val, params.n_threads.val);				
	//	std::cerr << "done.\n";
	//	std::cerr << "Time: " << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "\n";
	//}

	std::cerr << "Sorting...";
	auto start = std::chrono::high_resolution_clock::now();
	if (params.n_phases.val)
		raduls::PartialRadixSortMSD(input, nullptr, params.n_recs.val, params.rec_size.val, params.key_size.val, *params.n_phases.val, params.n_threads.val);
	else
		raduls::RadixSortMSD(input, nullptr, params.n_recs.val, params.rec_size.val, params.key_size.val, params.n_threads.val);

	auto dur = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start);
	auto time = dur.count();
	std::cerr << "done.\n";
	std::cerr << "Time: " << time << "\n";

/*	uint64_t phases_performed = params.key_size.val;
	if (params.n_phases.val)
		phases_performed = *params.n_phases.val;
		*/
	auto res = input;
	//	if (phases_performed % 2) 
	//		res = tmp;


	std::cerr << "Checking if result is sorted...";
	auto as_T = reinterpret_cast<T*>(res);
	bool sorted = false;
	if (params.n_phases.val)
	{
		auto bytes_to_mask = params.key_size.val - *params.n_phases.val;
		sorted = std::is_sorted(as_T, as_T + params.n_recs.val, [bytes_to_mask](const auto& e1, const auto& e2) {
			auto cp1 = e1;
			auto cp2 = e2;
			auto* p1 = reinterpret_cast<uint8_t*>(&cp1);
			auto* p2 = reinterpret_cast<uint8_t*>(&cp2);
			for (uint64_t i = 0; i < bytes_to_mask; ++i)
				p1[i] = p2[i] = 0;
			return cp1 < cp2;
		});

		if (params.key_size.val == *params.n_phases.val)
		{

		}
		else
		{
			if (std::is_sorted(as_T, as_T + params.n_recs.val))
			{
				std::cerr << "\nWarning: only part of key was sorted, but whole result is sorted (quite possible when uniformly distributed data is generated)\n";
			}
		}

	}
	else
	{
		sorted = std::is_sorted(as_T, as_T + params.n_recs.val);
	}
	std::cerr << "done.\n";
	if (!sorted)
	{
		std::cerr << "Error: Result is not sorted!\n";
		exit(1);
	}
	else
	{
		std::cerr << "Info: OK result sorted!\n";
	}


	if (params.full_validation.val)
	{
		std::cerr << "Sorting pattern for full result validation...";
		auto res_val_as_T = reinterpret_cast<T*>(res_val);
		auto start = std::chrono::high_resolution_clock::now();
#ifdef _MSC_VER		
		concurrency::parallel_sort(res_val_as_T, res_val_as_T + params.n_recs.val);
#elif __GNUG__
		omp_set_num_threads(params.n_threads.val);
		__gnu_parallel::sort(res_val_as_T, res_val_as_T + params.n_recs.val);
#else
		std::sort(res_val_as_T, res_val_as_T + params.n_recs.val);
#endif
		std::cerr << "done.\n";
		std::cerr << "Time: " << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "\n";
		std::cerr << "Compare pattern with raduls result...";

		bool res_ok = false;

		if (params.n_phases.val)
		{
			auto bytes_to_mask = params.key_size.val - *params.n_phases.val;
			res_ok = std::equal(as_T, as_T + params.n_recs.val, res_val_as_T, [bytes_to_mask](const auto& e1, const auto& e2) {
				auto cp1 = e1;
				auto cp2 = e2;
				auto* p1 = reinterpret_cast<uint8_t*>(&cp1);
				auto* p2 = reinterpret_cast<uint8_t*>(&cp2);
				for (uint64_t i = 0; i < bytes_to_mask; ++i)
					p1[i] = p2[i] = 0;
				return cp1 == cp2;
			});
		}
		else
			res_ok = std::equal(as_T, as_T + params.n_recs.val, res_val_as_T);
		std::cerr << "done.\n";
		if (!res_ok)
		{
			std::cerr << "Error: Result not equal to pattern\n";
			exit(1);
		}
		else
		{
			std::cerr << "Info: OK result and pattern are equal\n";
		}
	}

	if (params.output.val != "")
	{
		std::cerr << "Store result to file: " << params.output.val;
		auto f = fopen(params.output.val.c_str(), "wb");
		if (!f)
		{
			std::cerr << "Error: Cannot open file " << params.output.val << "\n";
			exit(1);
		}
		fwrite(res, params.rec_size.val, params.n_recs.val, f);
		fclose(f);
		std::cerr << "done.\n";
	}

	delete[] _raw_input;
	//	delete[] _raw_tmp;
	delete[] _raw_res_val;
}


template<unsigned REC_SIZE, unsigned KEY_SIZE>
struct Rec
{
	static const auto _REC_SIZE = REC_SIZE;
	static const auto _KEY_SIZE = KEY_SIZE;
	uint64_t data[REC_SIZE];

	bool operator<(const Rec<REC_SIZE, KEY_SIZE>& rhs) const
	{
		for (int32_t i = KEY_SIZE - 1; i >= 0; --i)
			if (data[i] < rhs.data[i])
				return true;
			else if (data[i] > rhs.data[i])
				return false;
		return false;
	}

	bool operator==(const Rec<REC_SIZE, KEY_SIZE>& rhs) const
	{
		for (int32_t i = KEY_SIZE - 1; i >= 0; --i)
			if (data[i] != rhs.data[i])
				return false;
		return true;
	}

	template<typename DISTRIBUTION>
	void RandomFill(std::mt19937_64& gen, DISTRIBUTION& dis, uint64_t most_significant_uint64_mask)
	{
		for (int32_t i = KEY_SIZE - 1; i >= 0; --i)
			data[i] = dis(gen);

		data[KEY_SIZE - 1] &= most_significant_uint64_mask;
	}

	Rec<REC_SIZE, KEY_SIZE> mask(const Rec& rhs) const
	{
		Rec<REC_SIZE, KEY_SIZE> result(*this);
		for (int32_t i = KEY_SIZE - 1; i >= 0; --i)
			result.data[i] &= rhs.data[i];
		return result;
	}

};


template<unsigned REC_SIZE, unsigned KEY_SIZE>
struct DispatchKeySize
{
	static void Run(const Params& params)
	{
		auto key_size_uint64 = (params.key_size.val + 7) / 8;
		if (KEY_SIZE == key_size_uint64)
		{
			using T = Rec<REC_SIZE, KEY_SIZE>;
			RunExperiment<T>(params);
		}
		else
			DispatchKeySize<REC_SIZE, KEY_SIZE - 1>::Run(params);
	}
};

template<unsigned REC_SIZE>
struct DispatchKeySize<REC_SIZE, 1>
{
	static void Run(const Params& params)
	{
		using T = Rec<REC_SIZE, 1>;
		RunExperiment<T>(params);
	}
};

template<unsigned REC_SIZE>
struct DispatchRecSize
{
	static void Run(const Params& params)
	{
		auto rec_size_uint64 = params.rec_size.val / 8;
		if (rec_size_uint64 == REC_SIZE)
			DispatchKeySize<REC_SIZE, REC_SIZE>::Run(params);
		else
			DispatchRecSize<REC_SIZE - 1>::Run(params);
	}
};
template<>
struct DispatchRecSize<1>
{
	static void Run(const Params& params)
	{
		DispatchKeySize<1, 1>::Run(params);
	}
};

int main(int argc, char**argv)
{
	Params p(argc, argv);
	std::cerr << "UWAGA: to jest wersja in-place, ktora jest jeszcze niedokonczona i probuje byc PARADISEM!!!!\n";
	//std::cerr << "TODO: UWAGA: cale inline jest wylaczone w radulsie!!!!\n";
	std::cerr << "Configuration:\n";
	p.Print();
	DispatchRecSize<raduls::MAX_REC_SIZE_IN_BYTES / 8>::Run(p);
	return 0;
}
