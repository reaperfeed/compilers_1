set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

BISON_TARGET(Parser
        "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y"
        "${GENERATED_DIR}/parser.cpp"
        DEFINES_FILE "${GENERATED_DIR}/parser.hpp"
)

FLEX_TARGET(Lexer
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l"
        "${GENERATED_DIR}/lexer.cpp"
)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

add_library(frontend_lib STATIC
        src/ast.cpp
        src/semantic.cpp
        src/interpreter.cpp
        ${BISON_Parser_OUTPUTS}
        ${FLEX_Lexer_OUTPUTS}
)

target_link_libraries(frontend_lib PUBLIC tools)

target_include_directories(frontend_lib PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GENERATED_DIR}"
)

add_executable(frontend main.cpp)
target_link_libraries(frontend PRIVATE frontend_lib)