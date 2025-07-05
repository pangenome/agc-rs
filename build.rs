use std::env;
use std::path::PathBuf;

fn main() {
    let _out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    
    // Get AGC source directory from environment or use default
    let agc_src = if let Ok(agc_dir) = env::var("AGC_DIR") {
        PathBuf::from(agc_dir)
    } else if PathBuf::from("/usr/local/include/agc").exists() {
        // System installation
        PathBuf::from("/usr/local")
    } else {
        // Fallback to git submodule
        PathBuf::from("agc")
    };
    
    // Build the C++ bridge code
    cxx_build::bridge("src/lib.rs")
        .file("src/agc_bridge.cpp")
        .include(&agc_src)  // Add AGC root for relative includes
        .include(&agc_src.join("src"))
        .include(&agc_src.join("src/common"))
        .include(&agc_src.join("src/core"))
        .include(&agc_src.join("3rd_party"))  // Add 3rd_party for zstd includes
        .flag_if_supported("-std=c++20")
        .compile("agc-bridge");

    // Link to AGC libraries
    println!("cargo:rustc-link-search=native={}", agc_src.join("bin").display());
    println!("cargo:rustc-link-lib=static=agc");
    
    // Link AGC's third-party libraries
    println!("cargo:rustc-link-search=native={}", agc_src.join("3rd_party/zstd/lib").display());
    println!("cargo:rustc-link-lib=static=zstd");
    
    // Link standard C++ library
    println!("cargo:rustc-link-lib=stdc++");
    
    // Link zlib (required by AGC)
    println!("cargo:rustc-link-lib=z");
    
    // Link pthread (required by AGC)
    println!("cargo:rustc-link-lib=pthread");
    
    // Rebuild if the bridge changes
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/agc_bridge.cpp");
}