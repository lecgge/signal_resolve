cmake_minimum_required(VERSION 3.16)
project(usde LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(usde_demo
    src/main.cpp
    src/dbc_parser.cpp
    src/ldf_parser.cpp
    src/arxml_parser.cpp
    src/codec_engine.cpp
)

target_include_directories(usde_demo PRIVATE include)

if(MSVC)
    target_compile_options(usde_demo PRIVATE /utf-8 /W4)
endif()
