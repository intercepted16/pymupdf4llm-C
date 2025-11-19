use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    // Root of the repository
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .parent()
        .unwrap()
        .to_path_buf();

    // Build directory
    let build_dir = root.join("build");
    fs::create_dir_all(&build_dir).expect("failed to create build dir");

    // Source directory
    let source_dir = &root;

    // Build type (Debug/Release)
    let cmake_build_type = env::var("CMAKE_BUILD_TYPE").unwrap_or_else(|_| "Release".to_string());

    // Environment variables for CMake
    let mut envs = env::vars().collect::<Vec<_>>();
    if env::var("CMAKE_BUILD_PARALLEL_LEVEL").is_err() {
        envs.push((
            "CMAKE_BUILD_PARALLEL_LEVEL".to_string(),
            format!("{}", num_cpus::get()),
        ));
    }

    // Optional: check cmake version
    Command::new("cmake")
        .arg("--version")
        .envs(envs.iter().cloned())
        .status()
        .expect("failed to run cmake --version");

    // Configure
    Command::new("cmake")
        .arg(source_dir)
        .arg(format!("-DCMAKE_BUILD_TYPE={}", cmake_build_type))
        .current_dir(&build_dir)
        .envs(envs.iter().cloned())
        .status()
        .expect("cmake configuration failed");

    // Build target
    Command::new("cmake")
        .arg("--build")
        .arg(".")
        .arg("--target")
        .arg("tomd")
        .current_dir(&build_dir)
        .envs(envs.iter().cloned())
        .status()
        .expect("cmake build failed");

    // Locate built library
    let lib_output = build_dir.join("lib");
    let produced = find_library(&lib_output).expect("failed to locate built tomd library");

    // Copy to OUT_DIR
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let target_dir = out_dir.join("lib");
    fs::create_dir_all(&target_dir).unwrap();
    let lib_path = target_dir.join(produced.file_name().unwrap());
    fs::copy(&produced, &lib_path).expect("failed to copy library");

    // Tell Rust/Cargo where to find the library
    println!("cargo:rustc-link-search=native={}", target_dir.display());

    // Link the library
    println!("cargo:rustc-link-lib=tomd");

    // Optional: trigger rebuild if any C files change
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
}

// Finds the library file in the given directory
fn find_library(search_dir: &Path) -> Option<PathBuf> {
    let suffix = match env::var("CARGO_CFG_TARGET_OS").as_deref() {
        Ok("linux") => ".so",
        Ok("macos") => ".dylib",
        Ok("windows") => ".dll",
        _ => ".so",
    };

    let entries = search_dir.read_dir().ok()?;
    let mut matches = entries
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| {
            let name = p.file_name().unwrap().to_string_lossy();
            p.is_file() && name.contains("tomd") && name.ends_with(suffix)
        })
        .collect::<Vec<_>>();

    matches.sort();
    matches.into_iter().next()
}
