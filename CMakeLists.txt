cmake_minimum_required(VERSION 3.20)

project(cminus
  VERSION 0.1.0
  DESCRIPTION "A compiler for the C-Minus language (Louden, Appendix A)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_executable(cmc
  src/main.cpp
  src/Diagnostic.cpp
  src/Lexer.cpp
  src/Token.cpp
)

target_include_directories(cmc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(cmc PRIVATE /W4 /permissive-)
else()
  target_compile_options(cmc PRIVATE -Wall -Wextra -Wpedantic)
endif()
