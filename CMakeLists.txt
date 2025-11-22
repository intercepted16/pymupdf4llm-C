cmake_minimum_required(VERSION 3.10)
project(PyMuPdf4LLMC C)

# --- Compiler ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(RELEASE_FLAGS "-Wall -Wextra -O3 -march=native -mtune=native -ffast-math -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros -fno-trapping-math -fassociative-math -freciprocal-math")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Determine project root ---
if(DEFINED PROJECT_ROOT_DIR)
    set(PROJECT_ROOT ${PROJECT_ROOT_DIR})
else()
    get_filename_component(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
endif()

# --- MuPDF submodule path ---
set(MUPDF_DIR ${PROJECT_ROOT}/mupdf)   # <-- directly point to the submodule

include_directories(BEFORE ${MUPDF_DIR}/include)

# --- Include directories ---
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- MuPDF shared libraries ---
if(APPLE)
    set(MUPDF_LIBS ${MUPDF_DIR}/libmupdf.dylib)
elseif(UNIX)
    set(MUPDF_LIBS ${MUPDF_DIR}/libmupdf.so)
elseif(WIN32)
    set(MUPDF_LIBS ${MUPDF_DIR}/mupdf.dll)
endif()

# --- Source files ---
file(GLOB APP_SOURCES "src/*.c")

# --- Shared library ---
add_library(tomd SHARED ${APP_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_link_libraries(tomd PRIVATE ${MUPDF_LIBS})

# --- RPATH for Python _cffi ---
set_target_properties(tomd PROPERTIES
    BUILD_WITH_INSTALL_RPATH ON
    INSTALL_RPATH "$ORIGIN/mupdf"
    BUILD_RPATH "$ORIGIN/mupdf"
)

# --- Optional CLI ---
option(BUILD_CLI "Build standalone to_md executable" ON)
if(BUILD_CLI)
    add_executable(to_md ${APP_SOURCES})
    target_link_libraries(to_md PRIVATE ${MUPDF_LIBS})
    set_target_properties(to_md PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN/mupdf"
        BUILD_RPATH "$ORIGIN/mupdf"
    )
endif()

# --- Install ---
install(TARGETS tomd DESTINATION lib)
if(BUILD_CLI)
    install(TARGETS to_md DESTINATION bin)
endif()
