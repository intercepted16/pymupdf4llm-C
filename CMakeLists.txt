cmake_minimum_required(VERSION 3.10)
project(PyMuPdf4LLMC C)

# --- Compiler and Flags ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(RELEASE_FLAGS "-Wall -Wextra -O3 -march=native -mtune=native -ffast-math -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros -fno-trapping-math -fassociative-math -freciprocal-math")
set(DEBUG_FLAGS "-Wall -Wextra -g -O0 -DDEBUG")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Includes ---
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Dependencies ---
find_package(Threads REQUIRED)
find_library(MUPDF_LIB mupdf PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
find_library(FREETYPE_LIB freetype)
find_library(ZLIB z)
find_library(HARFBUZZ_LIB harfbuzz)
find_library(OPENJP2_LIB openjp2)
find_library(JPEG_LIB jpeg)
find_library(PNG_LIB png16)
find_library(BZ2_LIB bz2)
find_library(CRYPTO_LIB crypto)
find_library(CURL_LIB curl)

set(EXTERNAL_LIBS
  ${MUPDF_LIB}
  ${FREETYPE_LIB}
  ${ZLIB}
  ${HARFBUZZ_LIB}
  ${OPENJP2_LIB}
  ${JPEG_LIB}
  ${PNG_LIB}
  ${BZ2_LIB}
  ${CRYPTO_LIB}
  ${CURL_LIB}
  m
  Threads::Threads
)

# --- Source files ---
file(GLOB APP_SOURCES "src/*.c")

# --- Shared Library ---
add_library(tomd SHARED ${APP_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_link_libraries(tomd PRIVATE ${EXTERNAL_LIBS})

# --- CLI executable ---
option(BUILD_CLI "Build standalone to_md executable" ON)
if(BUILD_CLI)
  add_executable(to_md ${APP_SOURCES})
  target_link_libraries(to_md PRIVATE ${EXTERNAL_LIBS})
endif()

# --- Installation ---
install(TARGETS tomd DESTINATION lib)
if(BUILD_CLI)
  install(TARGETS to_md DESTINATION bin)
endif()

message(STATUS "Executables → ${CMAKE_INSTALL_PREFIX}/bin, libraries → ${CMAKE_INSTALL_PREFIX}/lib")

