# Building

This project depends on **MuPDF**. That's pretty much the only thing you've got to worry about.

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
libmupdf.so.25.0
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

You can also download the precompiled MuPDF 1.25.0 shared libraries from the GitHub releases:

**Linux (x86_64):**
* [libmupdf.so](https://github.com/intercepted16/pymupdf4llm-C/releases/download/mupdf-1.25.0/libmupdf.so)
* [libmupdf.so.25.0](https://github.com/intercepted16/pymupdf4llm-C/releases/download/mupdf-1.25.0/libmupdf.so.25.0)

**macOS (Universal):**
* [libmupdf.dylib](https://github.com/intercepted16/pymupdf4llm-C/releases/download/mupdf-1.25.0/libmupdf.dylib)

**Windows:**
* Not provided.

> **Note:** These precompiled libraries are automatically built by the [Build MuPDF Native Libraries workflow](https://github.com/intercepted16/pymupdf4llm-C/blob/main/.github/workflows/buildwinmac.yml) and published to [GitHub Releases](https://github.com/intercepted16/pymupdf4llm-C/releases). They are used automatically by `cibuildwheel` when building Python wheels.

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

## Notes

* The **shared library (`libmupdf.so`) is the only critical dependency**.
* Python builds are automated once the shared library exists.
* If you modify MuPDF or `libtomd` source code, rebuild with `cmake .. && make`.
