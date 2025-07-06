# RADULS-inplace-dev
The repository contains our experimental implementation of an in-place version of RADULS [1,2].
It combines the ideas used in RADULS as well as in PARADIS [3].

The library is at an experimental stage.
Thus, API is likely to be changed.
This is also why the library <b>intentionally lacks</b> documentation.

# Compilation
The library supports various OS/hardware configurations.

### x64, Windows
Visual Studio 2022 solution is provided. Just open and compile.

### x64, linux
Makefile is provided. 
G++ 10 or newer is necessary.
For compilation use:
```
make PLATFORM=avx2                               # will be compiled with AVX2 extensions
make PLATFORM=avx                                # will be compiled with AVX2 extensions
make PLATFORM=sse2                               # will be compiled with SSE2 extensions
make PLATFORM=sse2 ADD_AVX=true                  # will be compiled with SSE2 and AVX extensions (selection at runtime)
make PLATFORM=sse2 ADD_AVX=true ADD_AVX2=true    # will be compiled with SSE2, AVX, AVX2 extensions (selection at runtime)
```

### arm, linux
Makefile is provided. 
G++ 10 or newer is necessary.
For compilation use:
```
make PLATFORM=arm8                               # will be compiled with NEON extensions
```

### M1 (arm), MacOS
Makefile is provided. 
G++ 10 or newer is necessary.
For compilation use (be sure that g++ is not clang or explicitly set CXX):
```
make PLATFORM=m1 CXX=g++-10                      # will be compiled with NEON extensions
```

### x64, MacOS
Makefile is provided. 
G++ 10 or newer is necessary.
For compilation use (be sure that g++ is not clang or explicitly set CXX):
```
make PLATFORM=avx2 CXX=g++-10                    # will be compiled with AVX2 extensions
```



### Max. rec size
By default allows only 8-byte records. 
If necessary to increase, define MAX_REC_SIZE::
```
make PLATFORM=neon MAX_REC_SIZE=16               # will be compiled with NEON extensions and REC_SIZES=8,16
```





# References
1. Kokot M., Deorowicz S., Debudaj-Grabysz A. (2017) Sorting Data on Ultra-Large Scale with RADULS. In: Kozielski S., Mrozek D., Kasprowski P., Małysiak-Mrozek B., Kostrzewa D. (eds) Beyond Databases, Architectures and Structures. Towards Efficient Solutions for Data Analysis and Knowledge Representation. BDAS 2017. Communications in Computer and Information Science, vol 716. Springer, Cham
2. Kokot M., Deorowicz S., Długosz M. (2018) Even Faster Sorting of (Not Only) Integers. In: Gruca A., Czachórski T., Harezlak K., Kozielski S., Piotrowska A. (eds) Man-Machine Interactions 5. ICMMI 2017. Advances in Intelligent Systems and Computing, vol 659. Springer, Cham
3. Cho, M., Brand, D., Bordawekar, R., Finkler, U., Kulandaisamy, V., & Puri, R. (2015). PARADIS: an efficient parallel algorithm for in-place radix sort. Proceedings of the VLDB Endowment, 8(12), 1518–1529. https://doi.org/10.14778/2824032.2824050
