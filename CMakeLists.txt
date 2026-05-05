cmake_minimum_required(VERSION 3.20)

project(MyLang LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(lang
    src/main.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Semantic.cpp
    src/CodeGen.cpp
)

target_include_directories(lang
    PRIVATE
        "${PROJECT_SOURCE_DIR}/inc"
)

target_compile_options(lang
    PRIVATE
        -Wall
        -Wextra
)

find_package(Clang CONFIG QUIET
    PATHS
        /usr/lib/llvm-18
        /usr/lib/llvm-18/lib/cmake/clang
)

if(Clang_FOUND)
    target_include_directories(lang PRIVATE "${CLANG_INCLUDE_DIRS}")
    target_link_libraries(lang PRIVATE libclang)
else()
    find_path(CLANG_INCLUDE_DIR
        NAMES clang-c/Index.h
        HINTS /usr/lib/llvm-18/include
    )

    find_library(CLANG_LIBRARY
        NAMES clang libclang clang-18 libclang-18
        HINTS /usr/lib/llvm-18/lib
    )

    if(NOT CLANG_INCLUDE_DIR OR NOT CLANG_LIBRARY)
        message(FATAL_ERROR "libclang was not found. Install libclang development files or set CLANG_INCLUDE_DIR and CLANG_LIBRARY.")
    endif()

    target_include_directories(lang PRIVATE "${CLANG_INCLUDE_DIR}")
    target_link_libraries(lang PRIVATE "${CLANG_LIBRARY}")
endif()

find_library(READLINE_LIBRARY NAMES readline REQUIRED)
find_library(NCURSES_LIBRARY NAMES ncurses curses REQUIRED)

target_link_libraries(lang
    PRIVATE
        "${READLINE_LIBRARY}"
        "${NCURSES_LIBRARY}"
)
