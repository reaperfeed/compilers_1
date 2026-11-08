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
add_executable(lir_tests lir_tests.cpp)
target_link_libraries(lir_tests PRIVATE lir Catch2::Catch2WithMain)
catch_discover_tests(lir_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME frontend_integration
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration.py"
                "$<TARGET_FILE:frontend>" "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Fixture programs exercise LIR features not exposed by the source grammar.
add_executable(codegen_fixtures codegen_fixtures.cpp)
target_link_libraries(codegen_fixtures PRIVATE lir)

if(UNIX AND Python3_Interpreter_FOUND)
    find_program(CHIBICC_EXECUTABLE NAMES chibicc
        HINTS "$ENV{HOME}/compiler-tools/chibicc"
        DOC "chibicc used to compile and execute generated C")
    if(CHIBICC_EXECUTABLE)
        add_test(NAME c_codegen_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/codegen_integration.py"
                "$<TARGET_FILE:frontend>" "${PROJECT_SOURCE_DIR}"
                "${CHIBICC_EXECUTABLE}" "$<TARGET_FILE:codegen_fixtures>")
        set_tests_properties(c_codegen_integration PROPERTIES TIMEOUT 300)
        add_test(NAME optimized_c_codegen_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/codegen_integration.py"
                "$<TARGET_FILE:frontend>" "${PROJECT_SOURCE_DIR}"
                "${CHIBICC_EXECUTABLE}" "$<TARGET_FILE:codegen_fixtures>" --optimize)
        set_tests_properties(optimized_c_codegen_integration PROPERTIES TIMEOUT 300)
    else()
        message(STATUS "C codegen execution tests need -DCHIBICC_EXECUTABLE=/path/to/chibicc")
    endif()
endif()

add_executable(optimizer_tests optimizer_tests.cpp)
target_link_libraries(optimizer_tests PRIVATE hir Catch2::Catch2WithMain)
catch_discover_tests(optimizer_tests)
