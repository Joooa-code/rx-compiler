cmake_minimum_required(VERSION 3.16)

project(rx_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ANTLR_GENERATED_DIR
    ${CMAKE_SOURCE_DIR}/generated/antlr
)

add_executable(rx-compiler
    src/main.cpp
    src/ast.h
    src/ast.cpp

    ${ANTLR_GENERATED_DIR}/RxLexer.cpp
    ${ANTLR_GENERATED_DIR}/RxParser.cpp
    ${ANTLR_GENERATED_DIR}/RxParserVisitor.cpp
    ${ANTLR_GENERATED_DIR}/RxParserBaseVisitor.cpp
)

target_include_directories(rx-compiler PRIVATE
    ${ANTLR_GENERATED_DIR}
    /usr/local/include/antlr4-runtime
)

target_link_directories(rx-compiler PRIVATE
    /usr/local/lib
)

target_link_libraries(rx-compiler PRIVATE
    antlr4-runtime
)