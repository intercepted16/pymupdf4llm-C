use std::env;
use std::path::PathBuf;
use cmake::Config;

fn main() {
    let crate_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let c_src_dir = crate_dir.join("c_src");
    let project_root = crate_dir.parent().unwrap();
    let mupdf_lib_path = project_root.join("lib").join("mupdf");

    // Build using cmake crate
    let dst = Config::new(&c_src_dir)
        .profile("Release")
        .define("MUPDF_LIB_DIR", mupdf_lib_path.to_str().unwrap())
        .define("PROJECT_ROOT_DIR", project_root.to_str().unwrap())
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
