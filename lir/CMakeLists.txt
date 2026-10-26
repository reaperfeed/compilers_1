add_library(lir STATIC
    lir.cpp
    lowering.cpp
)
target_include_directories(lir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}")
target_link_libraries(lir PUBLIC hir)
target_compile_features(lir PUBLIC cxx_std_20)
