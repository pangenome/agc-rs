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

This crate includes AGC as a git submodule and will build it automatically. Just run:

```bash
cargo build
```

The build process will:
1. Initialize the AGC submodule if needed
2. Build the AGC library if not already built
3. Link against the built library

### Alternative build methods

If you have AGC installed elsewhere, you can:

1. Use a system-wide installation (if installed to `/usr/local`)
2. Set the `AGC_DIR` environment variable:
   ```bash
   export AGC_DIR=/path/to/agc
   cargo build
   ```

## Requirements

- C++20 compatible compiler
- CMake
- zstd library

## License

MIT License (same as AGC)