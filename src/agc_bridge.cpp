#include "agc_bridge.h"
#include "agc-rs/src/lib.rs.h"
#include <memory>
#include <stdexcept>

AGCDecompressor::AGCDecompressor() : impl(std::make_unique<CAGCDecompressorLibrary>(false)) {}

std::unique_ptr<AGCDecompressor> create_agc_decompressor() {
    return std::make_unique<AGCDecompressor>();
}

bool open_archive(AGCDecompressor& decompressor, rust::Str archive_path, bool prefetch) {
    std::string path(archive_path);
    return decompressor.impl->Open(path, prefetch);
}

bool close_archive(AGCDecompressor& decompressor) {
    return decompressor.impl->Close();
}

bool is_opened(const AGCDecompressor& decompressor) {
    return decompressor.impl->IsOpened();
}

rust::String get_contig_string(
    AGCDecompressor& decompressor,
    rust::Str sample_name,
    rust::Str contig_name,
    int32_t start,
    int32_t end
) {
    std::string sample(sample_name);
    std::string contig(contig_name);
    std::string result;
    
    int ret = decompressor.impl->GetContigString(sample, contig, start, end, result);
    if (ret != 0) {
        throw std::runtime_error("Failed to get contig string");
    }
    
    return rust::String(result);
}

int64_t get_contig_length(
    const AGCDecompressor& decompressor,
    rust::Str sample_name,
    rust::Str contig_name
) {
    std::string sample(sample_name);
    std::string contig(contig_name);
    return decompressor.impl->GetContigLength(sample, contig);
}

rust::Vec<rust::String> list_samples(const AGCDecompressor& decompressor) {
    std::vector<std::string> samples;
    const_cast<CAGCDecompressorLibrary*>(decompressor.impl.get())->ListSamples(samples);
    
    rust::Vec<rust::String> result;
    for (const auto& sample : samples) {
        result.push_back(rust::String(sample));
    }
    return result;
}

rust::Vec<rust::String> list_contigs(const AGCDecompressor& decompressor, rust::Str sample_name) {
    std::string sample(sample_name);
    std::vector<std::string> contigs;
    const_cast<CAGCDecompressorLibrary*>(decompressor.impl.get())->ListContigs(sample, contigs);
    
    rust::Vec<rust::String> result;
    for (const auto& contig : contigs) {
        result.push_back(rust::String(contig));
    }
    return result;
}

int32_t get_no_samples(const AGCDecompressor& decompressor) {
    return decompressor.impl->GetNoSamples();
}

int32_t get_no_contigs(const AGCDecompressor& decompressor, rust::Str sample_name) {
    std::string sample(sample_name);
    return decompressor.impl->GetNoContigs(sample);
}