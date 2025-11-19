use pymupdf4llm_c::to_json;
use std::env;
use std::path::PathBuf;
use std::process;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 || args.len() > 3 {
        eprintln!("Usage: {} <input.pdf> [output_dir]", args[0]);
        process::exit(1);
    }

    let pdf_path = &args[1];
    let output_dir: Option<PathBuf> = args.get(2).map(PathBuf::from);

    match to_json(pdf_path, output_dir.as_deref()) {
        Ok(paths) => {
            println!("Extracted {} JSON files:", paths.len());
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            process::exit(1);
        }
    }
}
