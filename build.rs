use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/agc_bridge.cpp");
    println!("cargo:rerun-if-changed=src/agc_bridge.h");
    println!("cargo:rerun-if-changed=agc/");
    
    // Initialize git submodules if needed
    init_submodules();
    
    // Build AGC library
    build_agc();
    
    // Set up platform-specific configurations
    if cfg!(target_os = "macos") {
        setup_macos_gcc_linking();
    }
    
    // Build the C++ bridge using cxx-build
    build_cxx_bridge();
}

fn init_submodules() {
    println!("cargo:warning=Initializing submodules...");
    
    let output = Command::new("git")
        .args(&["submodule", "update", "--init", "--recursive"])
        .output()
        .expect("Failed to execute git submodule command");
    
    if !output.status.success() {
        panic!(
            "Failed to initialize submodules: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }
}

fn build_agc() {
    println!("cargo:warning=Building AGC library...");
    
    let agc_dir = PathBuf::from("agc");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    
    // Set up environment for macOS
    if cfg!(target_os = "macos") {
        env::set_var("CXX", "g++-11");
        env::set_var("CC", "gcc-11");
        env::set_var("PLATFORM", "arm8");
    }
    
    // Clean any previous build
    let _ = Command::new("make")
        .current_dir(&agc_dir)
        .arg("clean")
        .status();
    
    // Build AGC
    let make_cmd = if cfg!(target_os = "macos") {
        // Try gmake first (GNU make), fall back to make
        if Command::new("gmake").arg("--version").status().is_ok() {
            "gmake"
        } else {
            "make"
        }
    } else {
        "make"
    };
    
    let output = Command::new(make_cmd)
        .current_dir(&agc_dir)
        .env("CXX", if cfg!(target_os = "macos") { "g++-11" } else { "g++" })
        .env("CC", if cfg!(target_os = "macos") { "gcc-11" } else { "gcc" })
        .output()
        .expect("Failed to execute make command");
    
    if !output.status.success() {
        eprintln!("stdout: {}", String::from_utf8_lossy(&output.stdout));
        eprintln!("stderr: {}", String::from_utf8_lossy(&output.stderr));
        panic!("Failed to build AGC library");
    }
    
    // Set up library search paths
    println!("cargo:rustc-link-search=native={}", agc_dir.join("bin").display());
    println!("cargo:rustc-link-lib=static=agc");
    
    // Link zstd
    let zstd_path = agc_dir.join("3rd_party/zstd/lib");
    if zstd_path.exists() {
        println!("cargo:rustc-link-search=native={}", zstd_path.display());
        println!("cargo:rustc-link-lib=static=zstd");
    } else {
        // Fall back to system zstd
        println!("cargo:rustc-link-lib=zstd");
    }
    
    // Link other required libraries
    println!("cargo:rustc-link-lib=z");
    println!("cargo:rustc-link-lib=pthread");
}

fn setup_macos_gcc_linking() {
    println!("cargo:warning=Setting up macOS GCC linking...");
    
    // Common homebrew installation paths for gcc@11
    let possible_gcc_paths = vec![
        "/opt/homebrew/opt/gcc@11/lib/gcc/11",      // ARM64 Macs
        "/usr/local/opt/gcc@11/lib/gcc/11",         // Intel Macs
        "/opt/homebrew/opt/gcc@11/lib/gcc/current", // Alternative path
        "/usr/local/opt/gcc@11/lib/gcc/current",    // Alternative path
    ];
    
    // Find the first existing GCC path
    let gcc_lib_path = possible_gcc_paths
        .iter()
        .map(PathBuf::from)
        .find(|path| path.exists());
    
    if let Some(gcc_path) = gcc_lib_path {
        println!("cargo:rustc-link-search=native={}", gcc_path.display());
        
        // Also add the parent lib directory
        if let Some(parent) = gcc_path.parent() {
            println!("cargo:rustc-link-search=native={}", parent.display());
        }
    } else {
        // Try to find GCC using brew
        if let Ok(output) = Command::new("brew")
            .args(&["--prefix", "gcc@11"])
            .output()
        {
            if output.status.success() {
                let prefix = String::from_utf8_lossy(&output.stdout).trim().to_string();
                let gcc_lib_path = PathBuf::from(&prefix).join("lib/gcc/11");
                
                if gcc_lib_path.exists() {
                    println!("cargo:rustc-link-search=native={}", gcc_lib_path.display());
                    
                    // Also add the main lib directory
                    let lib_path = PathBuf::from(&prefix).join("lib");
                    println!("cargo:rustc-link-search=native={}", lib_path.display());
                }
            }
        }
        
        // Last resort: try to get the path from gcc itself
        if let Ok(output) = Command::new("gcc-11")
            .arg("-print-libgcc-file-name")
            .output()
        {
            if output.status.success() {
                let libgcc_path = String::from_utf8_lossy(&output.stdout);
                let libgcc_path = libgcc_path.trim();
                if let Some(dir) = PathBuf::from(libgcc_path).parent() {
                    println!("cargo:rustc-link-search=native={}", dir.display());
                }
            }
        }
    }
    
    // Link GCC runtime libraries
    println!("cargo:rustc-link-lib=gcc_s.1");
    println!("cargo:rustc-link-lib=gcc");
    
    // Use libc++ on macOS
    println!("cargo:rustc-link-lib=c++");
}

fn build_cxx_bridge() {
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    
    // Configure cxx-build
    let mut build = cxx_build::bridge("src/lib.rs");
    
    build
        .file("src/agc_bridge.cpp")
        .include("src")
        .include("agc/src")
        .include(&out_dir);
    
    // Set C++ standard
    build.flag_if_supported("-std=c++17");
    
    // Platform-specific flags
    if cfg!(target_os = "macos") {
        build.compiler("g++-11");
        build.flag("-stdlib=libc++");
        
        // Add GCC include paths if needed
        if let Ok(output) = Command::new("g++-11")
            .arg("-print-search-dirs")
            .output()
        {
            if output.status.success() {
                // Parse and add include directories if needed
            }
        }
    }
    
    // Add warnings
    build
        .flag_if_supported("-Wall")
        .flag_if_supported("-Wextra");
    
    // Compile the bridge
    build.compile("agc-bridge");
    
    // Tell cargo to link the standard C++ library
    if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=stdc++");
    }
}