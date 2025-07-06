#pragma once
#include <cstdint>
#include <cstddef>

#ifdef __GNUG__
#define FORCE_INLINE inline __attribute__((always_inline))
using __int64 = long long int;
#else
#define FORCE_INLINE __forceinline
#endif

#define MINIMAL_REQUIRED_INSTR_SET "SSE2"

//In some cases (like KMC k-mer counter) the KEY_SIZE in uint64 is always the same as REC_SIZE in uint64 so to keep library smaller one may compile with the following define uncommented
#define DISPATCH_ONLY_REC_SIZE //TODO: zakomentowac

//uncoment below to allow compile also with -mavx, -mavx2
//raduls will automatically detect which instruction set is supported on a givent machine during execution
//From current experiments it seems that sse2 is enaugh, so those defines are commented because its affects library size in a significant way

#ifdef _MSC_VER
#define COMPILE_FOR_SSE2
#define COMPILE_FOR_AVX
#define COMPILE_FOR_AVX2
#else
// defined in makefile
#endif

//#define COMPILE_FOR_NEON
