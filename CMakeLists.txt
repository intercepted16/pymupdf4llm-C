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

# --- External Libraries ---
set(EXTERNAL_LIBS
    /home/linuxbrew/.linuxbrew/lib/libmupdf.so
    /home/linuxbrew/.linuxbrew/lib/libfreetype.so
    /home/linuxbrew/.linuxbrew/lib/libz.so
    /home/linuxbrew/.linuxbrew/lib/libharfbuzz.so
    /home/linuxbrew/.linuxbrew/lib/libopenjp2.so
    /home/linuxbrew/.linuxbrew/lib/libjpeg.so
    /home/linuxbrew/.linuxbrew/lib/libpng16.so
    /home/linuxbrew/.linuxbrew/lib/libbz2.so
    /home/linuxbrew/.linuxbrew/lib/libcrypto.so
    /home/linuxbrew/.linuxbrew/lib/libcurl.so
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
add_executable(to_md ${APP_SOURCES})
target_link_libraries(to_md PRIVATE ${EXTERNAL_LIBS})

# --- Installation ---
install(TARGETS tomd DESTINATION lib)
install(TARGETS to_md DESTINATION bin)
message(STATUS "Executables will be installed to ${CMAKE_INSTALL_PREFIX}/bin and libraries to ${CMAKE_INSTALL_PREFIX}/lib")
