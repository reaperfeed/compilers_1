find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_executable(graph_tests
        tests.cpp
)

target_link_libraries(graph_tests
        PRIVATE
        tools
        Catch2::Catch2WithMain
        frontend_lib
)

include(CTest)
include(Catch)

catch_discover_tests(graph_tests)
add_executable(hir_tests hir_tests.cpp)
target_link_libraries(hir_tests PRIVATE hir Catch2::Catch2WithMain)
catch_discover_tests(hir_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME frontend_integration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration.py"
                "$<TARGET_FILE:frontend>" "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
