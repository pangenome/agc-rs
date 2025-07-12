use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let _out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    // Get AGC source directory from environment or use default
    let agc_src = if let Ok(agc_dir) = env::var("AGC_DIR") {
        PathBuf::from(agc_dir)
    } else if PathBuf::from("/usr/local/include/agc").exists() {
        // System installation
        PathBuf::from("/usr/local")
    } else {
        // Use vendored source - use absolute path from manifest directory
        let vendored_path = manifest_dir.join("agc");

        // Check if libagc.a exists, if not, try to build it
        if !vendored_path.join("bin/libagc.a").exists() {
            println!("cargo:warning=Building AGC library...");

            // Check if vendored source exists
            if !vendored_path.join("makefile").exists() {
                panic!(
                    "AGC source not found! Please run ./vendor-agc.sh to vendor the AGC source."
                );
            }

            // Determine which make command to use
            let make_cmd = if cfg!(target_os = "macos") {
                // Try gmake first, fall back to make
                if Command::new("gmake").arg("--version").output().is_ok() {
                    "gmake"
                } else {
                    "make"
                }
            } else {
                "make"
            };

            // Build AGC
            let mut cmd = Command::new(make_cmd);
            cmd.current_dir(&vendored_path).args(["-j"]);

            // Pass through CXX environment variable if set (critical for macOS)
            if let Ok(cxx) = env::var("CXX") {
                cmd.env("CXX", cxx);
            }

            // Pass through CC environment variable if set
            if let Ok(cc) = env::var("CC") {
                cmd.env("CC", cc);
            }

            // Pass through PLATFORM if set (for ARM builds)
            if let Ok(platform) = env::var("PLATFORM") {
                cmd.env("PLATFORM", platform);
            }

            let status = cmd.status().expect("Failed to build AGC");

            if !status.success() {
                panic!("Failed to build AGC library");
            }
        }

        vendored_path
    };

    // Build the C++ bridge code
    cxx_build::bridge("src/lib.rs")
        .file("src/agc_bridge.cpp")
        .include(&agc_src) // Add AGC root for relative includes
        .include(agc_src.join("src"))
        .include(agc_src.join("src/common"))
        .include(agc_src.join("src/core"))
        .include(agc_src.join("3rd_party")) // Add 3rd_party for zstd includes
        .flag_if_supported("-std=c++20")
        .compile("agc-bridge");

    // Link to AGC libraries
    println!(
        "cargo:rustc-link-search=native={}",
        agc_src.join("bin").display()
    );
    println!("cargo:rustc-link-lib=static=agc");

    // Link AGC's third-party libraries
    println!(
        "cargo:rustc-link-search=native={}",
        agc_src.join("3rd_party/zstd/lib").display()
    );
    println!("cargo:rustc-link-lib=static=zstd");

    // Link standard C++ library
    println!("cargo:rustc-link-lib=stdc++");

    // Link zlib (required by AGC)
    println!("cargo:rustc-link-lib=z");

    // Link pthread (required by AGC)
    println!("cargo:rustc-link-lib=pthread");

    // On macOS with ARM64, we need GCC runtime libraries
    if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        // Add g++ library search path
        if let Ok(gcc_path) = Command::new("brew").args(["--prefix", "gcc@11"]).output() {
            if gcc_path.status.success() {
                let prefix = String::from_utf8_lossy(&gcc_path.stdout).trim().to_string();
                println!("cargo:rustc-link-search=native={prefix}/lib/gcc/11");
            }
        }

        // Link GCC runtime libraries
        println!("cargo:rustc-link-lib=gcc_s.1");
        println!("cargo:rustc-link-lib=gcc");
    }

    // Rebuild if the bridge changes
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/agc_bridge.cpp");
    println!("cargo:rerun-if-env-changed=CXX");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=PLATFORM");
}
