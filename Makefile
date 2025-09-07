# Makefile for Standalone PDF to Markdown Converter
# Pure C implementation with ultra-optimizations

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O3 -march=native -mtune=native -ffast-math \
         -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros \
         -fno-trapping-math -fassociative-math -freciprocal-math -DHAVE_MUPDF

# Libraries
LIBS = -lmupdf -lm -pthread

# Directories
SRCDIR = .
BUILDDIR = build
BINDIR = bin

# Source files
SOURCES = to_md_standalone.c
TARGET = to_md

# MuPDF paths (adjust these based on your system)
MUPDF_INCLUDE = /usr/include/mupdf
MUPDF_LIB = /usr/lib/x86_64-linux-gnu

# Include directories
INCLUDES = -I$(MUPDF_INCLUDE) -I/usr/include

# Library directories
LIBDIRS = -L$(MUPDF_LIB)

# Full compilation flags
FULL_CFLAGS = $(CFLAGS) $(INCLUDES)
FULL_LDFLAGS = $(LIBDIRS) $(LIBS)

# Default target
all: $(TARGET)

# Create directories
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Main target
$(TARGET): $(BUILDDIR) $(BINDIR) $(SOURCES)
	@echo "Building ultra-optimized standalone PDF to Markdown converter..."
	@echo "Compiler: $(CC)"
	@echo "Flags: $(FULL_CFLAGS)"
	@echo "Libraries: $(FULL_LDFLAGS)"
	@echo
	$(CC) $(FULL_CFLAGS) -o $(BINDIR)/$(TARGET) $(SOURCES) $(FULL_LDFLAGS)
	@echo
	@echo "✓ Build completed successfully!"
	@echo "Executable: $(BINDIR)/$(TARGET)"
	@ls -lh $(BINDIR)/$(TARGET)

# Debug build
debug: CFLAGS = -std=c99 -Wall -Wextra -g -O0 -DDEBUG -DHAVE_MUPDF
debug: $(TARGET)
	@echo "Debug build completed"

# Test the build
test: $(TARGET)
	@echo "Testing the built executable..."
	@if [ -f "acme.pdf" ]; then \
		echo "Testing with acme.pdf..."; \
		./$(BINDIR)/$(TARGET) acme.pdf test_output.md; \
		if [ -f "test_output.md" ]; then \
			echo "✓ Test passed - output file created"; \
			wc -c test_output.md; \
			rm -f test_output.md; \
		else \
			echo "❌ Test failed - no output file"; \
		fi \
	else \
		echo "⚠ No test PDF file (acme.pdf) found"; \
		echo "Testing help output..."; \
		./$(BINDIR)/$(TARGET); \
	fi

# Performance test
benchmark: $(TARGET)
	@echo "Running performance benchmark..."
	@if [ -f "acme.pdf" ]; then \
		echo "Benchmarking with acme.pdf..."; \
		time ./$(BINDIR)/$(TARGET) acme.pdf benchmark_output.md; \
		if [ -f "benchmark_output.md" ]; then \
			echo "Output size: $$(wc -c < benchmark_output.md) bytes"; \
			rm -f benchmark_output.md; \
		fi \
	else \
		echo "❌ No test PDF file found for benchmarking"; \
	fi

# Static analysis
analyze:
	@echo "Running static analysis..."
	@which cppcheck > /dev/null 2>&1 && cppcheck --enable=all --std=c99 $(SOURCES) || echo "cppcheck not found"
	@which clang-tidy > /dev/null 2>&1 && clang-tidy $(SOURCES) -- $(INCLUDES) || echo "clang-tidy not found"

# Memory check (requires valgrind)
memcheck: debug
	@echo "Running memory check with valgrind..."
	@which valgrind > /dev/null 2>&1 && \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		./$(BINDIR)/$(TARGET) acme.pdf valgrind_output.md 2> valgrind.log && \
		echo "Memory check completed - see valgrind.log" || \
		echo "valgrind not found"

# Check dependencies
deps:
	@echo "Checking dependencies..."
	@echo "Required libraries:"
	@echo "  - MuPDF development libraries"
	@echo "  - Standard C library with math support"
	@echo
	@echo "Checking MuPDF headers..."
	@test -f /usr/include/mupdf/fitz.h && echo "✓ MuPDF headers found" || echo "❌ MuPDF headers not found"
	@echo
	@echo "Checking MuPDF library..."
	@ldconfig -p | grep mupdf > /dev/null && echo "✓ MuPDF library found" || echo "❌ MuPDF library not found"
	@echo
	@echo "To install MuPDF on Ubuntu/Debian:"
	@echo "  sudo apt-get install libmupdf-dev"
	@echo
	@echo "To install MuPDF on CentOS/RHEL:"
	@echo "  sudo yum install mupdf-devel"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILDDIR) $(BINDIR)
	rm -f *.o *.log test_output.md benchmark_output.md valgrind_output.md
	@echo "✓ Clean completed"

# Show build info
info:
	@echo "=== Build Information ==="
	@echo "Compiler: $(CC)"
	@echo "Version: $$($(CC) --version | head -n1)"
	@echo "Target: $(TARGET)"
	@echo "Optimization: $(CFLAGS)"
	@echo "Libraries: $(LIBS)"
	@echo "Source: $(SOURCES)"
	@echo "========================="

# Show usage
help:
	@echo "Standalone PDF to Markdown Converter - Build System"
	@echo
	@echo "Available targets:"
	@echo "  all        - Build optimized executable (default)"
	@echo "  debug      - Build debug version"
	@echo "  no-mupdf   - Build without MuPDF (fallback)"
	@echo "  install    - Install system-wide"
	@echo "  test       - Test the built executable"
	@echo "  benchmark  - Run performance benchmark"
	@echo "  analyze    - Run static analysis"
	@echo "  memcheck   - Run memory check with valgrind"
	@echo "  deps       - Check dependencies"
	@echo "  clean      - Clean build artifacts"
	@echo "  info       - Show build information"
	@echo "  help       - Show this help"
	@echo
	@echo "Usage examples:"
	@echo "  make               # Build optimized version"
	@echo "  make debug         # Build debug version"
	@echo "  make test          # Build and test"
	@echo "  make install       # Build and install"
	@echo "  make clean all     # Clean and rebuild"

.PHONY: all debug no-mupdf install test benchmark analyze memcheck deps clean info help

