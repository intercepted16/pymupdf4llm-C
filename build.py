import shutil
import os
import subprocess
import sys

# Paths
SRC_SO = os.path.abspath("lib/libtomd.so")
DST_DIR = os.path.abspath("pymupdf4llm_c/lib")
DST_SO = os.path.join(DST_DIR, "libtomd.so")

# Step 1: Ensure destination folder exists
os.makedirs(DST_DIR, exist_ok=True)

# Step 2: Copy the .so file (overwrite if exists)
shutil.copy2(SRC_SO, DST_SO)
print(f"Copied {SRC_SO} -> {DST_SO}")

# Step 3: Clean previous builds
for folder in ["build", "dist", "pymupdf4llm_c.egg-info"]:
    if os.path.exists(folder):
        shutil.rmtree(folder)
        print(f"Removed {folder}")

# Step 4: Build wheel and sdist
subprocess.check_call([sys.executable, "setup.py", "sdist", "bdist_wheel"])
print("Build finished. You can now install the wheel with pip install dist/*.whl")
