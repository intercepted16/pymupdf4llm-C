# Makefile for Standalone PDF to Markdown Converter
# Pure C implementation in to_md_c directory

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O3 -march=native -mtune=native -ffast-math \
         -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros \
         -fno-trapping-math -fassociative-math -freciprocal-math -DHAVE_MUPDF -DNOLIB_MAIN

# Libraries
LIBS = -lmupdf -lm -pthread

# Source files
MAIN_SOURCE = to_md.c
SOURCES = get_raw_lines.c multicolumn.c improved_table_detection.c
OBJECTS = $(SOURCES:.c=.o)
TARGET = to_md
TABLE_TEST = test_table_detection

# MuPDF paths (adjust these based on your system)
MUPDF_INCLUDE = /usr/include/mupdf
MUPDF_LIB = /usr/lib/x86_64-linux-gnu

# Include directories
INCLUDES = -I$(MUPDF_INCLUDE) -I/usr/include -I.

# Library directories
LIBDIRS = -L$(MUPDF_LIB)

# Full compilation flags
FULL_CFLAGS = $(CFLAGS) $(INCLUDES)
FULL_LDFLAGS = $(LIBDIRS) $(LIBS)

# Default target
all: $(TARGET)

# Rule to compile object files
%.o: %.c %.h
	@echo "Compiling $<..."
	$(CC) $(FULL_CFLAGS) -c $< -o $@

# Main target - compile standalone with all dependencies
$(TARGET): $(OBJECTS) $(MAIN_SOURCE)
	@echo "Building standalone PDF to Markdown converter..."
	@echo "Compiler: $(CC)"
	@echo "Flags: $(FULL_CFLAGS)"
	@echo "Libraries: $(FULL_LDFLAGS)"
	@echo
	$(CC) $(FULL_CFLAGS) -o $(TARGET) $(MAIN_SOURCE) $(OBJECTS) $(FULL_LDFLAGS)
	@echo
	@echo "✓ Build completed successfully!"
	@echo "Executable: ./$(TARGET)"
	@ls -lh $(TARGET)

# Table detection test program
$(TABLE_TEST): improved_table_detection.o
	@echo "Building table detection test program..."
	$(CC) $(FULL_CFLAGS) -o $(TABLE_TEST) test_table_detection.c improved_table_detection.o $(FULL_LDFLAGS)
	@echo "✓ Table detection test program built successfully!"

# Debug build
debug: CFLAGS = -std=c99 -Wall -Wextra -g -O0 -DDEBUG -DHAVE_MUPDF
debug: clean $(TARGET)
	@echo "Debug build completed"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET) $(TABLE_TEST)
	@echo "Clean completed"

# Test the build
test: $(TARGET)
	@echo "Testing the built executable..."
	@if [ -f "../acme.pdf" ]; then \
		echo "Testing with ../acme.pdf..."; \
		./$(TARGET) ../acme.pdf test_output.md; \
		if [ -f "test_output.md" ]; then \
			echo "✓ Test passed - output file created"; \
			wc -c test_output.md; \
			rm -f test_output.md; \
		else \
			echo "❌ Test failed - no output file"; \
		fi \
	else \
		echo "⚠ No test PDF file (../acme.pdf) found"; \
		echo "Testing help output..."; \
		./$(TARGET); \
	fi

# Test table detection
test-table: $(TABLE_TEST)
	@echo "Testing table detection..."
	@if [ -f "test_table.pdf" ]; then \
		echo "Testing with test_table.pdf..."; \
		./$(TABLE_TEST) test_table.pdf 0; \
	else \
		echo "⚠ No test table PDF found"; \
	fi

# Install to system (optional)
install: $(TARGET)
	@echo "Installing $(TARGET) to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Installation completed"

# Uninstall from system (optional)
uninstall:
	@echo "Removing $(TARGET) from /usr/local/bin..."
	sudo rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstallation completed"

# Performance test
benchmark: $(TARGET)
	@echo "Running performance benchmark..."
	@if [ -f "../acme.pdf" ]; then \
		echo "Benchmarking with ../acme.pdf..."; \
		time ./$(TARGET) ../acme.pdf benchmark_output.md; \
		if [ -f "benchmark_output.md" ]; then \
			echo "Output size: $(wc -c < benchmark_output.md) bytes"; \
			rm -f benchmark_output.md; \
		fi \
	else \
		echo "❌ No test PDF file found for benchmarking"; \
	fi

# Show help
help:
	@echo "Available targets:"
	@echo "  all        - Build the to_md executable (default)"
	@echo "  debug      - Build with debug symbols and no optimization"
	@echo "  clean      - Remove all build artifacts"
	@echo "  test       - Test the built executable"
	@echo "  test-table - Test the improved table detection"
	@echo "  benchmark  - Run performance benchmark"
	@echo "  install    - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall  - Remove from /usr/local/bin (requires sudo)"
	@echo "  help       - Show this help message"

.PHONY: all debug clean test test-table install uninstall benchmark help
