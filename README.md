# agc-rs

Rust bindings for [AGC (Assembled Genomes Compressor)](https://github.com/refresh-bio/agc).

## Overview

This crate provides safe Rust bindings to the AGC C++ library, allowing you to read compressed genome collections from Rust programs.

## Features

- Read access to AGC archives
- List samples and contigs
- Fetch sequence regions
- Thread-safe operations

## Usage

```rust
use agc_rs::AGCFile;

let mut agc = AGCFile::new();
agc.open("genomes.agc", true)?;

// List all samples
let samples = agc.list_samples();

// Get a sequence region
let sequence = agc.get_contig_sequence("sample1", "chr1", 100, 200)?;
```

## Building

This crate requires the AGC library to be installed. You can either:

1. Install AGC system-wide:
```bash
git clone --recurse-submodules https://github.com/refresh-bio/agc
cd agc && make && sudo make install
```

2. Or set the `AGC_DIR` environment variable to point to the AGC source directory:
```bash
export AGC_DIR=/path/to/agc
```

## Requirements

- C++20 compatible compiler
- CMake
- zstd library

## License

MIT License (same as AGC)