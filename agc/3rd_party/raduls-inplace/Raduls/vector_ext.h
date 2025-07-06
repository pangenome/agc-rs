#pragma once

#if defined(ARCH_X64)
#include <emmintrin.h>
#include <immintrin.h>
#elif defined(ARCH_ARM)
#include <arm_neon.h>
#include <atomic>
// https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
// https://github.com/DLTcollab/sse2neon/blob/master/sse2neon.h
#endif

FORCE_INLINE void our_fence(void)
{
#if defined(ARCH_X64)
    _mm_sfence();
#elif defined(ARCH_ARM)
    do {
        \
            __asm__ __volatile__("" ::: "memory"); \
            (void)0;                              \
    } while (0);

#if defined(__STDC_NO_ATOMICS__)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
#endif
}

