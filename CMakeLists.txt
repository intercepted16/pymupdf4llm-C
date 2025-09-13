cmake_minimum_required(VERSION 3.10)
project(ToMarkdownC C)

# --- Compiler and Flags ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)


# --- Build Types and Flags ---
set(RELEASE_FLAGS "-Wall -Wextra -O3 -march=native -mtune=native -ffast-math -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros -fno-trapping-math -fassociative-math -freciprocal-math")

set(CMAKE_C_FLAGS_RELEASE ${RELEASE_FLAGS})
set(DEBUG_FLAGS "-Wall -Wextra -g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG ${DEBUG_FLAGS})
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# --- Include directories ---
include_directories(
    include
    /home/linuxbrew/.linuxbrew/include
)

# --- Libraries (Homebrew .so files) ---
set(MUPDF_LIB /home/linuxbrew/.linuxbrew/lib/libmupdf.so)
set(FREETYPE_LIB /home/linuxbrew/.linuxbrew/lib/libfreetype.so)
set(ZLIB_LIB /home/linuxbrew/.linuxbrew/lib/libz.so)
set(HARFBUZZ_LIB /home/linuxbrew/.linuxbrew/lib/libharfbuzz.so)
set(OPENJPEG_LIB /home/linuxbrew/.linuxbrew/lib/libopenjp2.so)
set(LIBJPEG_LIB /home/linuxbrew/.linuxbrew/lib/libjpeg.so)
set(LIBPNG_LIB /home/linuxbrew/.linuxbrew/lib/libpng16.so)
set(LIBBZ2_LIB /home/linuxbrew/.linuxbrew/lib/libbz2.so)
set(LIBCRYPTO_LIB /home/linuxbrew/.linuxbrew/lib/libcrypto.so)
set(LIBCURL_LIB /home/linuxbrew/.linuxbrew/lib/libcurl.so)

# --- Threads ---
find_package(Threads REQUIRED)

# --- Source files ---
file(GLOB APP_SOURCES "src/*.c")

# --- Shared Library ---
add_library(tomd SHARED ${APP_SOURCES} ${EXTRA_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_link_libraries(tomd PRIVATE
    ${MUPDF_LIB}
    ${FREETYPE_LIB}
    ${ZLIB_LIB}
    ${HARFBUZZ_LIB}
    ${OPENJPEG_LIB}
    ${LIBJPEG_LIB}
    ${LIBPNG_LIB}
    ${LIBBZ2_LIB}
    ${LIBCRYPTO_LIB}
    ${LIBCURL_LIB}
    m
    ${CMAKE_THREAD_LIBS_INIT}
)

# --- Main executable ---
add_executable(to_md ${APP_SOURCES} ${EXTRA_SOURCES})
target_link_libraries(to_md PRIVATE
    ${MUPDF_LIB}
    ${FREETYPE_LIB}
    ${ZLIB_LIB}
    ${HARFBUZZ_LIB}
    ${OPENJPEG_LIB}
    ${LIBJPEG_LIB}
    ${LIBPNG_LIB}
    ${LIBBZ2_LIB}
    ${LIBCRYPTO_LIB}
    ${LIBCURL_LIB}
    m
    ${CMAKE_THREAD_LIBS_INIT}
)

# --- Table detection test executable ---
add_executable(test_table_detection test_table_detection.c src/improved_table_detection.c src/table.c)
target_link_libraries(test_table_detection PRIVATE
    ${MUPDF_LIB}

    ${FREETYPE_LIB}
    ${ZLIB_LIB}
    ${HARFBUZZ_LIB}
    ${OPENJPEG_LIB}
    ${LIBJPEG_LIB}
    ${LIBPNG_LIB}

    ${LIBBZ2_LIB}

    ${LIBCRYPTO_LIB}
    ${LIBCURL_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
)

# --- Installation ---
install(TARGETS to_md tomd DESTINATION bin)
message(STATUS "Executable and library will be installed to ${CMAKE_INSTALL_PREFIX}/bin")

# --- Custom targets ---
add_custom_target(test
    COMMAND ${CMAKE_COMMAND} -E echo "Testing the built executable..."
    COMMAND bash -c "if [ -f \"../test_table.pdf\" ]; then ./bin/to_md ../test_table.pdf test_output.md; fi"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS to_md

    COMMENT "Runs the main application test"
)

add_custom_target(test-table
    COMMAND ${CMAKE_COMMAND} -E echo "Testing table detection..."
    COMMAND bash -c "if [ -f \"../test_table.pdf\" ]; then ./bin/test_table_detection ../test_table.pdf 0; fi"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS test_table_detection
    COMMENT "Runs the table detection test"
)

add_custom_target(benchmark
    COMMAND ${CMAKE_COMMAND} -E echo "Running performance benchmark..."
    COMMAND bash -c "if [ -f \"../test_table.pdf\" ]; then time ./bin/to_md ../test_table.pdf benchmark_output.md; fi"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS to_md
    COMMENT "Runs a performance benchmark"
)
