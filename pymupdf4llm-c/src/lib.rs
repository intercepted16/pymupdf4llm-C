use libc::c_void;
use serde::Deserialize; // Required for the custom struct
use std::ffi::{CStr, CString, NulError};
use std::fmt;
use std::fs;
use std::io;
use std::os::raw::c_char;
use std::path::{Path, PathBuf};
use std::str::Utf8Error;

// --- FFI to C library ---
unsafe extern "C" {
    fn pdf_to_json(pdf_path: *const c_char, output_dir: *const c_char) -> i32;
    fn page_to_json_string(pdf_path: *const c_char, page_number: i32) -> *mut c_char;
    fn free(ptr: *mut c_void);
}

/// Rust-friendly error type
#[derive(Debug)]
pub enum PdfError {
    Nul(NulError),
    CError(i32),
    Io(io::Error),
    Utf8(Utf8Error),
    Json(serde_json::Error),
    NullResult,
    MissingInput(PathBuf),
    PageNumberOverflow,
}

impl From<NulError> for PdfError {
    fn from(err: NulError) -> Self {
        PdfError::Nul(err)
    }
}

impl From<io::Error> for PdfError {
    fn from(err: io::Error) -> Self {
        PdfError::Io(err)
    }
}

impl From<Utf8Error> for PdfError {
    fn from(err: Utf8Error) -> Self {
        PdfError::Utf8(err)
    }
}

impl From<serde_json::Error> for PdfError {
    fn from(err: serde_json::Error) -> Self {
        PdfError::Json(err)
    }
}

impl fmt::Display for PdfError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PdfError::Nul(e) => write!(f, "Null byte in string: {}", e),
            PdfError::CError(code) => write!(f, "C function returned error code {}", code),
            PdfError::Io(err) => write!(f, "I/O error: {}", err),
            PdfError::Utf8(err) => write!(f, "Invalid UTF-8 returned from C: {}", err),
            PdfError::PageNumberOverflow => write!(f, "page_number exceeds supported range"),
            PdfError::Json(err) => write!(f, "Failed to parse JSON output: {}", err),
            PdfError::NullResult => write!(f, "C extractor returned NULL"),
            PdfError::MissingInput(path) => write!(f, "Input PDF not found: {}", path.display()),
        }
    }
}

impl std::error::Error for PdfError {}

// --- Strongly Typed Block Struct ---

#[derive(Debug, Deserialize)]
pub struct Block {
    // `type` is a keyword in Rust, so we use `r#type` to escape it.
    // Serde automatically maps JSON key "type" to this field.
    pub r#type: String,
    
    pub text: String,
    
    pub bbox: Vec<f64>,
    
    #[serde(default)]
    pub font_size: f64,
    
    #[serde(default)]
    pub confidence: Option<f64>,
    
    #[serde(default)]
    pub row_count: Option<u32>,
    
    #[serde(default)]
    pub col_count: Option<u32>,
}

// --- Public API ---

/// Extract all pages and parse the JSON payloads into strongly typed Blocks.
pub fn to_json_collect<P, Q>(
    pdf_path: P,
    output_dir: Option<Q>,
) -> Result<Vec<Vec<Block>>, PdfError>
where
    P: AsRef<Path>,
    Q: AsRef<Path>,
{
    let files = to_json(pdf_path, output_dir)?;
    let mut pages = Vec::with_capacity(files.len());

    for path in files {
        let contents = fs::read_to_string(&path)?;
        // Deserializing into strict Block types instead of generic Value
        let blocks: Vec<Block> = serde_json::from_str(&contents)?;
        pages.push(blocks);
    }

    Ok(pages)
}

/// Extract an entire PDF into JSON files, returning the generated paths.
pub fn to_json<P, Q>(pdf_path: P, output_dir: Option<Q>) -> Result<Vec<PathBuf>, PdfError>
where
    P: AsRef<Path>,
    Q: AsRef<Path>,
{
    let pdf_path = pdf_path.as_ref();
    if !pdf_path.exists() {
        return Err(PdfError::MissingInput(pdf_path.to_path_buf()));
    }

    let target_dir = resolve_output_dir(pdf_path, output_dir);
    fs::create_dir_all(&target_dir)?;

    convert_document(pdf_path, &target_dir)
}

/// Extract a single page into an in-memory JSON string.
pub fn extract_page_json<P>(pdf_path: P, page_number: usize) -> Result<String, PdfError>
where
    P: AsRef<Path>,
{
    let pdf_path = pdf_path.as_ref();
    if !pdf_path.exists() {
        return Err(PdfError::MissingInput(pdf_path.to_path_buf()));
    }

    if page_number > i32::MAX as usize {
        return Err(PdfError::PageNumberOverflow);
    }

    let pdf_c = path_to_cstring(pdf_path)?;

    let ptr = unsafe { page_to_json_string(pdf_c.as_ptr(), page_number as i32) };

    if ptr.is_null() {
        return Err(PdfError::NullResult);
    }

    let json = unsafe {
        let c_str = CStr::from_ptr(ptr);
        let owned = c_str.to_str()?.to_owned();
        free(ptr as *mut c_void);
        owned
    };

    Ok(json)
}

// --- Internal Helpers ---

fn convert_document(pdf_path: &Path, target_dir: &Path) -> Result<Vec<PathBuf>, PdfError> {
    let pdf_c = path_to_cstring(pdf_path)?;
    let dir_c = path_to_cstring(target_dir)?;

    let ret = unsafe { pdf_to_json(pdf_c.as_ptr(), dir_c.as_ptr()) };
    if ret != 0 {
        return Err(PdfError::CError(ret));
    }

    gather_json_files(target_dir)
}

fn resolve_output_dir<P>(pdf_path: &Path, output_dir: Option<P>) -> PathBuf
where
    P: AsRef<Path>,
{
    output_dir
        .map(|dir| dir.as_ref().to_path_buf())
        .unwrap_or_else(|| default_output_dir(pdf_path))
}

fn default_output_dir(pdf_path: &Path) -> PathBuf {
    let stem = pdf_path
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("output");
    let parent = pdf_path.parent().unwrap_or_else(|| Path::new("."));
    parent.join(format!("{stem}_json"))
}

fn path_to_cstring(path: &Path) -> Result<CString, PdfError> {
    let lossy = path.to_string_lossy();
    CString::new(lossy.as_bytes()).map_err(PdfError::from)
}

fn gather_json_files(dir: &Path) -> Result<Vec<PathBuf>, PdfError> {
    let mut entries: Vec<PathBuf> = Vec::new();

    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().map(|ext| ext == "json").unwrap_or(false)
            && path
                .file_name()
                .and_then(|name| name.to_str())
                .map(|name| name.starts_with("page_"))
                .unwrap_or(false)
        {
            entries.push(path);
        }
    }

    entries.sort();
    Ok(entries)
}