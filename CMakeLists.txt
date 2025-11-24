cmake_minimum_required(VERSION 3.10)
project(PyMuPdf4LLMC C)

# --- Compiler standard ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --- Default build type ---
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Compiler flags ---
set(RELEASE_FLAGS "-Wall -Wextra -O3 -march=native -mtune=native -ffast-math -funroll-loops -finline-functions -ftree-vectorize -fno-signed-zeros -fno-trapping-math -fassociative-math -freciprocal-math")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")

# Debug flags (profiling-friendly)
set(DEBUG_FLAGS "-Wall -Wextra -O0 -g -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")

# --- Project root ---
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

# --- MuPDF submodule ---
set(MUPDF_DIR ${PROJECT_ROOT}/mupdf)
include_directories(BEFORE ${MUPDF_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- MuPDF library search ---
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
set(LIBS m)  # Math library

# --- Shared library for Python ---
add_library(tomd SHARED ${APP_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_link_libraries(tomd PRIVATE ${MUPDF_LIBS} ${LIBS})
set_target_properties(tomd PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "$ORIGIN"
)

# Copy MuPDF libs next to tomd (Unix)
if(UNIX AND NOT APPLE)
    get_filename_component(MUPDF_DIR_PATH "${MUPDF_LIBS}" DIRECTORY)
    file(GLOB MUPDF_SHARED_LIBS "${MUPDF_DIR_PATH}/libmupdf.so.*.*") # only get versioned shared libs
    foreach(lib ${MUPDF_SHARED_LIBS})
        add_custom_command(TARGET tomd POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${lib}
                    $<TARGET_FILE_DIR:tomd>
            COMMENT "Copying ${lib} next to tomd"
        )
    endforeach()
endif()

# --- Optional CLI executable ---
option(BUILD_CLI "Build standalone to_md executable" ON)
if(BUILD_CLI)
    add_executable(to_md ${APP_SOURCES})
    target_link_libraries(to_md PRIVATE ${MUPDF_LIBS} ${LIBS})
    set_target_properties(to_md PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH "$ORIGIN"
    )

    if(UNIX AND NOT APPLE)
        foreach(lib ${MUPDF_SHARED_LIBS})
            add_custom_command(TARGET to_md POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${lib}
                        $<TARGET_FILE_DIR:to_md>
                COMMENT "Copying ${lib} next to to_md"
            )
        endforeach()
    endif()
endif()

# --- Install ---
if(SKBUILD)
    install(TARGETS tomd 
        LIBRARY DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
        RUNTIME DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
    )

    if(MUPDF_SHARED_LIBS)
        install(FILES ${MUPDF_SHARED_LIBS}
            DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
        )
    endif()
else()
    install(TARGETS tomd DESTINATION lib)
endif()

if(BUILD_CLI)
    install(TARGETS to_md DESTINATION bin)
endif()
