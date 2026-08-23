cmake_minimum_required(VERSION 3.20)

project(cminus
  VERSION 0.1.0
  DESCRIPTION "A compiler for the C-Minus language (Louden, Appendix A)"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CMINUS_WERROR "Treat compiler warnings as errors" OFF)
option(CMINUS_SANITIZERS "Build with the address and undefined-behaviour sanitizers" OFF)

find_package(LLVM REQUIRED CONFIG)

# A version floor rather than find_package(LLVM 21 ...): LLVM's config file
# treats a requested version as exact down to the minor number, so asking for
# "21" rejects 21.1. The floor is 21 because the code generator uses the
# Triple-based overloads of TargetRegistry::lookupTarget,
# TargetMachine::createTargetMachine and Module::setTargetTriple, which
# replaced the string ones in that release.
if(LLVM_PACKAGE_VERSION VERSION_LESS 21)
  message(FATAL_ERROR
    "LLVM 21 or newer is required, but ${LLVM_PACKAGE_VERSION} was found at "
    "${LLVM_DIR}. Point CMake at a newer one with -DLLVM_DIR=<path>.")
endif()

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

llvm_map_components_to_libnames(CMINUS_LLVM_LIBS
  core support passes native nativecodegen)

add_executable(cmc
  src/main.cpp
  src/AST.cpp
  src/Diagnostic.cpp
  src/Emit.cpp
  src/IRGen.cpp
  src/Lexer.cpp
  src/Link.cpp
  src/Parser.cpp
  src/SemanticAnalyzer.cpp
  src/Symbol.cpp
  src/Token.cpp
)

target_include_directories(cmc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(cmc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

separate_arguments(CMINUS_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cmc PRIVATE ${CMINUS_LLVM_DEFINITIONS})
target_link_libraries(cmc PRIVATE ${CMINUS_LLVM_LIBS})

if(MSVC)
  target_compile_options(cmc PRIVATE /W4 /permissive-)
  if(CMINUS_WERROR)
    target_compile_options(cmc PRIVATE /WX)
  endif()
else()
  target_compile_options(cmc PRIVATE -Wall -Wextra -Wpedantic)
  if(CMINUS_WERROR)
    target_compile_options(cmc PRIVATE -Werror)
  endif()
endif()

if(CMINUS_SANITIZERS)
  # -fno-sanitize-recover makes a report fail the run, so CI notices.
  set(CMINUS_SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_compile_options(cmc PRIVATE ${CMINUS_SANITIZER_FLAGS})
  target_link_options(cmc PRIVATE ${CMINUS_SANITIZER_FLAGS})
endif()

# LLVM is often built without RTTI; a translation unit that includes its
# headers has to agree.
if(NOT LLVM_ENABLE_RTTI AND NOT MSVC)
  target_compile_options(cmc PRIVATE -fno-rtti)
endif()

# Definitions of input, output and the negative-subscript handler, linked
# into every compiled C- program.
add_library(cminus_rt STATIC runtime/cminus_rt.c)

enable_testing()
add_test(NAME golden
  COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh)
add_test(NAME execution
  COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
          CMINUS_RT=$<TARGET_FILE:cminus_rt>
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_tests.sh)
