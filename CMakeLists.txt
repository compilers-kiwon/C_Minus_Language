cmake_minimum_required(VERSION 3.20)

project(cminus
  VERSION 0.1.3
  DESCRIPTION "A compiler for the C-Minus language (Louden, Appendix A)"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CMINUS_WERROR "Treat compiler warnings as errors" OFF)
option(CMINUS_SANITIZERS "Build with the address and undefined-behaviour sanitizers" OFF)

# The compiler and the runtime end up on different machines whenever code is
# built for anything but the host: cmc runs where the build runs, while
# libcminus_rt.a is linked into programs for the target. A cross build system
# therefore builds them separately, with different toolchains, and only the
# compiler half needs LLVM.
option(CMINUS_BUILD_COMPILER "Build the cmc compiler (requires LLVM)" ON)
option(CMINUS_BUILD_RUNTIME "Build libcminus_rt for the current toolchain" ON)
# A second code generator that goes through MLIR, for learning MLIR; off by
# default so that nothing else depends on it.
option(CMINUS_ENABLE_MLIR "Add the MLIR path to cmc (--use-mlir; requires MLIR)" OFF)

if(NOT CMINUS_BUILD_COMPILER AND NOT CMINUS_BUILD_RUNTIME)
  message(FATAL_ERROR
    "Nothing to build: CMINUS_BUILD_COMPILER and CMINUS_BUILD_RUNTIME are both off.")
endif()

if(CMINUS_BUILD_COMPILER)
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

  # The MLIR libraries link against the LLVM they were built with. When that
  # is the shared libLLVM, cmc has to use it too, or LLVM's static registries
  # (command-line options, targets) end up in the process twice.
  if(CMINUS_ENABLE_MLIR AND LLVM_LINK_LLVM_DYLIB)
    set(CMINUS_LLVM_LIBS LLVM)
  else()
    # Every back end LLVM was built with, not just the host's, so that
    # --target can name any of them. This is most of cmc's size.
    llvm_map_components_to_libnames(CMINUS_LLVM_LIBS
      core support passes ${LLVM_TARGETS_TO_BUILD})
  endif()

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

  if(CMINUS_ENABLE_MLIR)
    find_package(MLIR REQUIRED CONFIG)
    message(STATUS "Using MLIR from ${MLIR_DIR}")

    # MLIRConfig names mlir-tblgen without a path, counting on an imported
    # target of that name. LLVM 21's add_tablegen exported one; 23's does not,
    # so fall back to the installed program.
    if(NOT TARGET ${MLIR_TABLEGEN_EXE})
      find_program(CMINUS_MLIR_TBLGEN ${MLIR_TABLEGEN_EXE}
                   HINTS "${MLIR_INSTALL_PREFIX}/bin" REQUIRED NO_CMAKE_FIND_ROOT_PATH)
      set(MLIR_TABLEGEN_EXE "${CMINUS_MLIR_TBLGEN}")
    endif()

    # The cminus dialect is written in TableGen (ODS); mlir-tblgen generates
    # its C++ into the build directory whenever the .td file changes.
    list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
    include(AddLLVM) # TableGen.cmake calls its get_subproject_title
    include(AddMLIR)
    set(LLVM_TARGET_DEFINITIONS include/cminus/CMinusOps.td)
    mlir_tablegen(CMinusDialect.h.inc -gen-dialect-decls -dialect=cminus
                  EXTRA_INCLUDES ${MLIR_INCLUDE_DIRS})
    mlir_tablegen(CMinusDialect.cpp.inc -gen-dialect-defs -dialect=cminus
                  EXTRA_INCLUDES ${MLIR_INCLUDE_DIRS})
    mlir_tablegen(CMinusOps.h.inc -gen-op-decls EXTRA_INCLUDES ${MLIR_INCLUDE_DIRS})
    mlir_tablegen(CMinusOps.cpp.inc -gen-op-defs EXTRA_INCLUDES ${MLIR_INCLUDE_DIRS})
    add_public_tablegen_target(CMinusOpsIncGen)
    add_dependencies(cmc CMinusOpsIncGen)

    target_sources(cmc PRIVATE
      src/CMinusDialect.cpp src/CMinusToCF.cpp src/MLIRGen.cpp src/MLIRLower.cpp)
    # SYSTEM: the generated .inc files trip -Wunused-parameter, as MLIR's own do.
    target_include_directories(cmc SYSTEM PRIVATE
      ${CMAKE_CURRENT_BINARY_DIR} ${MLIR_INCLUDE_DIRS})
    target_compile_definitions(cmc PRIVATE CMINUS_HAVE_MLIR)
    target_link_libraries(cmc PRIVATE
      MLIRIR MLIRPass
      MLIRArithDialect MLIRControlFlowDialect MLIRFuncDialect
      MLIRLLVMDialect MLIRMemRefDialect
      MLIRArithToLLVM MLIRControlFlowToLLVM MLIRFuncToLLVM MLIRMemRefToLLVM
      MLIRReconcileUnrealizedCasts MLIRTransformUtils
      MLIRBuiltinToLLVMIRTranslation MLIRLLVMToLLVMIRTranslation
      MLIRTargetLLVMIRExport)
  endif()

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

  install(TARGETS cmc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CMINUS_BUILD_RUNTIME)
  # Definitions of input, output and the negative-subscript handler, linked
  # into every compiled C- program.
  add_library(cminus_rt STATIC runtime/cminus_rt.c)
  install(TARGETS cminus_rt ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# The suites all drive cmc, so there is nothing to run in a runtime-only build.
if(CMINUS_BUILD_COMPILER)
  enable_testing()
  add_test(NAME golden
    COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
            bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh)
  if(CMINUS_BUILD_RUNTIME)
    add_test(NAME execution
      COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
              CMINUS_RT=$<TARGET_FILE:cminus_rt>
              bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_tests.sh)
    if(CMINUS_ENABLE_MLIR)
      # The same programs through both MLIR paths, so all three must agree.
      add_test(NAME execution-mlir-std
        COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
                CMINUS_RT=$<TARGET_FILE:cminus_rt>
                CMC_FLAGS=--use-mlir=std
                bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_tests.sh)
      add_test(NAME execution-mlir-cminus
        COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
                CMINUS_RT=$<TARGET_FILE:cminus_rt>
                CMC_FLAGS=--use-mlir=cminus
                bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_exec_tests.sh)
    endif()
  endif()
  # Skips itself when no cross toolchain or emulator is installed.
  add_test(NAME cross
    COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
            bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cross_tests.sh)
  if(CMINUS_ENABLE_MLIR)
    foreach(flavor std cminus)
      add_test(NAME cross-mlir-${flavor}
        COMMAND ${CMAKE_COMMAND} -E env CMC=$<TARGET_FILE:cmc>
                CMC_FLAGS=--use-mlir=${flavor}
                bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cross_tests.sh)
    endforeach()
  endif()
endif()
