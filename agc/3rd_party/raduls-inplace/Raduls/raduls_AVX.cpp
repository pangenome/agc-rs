#include "defs.h"
#ifdef COMPILE_FOR_AVX

#define RunWrapper RunWrapperAVX
#define NAMESPACE_NAME AVX

#include "raduls_impl.h"

namespace raduls
{
	void CInstrSetAVX::Run(const SortParams& p)
	{		
		AVX::RunWrapperAVX(p);
	}
}
#endif