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
    fn pdf_to_json(pdf_path: *const c_char, output_file: *const c_char) -> i32;
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

// --- Strongly Typed Block Structs ---

#[derive(Debug, Deserialize)]
pub struct BBox {
    pub x0: f64,
    pub y0: f64,
    pub x1: f64,
    pub y1: f64,
}

#[derive(Debug, Deserialize)]
pub struct TableCell {
    pub bbox: BBox,
    pub text: String,
}

#[derive(Debug, Deserialize)]
pub struct TableRow {
    pub bbox: BBox,
    pub cells: Vec<TableCell>,
}

#[derive(Debug, Deserialize)]
pub struct Block {
    // `type` is a keyword in Rust, so we use `r#type` to escape it.
    // Serde automatically maps JSON key "type" to this field.
    #[serde(rename = "type")]
    pub r#type: String,
    
    pub text: String,
    
    // For backward compatibility, support both array and object bbox formats
    #[serde(deserialize_with = "deserialize_bbox")]
    pub bbox: BBox,
    
    #[serde(default)]
    pub font_size: f64,
    
    #[serde(default)]
    pub font_weight: Option<String>,
    
    pub page_number: i32,
    
    pub length: usize,
    
    #[serde(default)]
    pub lines: Option<u32>,
    
    #[serde(default)]
    pub confidence: Option<f64>,
    
    #[serde(default)]
    pub row_count: Option<u32>,
    
    #[serde(default)]
    pub col_count: Option<u32>,
    
    #[serde(default)]
    pub cell_count: Option<u32>,
    
    #[serde(default)]
    pub rows: Option<Vec<TableRow>>,
}

// Custom deserializer to handle bbox as either [x0, y0, x1, y1] or {x0, y0, x1, y1}
fn deserialize_bbox<'de, D>(deserializer: D) -> Result<BBox, D::Error>
where
    D: serde::Deserializer<'de>,
{
    use serde::de::Error;
    use serde_json::Value;
    
    let value = Value::deserialize(deserializer)?;
    
    match value {
        Value::Array(arr) if arr.len() == 4 => {
            Ok(BBox {
                x0: arr[0].as_f64().ok_or_else(|| Error::custom("bbox[0] not a number"))?,
                y0: arr[1].as_f64().ok_or_else(|| Error::custom("bbox[1] not a number"))?,
                x1: arr[2].as_f64().ok_or_else(|| Error::custom("bbox[2] not a number"))?,
                y1: arr[3].as_f64().ok_or_else(|| Error::custom("bbox[3] not a number"))?,
            })
        }
        Value::Object(obj) => {
            Ok(BBox {
                x0: obj.get("x0")
                    .and_then(|v| v.as_f64())
                    .ok_or_else(|| Error::custom("missing or invalid x0"))?,
                y0: obj.get("y0")
                    .and_then(|v| v.as_f64())
                    .ok_or_else(|| Error::custom("missing or invalid y0"))?,
                x1: obj.get("x1")
                    .and_then(|v| v.as_f64())
                    .ok_or_else(|| Error::custom("missing or invalid x1"))?,
                y1: obj.get("y1")
                    .and_then(|v| v.as_f64())
                    .ok_or_else(|| Error::custom("missing or invalid y1"))?,
            })
        }
        _ => Err(Error::custom("bbox must be array [x0,y0,x1,y1] or object {x0,y0,x1,y1}")),
    }
}

// --- Public API ---

/// Extract all pages and parse the JSON payload into strongly typed Blocks.
pub fn to_json_collect<P, Q>(
    pdf_path: P,
    output_file: Option<Q>,
) -> Result<Vec<Block>, PdfError>
where
    P: AsRef<Path>,
    Q: AsRef<Path>,
{
    let json_file = to_json(pdf_path, output_file)?;
    let contents = fs::read_to_string(&json_file)?;
    let blocks: Vec<Block> = serde_json::from_str(&contents)?;
    Ok(blocks)
}

/// Extract an entire PDF into a single merged JSON file.
pub fn to_json<P, Q>(pdf_path: P, output_file: Option<Q>) -> Result<PathBuf, PdfError>
where
    P: AsRef<Path>,
    Q: AsRef<Path>,
{
    let pdf_path = pdf_path.as_ref();
    if !pdf_path.exists() {
        return Err(PdfError::MissingInput(pdf_path.to_path_buf()));
    }

    let target_file = resolve_output_file(pdf_path, output_file);
    
    if let Some(parent) = target_file.parent() {
        fs::create_dir_all(parent)?;
    }

    convert_document(pdf_path, &target_file)?;
    Ok(target_file)
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

fn convert_document(pdf_path: &Path, target_file: &Path) -> Result<(), PdfError> {
    let pdf_c = path_to_cstring(pdf_path)?;
    let file_c = path_to_cstring(target_file)?;

    let ret = unsafe { pdf_to_json(pdf_c.as_ptr(), file_c.as_ptr()) };
    if ret != 0 {
        return Err(PdfError::CError(ret));
    }

    Ok(())
}

fn resolve_output_file<P>(pdf_path: &Path, output_file: Option<P>) -> PathBuf
where
    P: AsRef<Path>,
{
    output_file
        .map(|file| file.as_ref().to_path_buf())
        .unwrap_or_else(|| default_output_file(pdf_path))
}

fn default_output_file(pdf_path: &Path) -> PathBuf {
    pdf_path.with_extension("json")
}