#include "defs.h"

#ifdef COMPILE_FOR_AVX2

#define RunWrapper RunWrapperAVX2
#define NAMESPACE_NAME AVX2

#include "raduls_impl.h"

namespace raduls
{
	void CInstrSetAVX2::Run(const SortParams& p)
	{		
		AVX2::RunWrapperAVX2(p);
	}
}

#endif