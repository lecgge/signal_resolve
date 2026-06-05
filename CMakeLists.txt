cmake_minimum_required(VERSION 3.16)
project(usde LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Common source files (Layer 1 + Layer 2) ────────────────────────────────

set(USDE_CORE_SOURCES
    src/dbc_parser.cpp
    src/ldf_parser.cpp
    src/arxml_parser.cpp
    src/codec_engine.cpp
)

set(USDE_PUBLIC_HEADERS
    include/usde_types.h
    include/dbc_parser.h
    include/ldf_parser.h
    include/arxml_parser.h
    include/codec_engine.h
    include/usde_c_api.h
)

set(USDE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ─── 1. Executable (demo / tests) ───────────────────────────────────────────

add_executable(usde_demo
    src/main.cpp
    ${USDE_CORE_SOURCES}
)
target_include_directories(usde_demo PRIVATE ${USDE_INCLUDE_DIR})

# ─── 2. Shared library (C-API for JNA / FFI) ────────────────────────────────

add_library(usde SHARED
    src/usde_c_api.cpp
    ${USDE_CORE_SOURCES}
)
target_include_directories(usde PUBLIC ${USDE_INCLUDE_DIR})
set_target_properties(usde PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PUBLIC_HEADER "${USDE_PUBLIC_HEADERS}"
)

if(MSVC)
    target_compile_options(usde PRIVATE /utf-8 /W4)
    target_compile_options(usde_demo PRIVATE /utf-8 /W4)
else()
    target_compile_options(usde PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ─── 3. Python extension (pybind11) — optional ──────────────────────────────
#   Set -DUSDE_PYTHON_EXECUTABLE=... to target a specific Python version.
#   Set -DUSDE_PYTHON_VERSION=3.11 to search for a specific version.

set(USDE_PYTHON_EXECUTABLE "" CACHE FILEPATH "Python interpreter for pybind11 module")
set(USDE_PYTHON_VERSION "" CACHE STRING "Target Python version (e.g. 3.11)")

if(USDE_PYTHON_EXECUTABLE)
    set(PYBIND11_FINDPYTHON ON)
    set(PYTHON_EXECUTABLE "${USDE_PYTHON_EXECUTABLE}")
elseif(USDE_PYTHON_VERSION)
    set(PYBIND11_FINDPYTHON ON)
    set(PYBIND11_PYTHON_VERSION "${USDE_PYTHON_VERSION}")
endif()

find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(usde_python
        src/usde_pybind.cpp
        ${USDE_CORE_SOURCES}
    )
    target_include_directories(usde_python PRIVATE ${USDE_INCLUDE_DIR})
    if(MSVC)
        target_compile_options(usde_python PRIVATE /utf-8)
    endif()
    message(STATUS "pybind11 found — building usde_python module")
    # Post-build: copy .pyd to python/usde/ for self-contained package
    add_custom_command(TARGET usde_python POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:usde_python>"
            "${CMAKE_CURRENT_SOURCE_DIR}/python/usde/"
        COMMENT "Copying usde_python to python/usde/"
    )
else()
    message(STATUS "pybind11 not found — skipping usde_python (pip install pybind11)")
endif()

# ─── 4. Install targets ─────────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS usde
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/usde
)

if(pybind11_FOUND)
    install(TARGETS usde_python
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
