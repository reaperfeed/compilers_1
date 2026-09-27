# Сгенерированные файлы кладём в build/frontend/generated/
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# --- Bison ---
BISON_TARGET(Parser
        "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y"
        "${GENERATED_DIR}/parser.cpp"
        DEFINES_FILE "${GENERATED_DIR}/parser.hpp"
)

# --- Flex ---
FLEX_TARGET(Lexer
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l"
        "${GENERATED_DIR}/lexer.cpp"
)

# lexer.cpp подключает parser.hpp → Flex должен собираться после Bison.
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# --- Библиотека фронтенда (без main) ---
add_library(frontend_lib STATIC
        src/ast.cpp
        src/semantic.cpp
        src/interpreter.cpp
        ${BISON_Parser_OUTPUTS}
        ${FLEX_Lexer_OUTPUTS}
)

target_link_libraries(frontend_lib PUBLIC tools)

target_include_directories(frontend_lib PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"   # для ast.hpp, main.cpp и т.д.
        "${GENERATED_DIR}"                  # для parser.hpp
)

# --- Исполняемый файл ---
add_executable(frontend main.cpp)
target_link_libraries(frontend PRIVATE frontend_lib)