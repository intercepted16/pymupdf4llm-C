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
# CMAKE_SOURCE_DIR is the top-level source directory (the sdist root during build)
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

# --- MuPDF submodule path ---
set(MUPDF_DIR ${PROJECT_ROOT}/mupdf)   # <-- directly point to the submodule

include_directories(BEFORE ${MUPDF_DIR}/include)

# --- Include directories ---
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- MuPDF shared libraries ---
# Try multiple locations for MuPDF library (prioritize lib/mupdf/)
set(MUPDF_LIB_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/lib/mupdf
    ${CMAKE_SOURCE_DIR}/mupdf
    ${MUPDF_DIR}
)

if(APPLE)
    set(MUPDF_LIB_NAME libmupdf.dylib)
elseif(WIN32)
    set(MUPDF_LIB_NAME mupdf.dll)
else()
    set(MUPDF_LIB_NAME libmupdf.so)
endif()

# Find MuPDF library
set(MUPDF_LIBS "")
foreach(search_path ${MUPDF_LIB_SEARCH_PATHS})
    if(EXISTS "${search_path}/${MUPDF_LIB_NAME}")
        set(MUPDF_LIBS "${search_path}/${MUPDF_LIB_NAME}")
        message(STATUS "Found MuPDF library: ${MUPDF_LIBS}")
        break()
    endif()
endforeach()

if(NOT MUPDF_LIBS)
    message(FATAL_ERROR "MuPDF library not found! Searched in: ${MUPDF_LIB_SEARCH_PATHS}")
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
# Install to Python package directory
if(SKBUILD)
    # scikit-build-core sets SKBUILD_PLATLIB_DIR for the Python package location
    install(TARGETS tomd 
        LIBRARY DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
        RUNTIME DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
    )
    
    # Also install MuPDF library
    if(EXISTS "${MUPDF_LIBS}")
        install(FILES "${MUPDF_LIBS}" 
            DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
        )
    endif()
    
    # On Linux, also copy the .so symlink if it exists
    if(UNIX AND NOT APPLE)
        get_filename_component(MUPDF_DIR_PATH "${MUPDF_LIBS}" DIRECTORY)
        if(EXISTS "${MUPDF_DIR_PATH}/libmupdf.so")
            install(FILES "${MUPDF_DIR_PATH}/libmupdf.so"
                DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
            )
        endif()
    endif()
else()
    # Traditional install
    install(TARGETS tomd DESTINATION lib)
endif()

if(BUILD_CLI)
    install(TARGETS to_md DESTINATION bin)
endif()
