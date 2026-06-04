cmake_minimum_required(VERSION 3.16)
project(usde LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Common source files (Layer 1 + Layer 2) ────────────────────────────────

set(USDE_CORE_SOURCES
    src/dbc_parser.cpp
    src/ldf_parser.cpp
    src/arxml_parser.cpp
    src/codec_engine.cpp
)

set(USDE_CORE_HEADERS
    include/usde_types.h
    include/dbc_parser.h
    include/ldf_parser.h
    include/arxml_parser.h
    include/codec_engine.h
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
    ${USDE_CORE_HEADERS}
)
target_include_directories(usde PUBLIC ${USDE_INCLUDE_DIR})

# Export all symbols on Windows for simplicity; production builds should use
# a .def file or explicit __declspec(dllexport).
set_target_properties(usde PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
    target_compile_options(usde PRIVATE /utf-8 /W4)
    target_compile_options(usde_demo PRIVATE /utf-8 /W4)
else()
    target_compile_options(usde PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ─── 3. Python extension (pybind11) — optional ──────────────────────────────

find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(usde_python
        src/usde_pybind.cpp
        ${USDE_CORE_SOURCES}
    )
    target_include_directories(usde_python PRIVATE ${USDE_INCLUDE_DIR})
    message(STATUS "pybind11 found — building usde_python module")
else()
    message(STATUS "pybind11 not found — skipping usde_python (install via pip or vcpkg)")
endif()
