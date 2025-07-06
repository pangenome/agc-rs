all: raduls

ifdef MSVC     # Avoid the MingW/Cygwin sections
    uname_S := Windows
	uname_M := "x86_64"
else                          # If uname not available => 'not'
    uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
	uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
endif

BIN_DIR = bin
MAIN_DIR = Example
RADULS_DIR=Raduls

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
		COMMON_DEFS := -DCOMPILE_FOR_SSE2 -DCOMPILE_FOR_AVX -DCOMPILE_FOR_AVX2
		OBJS += raduls_SSE2.o sorting_network_SSE2.o
		OBJS += raduls_AVX.o sorting_network_AVX.o
		OBJS += raduls_AVX2.o sorting_network_AVX2.o
	else
		ARCH_FLAGS := -march=armv8-a
		COMMON_DEFS := -DCOMPILE_FOR_NEON
		OBJS += raduls_NEON.o sorting_network_NEON.o
	endif	
else
$(info *** default extensions ***)
	ifeq ($(uname_M),x86_64)
		ARCH_FLAGS := -m64
		COMMON_DEFS := -DCOMPILE_FOR_SSE2 -DCOMPILE_FOR_AVX -DCOMPILE_FOR_AVX2
		OBJS += raduls_SSE2.o sorting_network_SSE2.o
		OBJS += raduls_AVX.o sorting_network_AVX.o
		OBJS += raduls_AVX2.o sorting_network_AVX2.o
	else
		ARCH_FLAGS := -march=armv8-a
		COMMON_DEFS := -DCOMPILE_FOR_NEON
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

CFLAGS	= -DNDEBUG -I$(RADULS_DIR) -fopenmp -Wall -static -O3 -fno-ipa-ra -fno-tree-vrp -fno-tree-pre $(COMMON_FLAGS) -std=c++17 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
#CFLAGS	= -I$(RADULS_DIR) -fopenmp -Wall -static -O3 -fno-ipa-ra -fno-tree-vrp -fno-tree-pre -m64 -std=c++17 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

ifeq ($(uname_S), Darwin)
	CLINK	= -lm $(COMMON_FLAGS) -fopenmp -O3 -fno-ipa-ra -fno-tree-vrp -fno-tree-pre  -std=c++17 -lpthread
else
	CLINK	= -lm $(COMMON_FLAGS) -fopenmp -static -O3 -fno-ipa-ra -fno-tree-vrp -fno-tree-pre  -std=c++17 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
endif


OBJS = \
$(MAIN_DIR)/main.o \

$(OBJS): %.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

$(OBJS): %.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

raduls: $(OBJS) $(RADULS_DIR)/libraduls.a
	mkdir -p $(BIN_DIR)
	$(CXX) $(CLINK) -o $(BIN_DIR)/$@ $^

$(RADULS_DIR)/libraduls.a:
	cd $(RADULS_DIR) && $(MAKE)

clean:	
	-rm $(MAIN_DIR)/*.o
	-rm -rf bin
	cd $(RADULS_DIR) && $(MAKE) clean

all: raduls
