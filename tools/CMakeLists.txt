add_library(tools INTERFACE)

target_include_directories(tools INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(tools INTERFACE cxx_std_20)

option(ENABLE_DEBUG_ASSERTS "Enable GRAPH_ASSERT checks" OFF)
if(ENABLE_DEBUG_ASSERTS)
    target_compile_definitions(tools INTERFACE DEBUG)
endif()
