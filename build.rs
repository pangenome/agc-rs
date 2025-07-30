//! Build script for agc‑rs
//! • Builds the vendored AGC static library (if AGC_DIR is not set).
//! • Compiles the C++ bridge with the same Homebrew GCC that will be used
//!   by rustc to link the final crate on macOS.
//! • Ensures all C++ symbols are resolved before final linking.

use std::{
    env,
    path::PathBuf,
    process::Command,
};

/// Locate a Homebrew GCC ≤ 13 (AGC rejects 14+) and return `(prefix, version)`.
#[cfg(target_os = "macos")]
fn detect_homebrew_gcc() -> Option<(String, String)> {
    for ver in ["13", "12", "11"] {
        let formula = format!("gcc@{ver}");
        if let Ok(out) = Command::new("brew")
            .args(["--prefix", &formula])
            .output()
        {
            if out.status.success() {
                let prefix = String::from_utf8_lossy(&out.stdout).trim().to_owned();
                if !prefix.is_empty() {
                    return Some((prefix, ver.to_owned()));
                }
            }
        }
    }
    None
}

fn main() {
    /* ──────────────────────────────────────────────────────────────── */
    /* 1. Build / locate AGC                                           */
    /* ──────────────────────────────────────────────────────────────── */
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let agc_root = env::var("AGC_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest_dir.join("agc"));

    if !agc_root.join("bin/libagc.a").exists() {
        println!("cargo:warning=Building vendored AGC …");

        let make_cmd = if cfg!(target_os = "macos") {
            // Check for gmake first
            if Command::new("gmake").arg("--version").output().is_ok() {
                "gmake"
            } else {
                "make"
            }
        } else {
            "make"
        };

        let mut make = Command::new(make_cmd);
        make.current_dir(&agc_root).arg("-j");

        #[cfg(target_os = "macos")]
        if let Some((prefix, ver)) = detect_homebrew_gcc() {
            println!("cargo:warning=Using Homebrew GCC {ver} at {prefix}");
            make.env("CC", format!("gcc-{ver}"))
                .env("CXX", format!("g++-{ver}"));
            if cfg!(target_arch = "aarch64") {
                make.env("PLATFORM", "arm8");
            }
        } else {
            panic!("Homebrew GCC 11-13 is required on macOS. Install with: brew install gcc@13");
        }

        if !make.status().expect("failed to execute make").success() {
            panic!("AGC build failed");
        }
    }

    /* ──────────────────────────────────────────────────────────────── */
    /* 2. Configure cxx‑build for bridge                               */
    /* ──────────────────────────────────────────────────────────────── */
    let mut bridge = cxx_build::bridge("src/lib.rs");
    bridge
        .file("src/agc_bridge.cpp")
        .include(&agc_root)
        .include(agc_root.join("src"))
        .include(agc_root.join("src/common"))
        .include(agc_root.join("src/core"))
        .include(agc_root.join("3rd_party"))
        .flag_if_supported("-std=c++20")
        .flag_if_supported("-fPIC");

    #[cfg(target_os = "macos")]
    {
        if let Some((prefix, ver)) = detect_homebrew_gcc() {
            // Set environment variables that cc-rs will respect
            // This ensures cxx-build uses GCC instead of clang
            env::set_var("CXX", format!("{prefix}/bin/g++-{ver}"));
            env::set_var("CC", format!("{prefix}/bin/gcc-{ver}"));
            
            // Also set these for good measure
            env::set_var("TARGET_CXX", format!("{prefix}/bin/g++-{ver}"));
            env::set_var("TARGET_CC", format!("{prefix}/bin/gcc-{ver}"));
            
            // Now configure the bridge - it should use GCC from env vars
            bridge.compiler(&format!("{prefix}/bin/g++-{ver}"));
            
            // Add ARM-specific flags to match AGC compilation
            if cfg!(target_arch = "aarch64") {
                bridge.flag("-march=armv8-a");
            }

            // Force static linking of ALL runtime libraries
            bridge.flag("-static-libgcc");
            bridge.flag("-static-libstdc++");
            
            // Add GCC's lib path for finding the static libraries
            bridge.flag(&format!("-L{prefix}/lib/gcc/{ver}"));
        }
    }

    #[cfg(not(target_os = "macos"))]
    {
        bridge
            .flag_if_supported("-static-libgcc")
            .flag_if_supported("-static-libstdc++");
    }

    bridge.compile("agc-bridge");

    /* ──────────────────────────────────────────────────────────────── */
    /* 3. Link configuration for macOS                                 */
    /* ──────────────────────────────────────────────────────────────── */
    #[cfg(target_os = "macos")]
    if let Some((prefix, ver)) = detect_homebrew_gcc() {
        // CRITICAL: We need to provide the GCC runtime libraries BEFORE the system ones
        // Add GCC lib directory with highest priority
        println!("cargo:rustc-link-search=native={prefix}/lib/gcc/{ver}");
        
        // Link libstdc++ statically by specifying the full path
        let libstdcxx_path = format!("{prefix}/lib/gcc/{ver}/libstdc++.a");
        if PathBuf::from(&libstdcxx_path).exists() {
            // Use whole-archive to ensure all symbols are included
            println!("cargo:rustc-link-arg=-Wl,-force_load,{libstdcxx_path}");
        } else {
            // Fallback to dynamic linking if static lib not found
            println!("cargo:rustc-link-lib=stdc++");
        }
        
        // Link GCC runtime libraries - check what actually exists
        let gcc_lib_path = PathBuf::from(&format!("{prefix}/lib/gcc/{ver}"));

        // Try to find and link libgcc_s
        if gcc_lib_path.join("libgcc_s.1.dylib").exists() {
            println!("cargo:rustc-link-lib=dylib=gcc_s.1");
        } else if gcc_lib_path.join("libgcc_s.dylib").exists() {
            println!("cargo:rustc-link-lib=dylib=gcc_s");
        }

        // For static libgcc, use the .a file if it exists
        if gcc_lib_path.join("libgcc.a").exists() {
            let libgcc_path = gcc_lib_path.join("libgcc.a");
            println!("cargo:rustc-link-arg=-Wl,-force_load,{}", libgcc_path.display());
        } else if gcc_lib_path.join("libgcc_eh.a").exists() {
            // On some systems, exception handling is in a separate library
            let libgcc_eh_path = gcc_lib_path.join("libgcc_eh.a");
            println!("cargo:rustc-link-arg=-Wl,-force_load,{}", libgcc_eh_path.display());
        }
        
        // IMPORTANT: Do NOT link against system libc++
        // The cxx crate will try to link it, but we override with our args
    }

    /* ──────────────────────────────────────────────────────────────── */
    /* 4. Link against AGC & dependencies                              */
    /* ──────────────────────────────────────────────────────────────── */
    println!("cargo:rustc-link-search=native={}", agc_root.join("bin").display());
    println!("cargo:rustc-link-lib=static=agc");

    println!(
        "cargo:rustc-link-search=native={}",
        agc_root.join("3rd_party/zstd/lib").display()
    );
    println!("cargo:rustc-link-lib=static=zstd");
    
    // Common system libraries
    println!("cargo:rustc-link-lib=z");
    println!("cargo:rustc-link-lib=pthread");
    
    // On non-macOS, link libstdc++ normally
    #[cfg(not(target_os = "macos"))]
    println!("cargo:rustc-link-lib=stdc++");

    /* ──────────────────────────────────────────────────────────────── */
    /* 5. Re‑run triggers                                              */
    /* ──────────────────────────────────────────────────────────────── */
    println!("cargo:rerun-if-env-changed=AGC_DIR");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/agc_bridge.cpp");
    println!("cargo:rerun-if-changed=src/agc_bridge.h");
}