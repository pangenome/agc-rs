#include "defs.h"

#ifdef COMPILE_FOR_NEON
#define NAMESPACE_NAME NEON

#include "sorting_network_impl.h"

namespace raduls
{
	namespace NAMESPACE_NAME
	{
		namespace small_sort
		{
			template struct SortingNetwork<Record<1, 1>, SwapLowerGreater<Record<1, 1>, LessFirstNotEqual<Record<1, 1>>>>;
			template struct SortingNetwork<Record<2, 1>, SwapLowerGreater<Record<2, 1>, LessFirstLower<Record<2, 1>>>>;
			template struct SortingNetwork<Record<2, 2>, IntrSwapper<Record<2, 2>, LS_uint64_lower<Record<2, 2>>>>;
		}
	}
}
#endif
