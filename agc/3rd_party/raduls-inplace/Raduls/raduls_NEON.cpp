#include "defs.h"

#ifdef COMPILE_FOR_NEON

#define RunWrapper RunWrapperNEON
#define NAMESPACE_NAME NEON

#include "raduls_impl.h"

namespace raduls
{
	void CInstrSetNEON::Run(const SortParams& p)
	{
		NEON::RunWrapperNEON(p);
	}
}

#endif