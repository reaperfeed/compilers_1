cmake_minimum_required(VERSION 3.20)
project(lab1_graphs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GRAPH_DEBUG "Enable graph consistency checks" OFF)

if(GRAPH_DEBUG)
    add_compile_definitions(DEBUG)
endif()

add_subdirectory(tools)
enable_testing()
add_subdirectory(tests)
add_executable(main main.cpp)
target_link_libraries(main graph)