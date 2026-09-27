cmake_minimum_required(VERSION 3.20)
project(compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

option(GRAPH_DEBUG "Enable graph consistency checks" OFF)

if(GRAPH_DEBUG)
    add_compile_definitions(DEBUG)
endif()

find_package(BISON 3.8 REQUIRED)
find_package(FLEX 2.6 REQUIRED)

add_subdirectory(tools)
add_subdirectory(frontend)
enable_testing()
add_subdirectory(tests)
