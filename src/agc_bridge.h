#pragma once

#include <memory>
#include <string>
#include <vector>
#include "rust/cxx.h"
#include "common/agc_decompressor_lib.h"

class AGCDecompressor {
public:
    AGCDecompressor();
    std::unique_ptr<CAGCDecompressorLibrary> impl;
};

std::unique_ptr<AGCDecompressor> create_agc_decompressor();
bool open_archive(AGCDecompressor& decompressor, rust::Str archive_path, bool prefetch);
bool close_archive(AGCDecompressor& decompressor);
bool is_opened(const AGCDecompressor& decompressor);

rust::String get_contig_string(
    AGCDecompressor& decompressor,
    rust::Str sample_name,
    rust::Str contig_name,
    int32_t start,
    int32_t end
);

int64_t get_contig_length(
    const AGCDecompressor& decompressor,
    rust::Str sample_name,
    rust::Str contig_name
);

rust::Vec<rust::String> list_samples(const AGCDecompressor& decompressor);
rust::Vec<rust::String> list_contigs(const AGCDecompressor& decompressor, rust::Str sample_name);
int32_t get_no_samples(const AGCDecompressor& decompressor);
int32_t get_no_contigs(const AGCDecompressor& decompressor, rust::Str sample_name);