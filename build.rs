//! Build script for agc‑rs
//! • Builds the vendored AGC static library (if AGC_DIR is not set).
//! • Compiles the C++ bridge with the same Homebrew GCC that will be used
//!   by rustc to link the final crate on macOS.
//! • Adds the correct search paths and runtime libs.  No `-lgcc` is emitted
//!   because Homebrew provides only `libgcc_s.1.dylib` on macOS.

use std::{
    env,
    path::PathBuf,
    process::{Command, Stdio},
};

/// Locate a Homebrew GCC ≤ 13 (AGC rejects 14+) and return `(prefix, version)`.
#[cfg(target_os = "macos")]
fn detect_homebrew_gcc() -> Option<(String, String)> {
    for ver in ["13", "12", "11", "10"] {
        let formula = format!("gcc@{ver}");
        if Command::new("brew")
            .args(["--prefix", &formula])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
        {
            let out = Command::new("brew")
                .args(["--prefix", &formula])
                .output()
                .ok()?;
            let prefix = String::from_utf8_lossy(&out.stdout).trim().to_owned();
            return Some((prefix, ver.to_owned()));
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

        let make_cmd = if cfg!(target_os = "macos")
            && Command::new("gmake").arg("--version").output().is_ok()
        {
            "gmake"
        } else {
            "make"
        };

        let mut make = Command::new(make_cmd);
        make.current_dir(&agc_root).arg("-j");

        #[cfg(target_os = "macos")]
        if let Some((_prefix, ver)) = detect_homebrew_gcc() {
            make.env("CC", format!("gcc-{ver}"))
                .env("CXX", format!("g++-{ver}"));
            if cfg!(target_arch = "aarch64") {
                make.env("PLATFORM", "arm8");
            }
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
        .flag_if_supported("-fPIC")
        .flag_if_supported("-static-libgcc")
        .flag_if_supported("-static-libstdc++");

    #[cfg(target_os = "macos")]
    {
        if let Some((prefix, ver)) = detect_homebrew_gcc() {
            // Compile the bridge with g++
            bridge.compiler(&format!("g++-{ver}"));
            
            // Add ARM-specific flags to match AGC compilation
            if target_arch == "aarch64" {
                bridge.flag("-march=armv8-a");
            }

            // Add library search paths
            println!("cargo:rustc-link-search=native={prefix}/lib/gcc/{ver}");
            
            // Link the runtime libraries
            println!("cargo:rustc-link-lib=gcc_s.1");
            println!("cargo:rustc-link-lib=atomic");
            
            // DO NOT set CARGO_TARGET_*_LINKER or rustc-link-arg=-C linker=g++
            // Let rustc use the default system linker
        }
    }

    bridge.compile("agc-bridge");

    /* ──────────────────────────────────────────────────────────────── */
    /* 3. Link against AGC & friends                                   */
    /* ──────────────────────────────────────────────────────────────── */
    println!("cargo:rustc-link-search=native={}", agc_root.join("bin").display());
    println!("cargo:rustc-link-lib=static=agc");

    println!(
        "cargo:rustc-link-search=native={}",
        agc_root.join("3rd_party/zstd/lib").display()
    );
    println!("cargo:rustc-link-lib=static=zstd");
    println!("cargo:rustc-link-lib=stdc++");
    println!("cargo:rustc-link-lib=z");
    println!("cargo:rustc-link-lib=pthread");

    /* ──────────────────────────────────────────────────────────────── */
    /* 4. Re‑run triggers                                              */
    /* ──────────────────────────────────────────────────────────────── */
    println!("cargo:rerun-if-env-changed=AGC_DIR");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/agc_bridge.cpp");
}
