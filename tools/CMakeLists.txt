add_library(graph INTERFACE)
target_compile_features(graph INTERFACE cxx_std_20)
target_include_directories(graph INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")