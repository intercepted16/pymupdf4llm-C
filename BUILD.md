# Building PyMuPDF4LLM-C

This project depends on **MuPDF**. The key requirement is the **MuPDF shared library**. Once that’s in place, Python and Rust bindings are handled automatically.

---

## 1. Obtain MuPDF Shared Library

You have two options:

### Option A: Build from the submodule

If you cloned the repository without recursive submodules:

```bash
git submodule update --init --recursive
````

Then navigate to the `mupdf` submodule and build the shared library:

```bash
cd mupdf
make shared=yes release=yes lib
```

After building, copy the generated libraries into your project’s `lib/mupdf` directory:

```
libmupdf.so
libmupdf.so.27.0
```
or
```
libmupdf.dylib
```

or
```
libmupdf.dll
```

---

### Option B: Use precompiled shared libraries

You can also download the precompiled MuPDF 1.27.0 shared libraries:

* [libmupdf.so](https://hc-cdn.hel1.your-objectstorage.com/s/v3/f43060643d2cefdcb0ed114ec165bff60ecb2bf4_libmupdf.so)
* [libmupdf.so.27.0](https://hc-cdn.hel1.your-objectstorage.com/s/v3/f43060643d2cefdcb0ed114ec165bff60ecb2bf4_libmupdf.so.27.0)
* [libmupdf.dylib](https://hc-cdn.hel1.your-objectstorage.com/s/v3/754b5dd360d3996af4d6a69e28c1dbee19b20518_libmupdf.dylib)
* libmupdf.dll (not currently provided)

> **Note:** Precompiled libraries are provided for convenience, as MuPDF 1.27.0 shared libraries are not readily available elsewhere.

---

## 2. Build the Native C Extractor (Optional)

If you want the standalone C library (`libtomd`):

```bash
cd build
cmake ..
make
```

* This produces `libtomd.so` and other artifacts.
* This step is optional if you only need Python or Rust bindings.

---

## 3. Python Installation

Once `libmupdf.so` is in `lib/mupdf`, install the Python package:

```bash
pip install .
```

* `setup.py` automatically links against `libmupdf.so`.

---

## 4. Rust Installation

For Rust, the `build.rs` script handles linking automatically:

```bash
cargo build
```

* Ensure the shared library is in `lib/mupdf` or discoverable via `LD_LIBRARY_PATH`.

---

## Notes

* The **shared library (`libmupdf.so`) is the only critical dependency**.
* Python and Rust builds are automated once the shared library exists.
* If you modify MuPDF or `libtomd` source code, rebuild with `cmake .. && make`.
