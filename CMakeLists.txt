cmake_minimum_required(VERSION 3.10)
project(PyMuPdf4LLMC C)

find_package(Threads REQUIRED)

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
if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(NOT DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

# --- Include directories ---
include_directories(
    include
    /usr/include
)

# --- External Libraries ---
set(EXTERNAL_LIBS
    /usr/lib/libmupdf.so
    /usr/lib/libfreetype.so
    /usr/lib/libz.so
    /usr/lib/libharfbuzz.so
    /usr/lib/libopenjp2.so
    /usr/lib/libjpeg.so
    /usr/lib/libpng16.so
    /usr/lib/libbz2.so
    /usr/lib/libcrypto.so
    /usr/lib/libcurl.so
    m
    ${CMAKE_THREAD_LIBS_INIT}
)

# --- Source files ---
file(GLOB APP_SOURCES "src/*.c")

# --- Shared Library ---
add_library(tomd SHARED ${APP_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_link_libraries(tomd PRIVATE ${EXTERNAL_LIBS})

# --- Main executable ---
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

message(STATUS "Executables will be installed to ${CMAKE_INSTALL_PREFIX}/bin and libraries to ${CMAKE_INSTALL_PREFIX}/lib")
