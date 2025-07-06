#include "defs.h"

#ifdef COMPILE_FOR_SSE2

#define RunWrapper RunWrapperSSE2
#define NAMESPACE_NAME SSE2

#include "raduls_impl.h"

namespace raduls
{
	void CInstrSetSSE2::Run(const SortParams& p)
	{
		SSE2::RunWrapperSSE2(p);
	}
}
#endif