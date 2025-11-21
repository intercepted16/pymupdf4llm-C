use std::env;
use std::path::PathBuf;
use cmake::Config;

fn main() {
    let crate_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let c_src_dir = crate_dir.join("c_src");

    // Build using cmake crate
    let dst = Config::new(&c_src_dir)
        .profile("Release")
        .build_target("tomd")
        .build();

    // dst points to CMAKE_INSTALL_PREFIX
    // The actual library is in build/lib/ subdirectory
    let lib_dir = dst.join("build").join("lib");

    // Tell Cargo where the library lives
    println!("cargo:rustc-link-search=native={}", lib_dir.display());

    // Link dynamically
    println!("cargo:rustc-link-lib=dylib=tomd");

    // Rebuild if anything changes
    println!("cargo:rerun-if-changed={}", c_src_dir.display());
}
