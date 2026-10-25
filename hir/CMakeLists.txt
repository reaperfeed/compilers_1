add_library(hir STATIC
    hir.cpp
    cfg.cpp
    verifier.cpp
    lowering.cpp
)
target_include_directories(hir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hir PUBLIC tools)
target_include_directories(hir PUBLIC "${PROJECT_SOURCE_DIR}/frontend/src")
target_compile_features(hir PUBLIC cxx_std_20)
