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
set(RELEASE_FLAGS "-Wall -Wextra -O3 -march=native -mtune=native -funroll-loops -finline-functions -ftree-vectorize")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")

set(DEBUG_FLAGS "-Wall -Wextra -O0 -g -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")

# --- Project root ---
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

# --- MuPDF headers (pinned submodule) ---
set(MUPDF_INCLUDE ${PROJECT_ROOT}/mupdf/include)

# --- MuPDF library (vendored) ---
if(APPLE)
    file(GLOB MUPDF_LIBS "${PROJECT_ROOT}/lib/mupdf/libmupdf.dylib*")
elseif(UNIX)
    file(GLOB MUPDF_LIBS "${PROJECT_ROOT}/lib/mupdf/libmupdf.so*")
else()
    message(FATAL_ERROR "Unsupported platform for MuPDF library detection")
endif()
list(GET MUPDF_LIBS 0 MUPDF_LIBS)
message(STATUS "Using MuPDF library: ${MUPDF_LIBS}")

# --- Source files ---
file(GLOB_RECURSE APP_SOURCES
    "${PROJECT_SOURCE_DIR}/src/*.c"
)
set(LIBS m)  # math library

# --- Shared library for Python ---
add_library(tomd SHARED ${APP_SOURCES})
target_compile_definitions(tomd PRIVATE NOLIB_MAIN)
target_include_directories(tomd PRIVATE ${MUPDF_INCLUDE} ${PROJECT_ROOT}/include)
target_link_libraries(tomd PRIVATE ${MUPDF_LIBS} ${LIBS})
set_target_properties(tomd PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "$ORIGIN"
)

# Copy MuPDF next to tomd
if(UNIX AND NOT APPLE)
    add_custom_command(TARGET tomd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MUPDF_LIBS}
                $<TARGET_FILE_DIR:tomd>
        COMMENT "Copying MuPDF library next to tomd"
    )
endif()

# --- Optional CLI executable ---
option(BUILD_CLI "Build standalone to_md executable" ON)
if(BUILD_CLI)
    add_executable(to_md ${APP_SOURCES})
    target_compile_definitions(to_md PRIVATE NOLIB_MAIN)
    target_include_directories(to_md PRIVATE ${MUPDF_INCLUDE} ${PROJECT_ROOT}/include)
    target_link_libraries(to_md PRIVATE ${MUPDF_LIBS} ${LIBS})
    set_target_properties(to_md PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
        BUILD_RPATH "$ORIGIN"
    )

    if(UNIX AND NOT APPLE)
        add_custom_command(TARGET to_md POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${MUPDF_LIBS}
                    $<TARGET_FILE_DIR:to_md>
            COMMENT "Copying MuPDF library next to to_md"
        )
    endif()
endif()

# --- Install ---
if(SKBUILD)
    install(TARGETS tomd 
        LIBRARY DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
        RUNTIME DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib
    )
    install(FILES ${MUPDF_LIBS} DESTINATION ${SKBUILD_PLATLIB_DIR}/pymupdf4llm_c/lib)
else()
    install(TARGETS tomd DESTINATION lib)
    install(FILES ${MUPDF_LIBS} DESTINATION lib)
endif()

if(BUILD_CLI)
    install(TARGETS to_md DESTINATION bin)
endif()

