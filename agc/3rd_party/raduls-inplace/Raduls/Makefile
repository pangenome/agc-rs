ifdef MSVC     # Avoid the MingW/Cygwin sections
    uname_S := Windows
	uname_M := "x86_64"
else                          # If uname not available => 'not'
    uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
	uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
endif

OBJS = raduls_impl.o exceptions.o

ifeq ($(PLATFORM), arm8)
$(info *** ARMv8 with NEON extensions ***)
	COMMON_FLAGS := -march=armv8-a 
	COMMON_DEFS := -DCOMPILE_FOR_NEON -DARCH_ARM
	OBJS += raduls_NEON.o sorting_network_NEON.o
else ifeq ($(PLATFORM), m1)
$(info *** Apple M1(or never) with NEON extensions ***)
	COMMON_FLAGS := -march=armv8.4-a 
	COMMON_DEFS := -DCOMPILE_FOR_NEON -DARCH_ARM
	OBJS += raduls_NEON.o sorting_network_NEON.o
else ifeq ($(PLATFORM), sse2)
$(info *** x86-64 with SSE2 extensions ***)
	COMMON_FLAGS := -msse2 -m64 
	COMMON_DEFS := -DCOMPILE_FOR_SSE2 -DARCH_X64
	OBJS += raduls_SSE2.o sorting_network_SSE2.o
else ifeq ($(PLATFORM), avx)
$(info *** x86-64 with AVX extensions ***)
	COMMON_FLAGS := -mavx -m64 
	COMMON_DEFS := -DCOMPILE_FOR_AVX -DARCH_X64
	OBJS += raduls_AVX.o sorting_network_AVX.o
else ifeq ($(PLATFORM), avx2)
$(info *** x86-64 with AVX2 extensions ***)
	COMMON_FLAGS := -mavx2 -m64 
	COMMON_DEFS := -DCOMPILE_FOR_AVX2 -DARCH_X64
	OBJS += raduls_AVX2.o sorting_network_AVX2.o
else ifeq ($(PLATFORM), native)
$(info *** native architecture ***)
	ifeq ($(uname_M),x86_64)
		ARCH_FLAGS := -m64
		COMMON_DEFS := -DCOMPILE_FOR_SSE2 -DCOMPILE_FOR_AVX -DCOMPILE_FOR_AVX2 -DARCH_X64
		OBJS += raduls_SSE2.o sorting_network_SSE2.o
		OBJS += raduls_AVX.o sorting_network_AVX.o
		OBJS += raduls_AVX2.o sorting_network_AVX2.o
	else
		ARCH_FLAGS := -march=armv8-a
		COMMON_DEFS := -DCOMPILE_FOR_NEON -DARCH_ARM
		OBJS += raduls_NEON.o sorting_network_NEON.o
	endif	
else
$(info *** default extensions ***)
	ifeq ($(uname_M),x86_64)
		ARCH_FLAGS := -m64
		COMMON_DEFS := -DCOMPILE_FOR_SSE2 -DCOMPILE_FOR_AVX -DCOMPILE_FOR_AVX2 -DARCH_X64
		OBJS += raduls_SSE2.o sorting_network_SSE2.o
		OBJS += raduls_AVX.o sorting_network_AVX.o
		OBJS += raduls_AVX2.o sorting_network_AVX2.o
	else
		ARCH_FLAGS := -march=armv8-a
		COMMON_DEFS := -DCOMPILE_FOR_NEON -DARCH_ARM
		OBJS += raduls_NEON.o sorting_network_NEON.o
	endif	
endif

ifeq ($(ADD_SSE2), true)
	COMMON_DEFS += -DCOMPILE_FOR_SSE2
	OBJS += raduls_SSE2.o sorting_network_SSE2.o
endif

ifeq ($(ADD_AVX), true)
	COMMON_DEFS += -DCOMPILE_FOR_AVX
	OBJS += raduls_AVX.o sorting_network_AVX.o
endif

ifeq ($(ADD_AVX2), true)
	COMMON_DEFS += -DCOMPILE_FOR_AVX2
	OBJS += raduls_AVX2.o sorting_network_AVX2.o
endif

ifdef MAX_REC_SIZE
	COMMON_DEFS += -DMAX_REC_SIZE=$(MAX_REC_SIZE)
endif


CFLAGS = -DNDEBUG -Wall -O3 $(COMMON_FLAGS) -static -fno-ipa-ra -fno-tree-vrp -fno-tree-pre $(COMMON_FLAGS) -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
#CFLAGS = -Wall -O3 -static -fno-ipa-ra -fno-tree-vrp -fno-tree-pre -m64 -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

KEY_SIZE_EQ_REC_SIZE ?= false
ifeq ($(KEY_SIZE_EQ_REC_SIZE), true)
	CFLAGS += -DDISPATCH_ONLY_REC_SIZE
endif
	
libraduls.a: $(OBJS)
	ar rcs $@ $^

excetions.o: exceptions.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

raduls_impl.o: raduls_impl.cpp
	$(CXX) $(CFLAGS) $(COMMON_DEFS) -c $< -o $@

raduls_AVX.o: raduls_AVX.cpp
	$(CXX) $(CFLAGS) $(COMMON_FLAGS) $(COMMON_DEFS) -c $< -o $@

raduls_AVX2.o: raduls_AVX2.cpp
	$(CXX) $(CFLAGS) $(COMMON_FLAGS) $(COMMON_DEFS) -c $< -o $@

raduls_SSE2.o: raduls_SSE2.cpp
	$(CXX) $(CFLAGS) $(COMMON_FLAGS) $(COMMON_DEFS) -c $< -o $@
	
raduls_NEON.o: raduls_NEON.cpp
	$(CXX) $(CFLAGS) $(COMMON_FLAGS) $(COMMON_DEFS) -c $< -o $@

sorting_network_AVX.o: sorting_network_AVX.cpp
	$(CXX) -static -O1 $(COMMON_FLAGS) $(COMMON_DEFS) -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -c $< -o $@

sorting_network_AVX2.o: sorting_network_AVX2.cpp
	$(CXX) -static -O1 $(COMMON_FLAGS) $(COMMON_DEFS) -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -c $< -o $@

sorting_network_SSE2.o: sorting_network_SSE2.cpp
	$(CXX) -static -O1 $(COMMON_FLAGS) $(COMMON_DEFS) -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -c $< -o $@

sorting_network_NEON.o: sorting_network_NEON.cpp
	$(CXX) -static -O1 $(COMMON_FLAGS) $(COMMON_DEFS) -std=c++14 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -c $< -o $@

clean:
	-rm *.o
	-rm libraduls.a
