cmake_minimum_required(VERSION 3.20)
project(asc LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Find MLIR
find_package(MLIR REQUIRED CONFIG)
message(STATUS "Found MLIR ${MLIR_PACKAGE_VERSION}")
message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")

list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR}/include)

add_definitions(${LLVM_DEFINITIONS})

# Compiler flags
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

add_subdirectory(lib/Basic)
add_subdirectory(lib/Lex)
add_subdirectory(lib/Parse)
add_subdirectory(lib/AST)
add_subdirectory(lib/Sema)
add_subdirectory(lib/HIR)
add_subdirectory(lib/Analysis)
add_subdirectory(lib/CodeGen)
add_subdirectory(lib/Driver)
add_subdirectory(lib/Runtime)
add_subdirectory(tools/asc)

# Testing
option(ASC_BUILD_TESTS "Build asc unit tests" ON)
if(ASC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(unittests)
endif()
