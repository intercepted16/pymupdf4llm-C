# Building

This project depends on **MuPDF**. That's pretty much the only thing you've got to worry about.

---

## 1. Obtain MuPDF Shared Library

You have two options:

### Option A: Build from the submodule

If you cloned the repository without recursive submodules:

```bash
git submodule update --init --recursive
```

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

You can also download the precompiled MuPDF 1.27.0 shared libraries from the GitHub releases:

**Linux (x86_64):**
* [libmupdf.so](https://github.com/intercepted16/mupdf-prebuilts/releases/download/mupdf-1.27.0/libmupdf.so)
* [libmupdf.so.27.0](https://github.com/intercepted16/mupdf-prebuilts/releases/download/mupdf-1.27.0/libmupdf.so.27.0)

**macOS (Universal):**
* [libmupdf.dylib](https://github.com/intercepted16/mupdf-prebuilts/releases/download/mupdf-1.27.0/libmupdf.dylib)

**Windows:**
* Not provided.

> **Note:** These precompiled libraries are automatically built by [MuPDF prebuilts](https://github.com/intercepted16/mupdf-prebuilts) and published to [GitHub Releases](https://github.com/intercepted16/mupdf-prebuilts/releases). They are used automatically by `cibuildwheel` when building Python wheels.

---

## 2. Use the Go CLI

In `go/cmd/tomd/main.go`, there is a basic cli, that can be used via:

```bash
go run cmd/tomd <pdf_path> [output_json]
```

You could also manually build the Go shared library if you want to use that in any other language.
I won't go into detail here, however.

---

## 3. Python Installation

Once `libmupdf.so` and `libmupdf.so.27.0` is in `lib/mupdf`, install the Python package:

```bash
pip install .
```

> This can be prefixed with anything, for example, I like to use `uv`.

* `setup.py` automatically links against the shared libraries.

---

## Notes

* The **shared library (`libmupdf.so`) is the only critical dependency**.
* Python builds are automated once the shared library exists.
