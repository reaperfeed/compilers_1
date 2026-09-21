include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(Catch2)

add_executable(graph_tests
        tests.cpp
)

target_link_libraries(graph_tests
        PRIVATE
        graph
        Catch2::Catch2WithMain
)

include(CTest)
include(Catch)

catch_discover_tests(graph_tests)