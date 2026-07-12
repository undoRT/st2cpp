cmake_minimum_required(VERSION 3.14)
project(PLC_Test)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add the generated code directory
set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(SOURCES_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================================
# Find dependencies
# ============================================================================

# Find the undoCore library
set(ST2CPP_INCLUDES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../st2cpp_includes CACHE PATH "Path to st2cpp_includes")
set(UNDOCORE_INCLUDE_DIR ${ST2CPP_INCLUDES_DIR}/undoCore/include)

if(EXISTS ${UNDOCORE_INCLUDE_DIR})
    message(STATUS "Found undoCore headers at: ${UNDOCORE_INCLUDE_DIR}")
else()
    find_path(UNDOCORE_INCLUDE_DIR NAMES undoCore/undoCore.hpp)
    if(NOT UNDOCORE_INCLUDE_DIR)
        message(WARNING "Could not find undoCore headers at ${UNDOCORE_INCLUDE_DIR}")
        message(WARNING "Please set ST2CPP_INCLUDES_DIR to the correct path.")
    endif()
endif()

# ============================================================================
# generated code library
# ============================================================================

# Collect all generated source files
file(GLOB_RECURSE GENERATED_SOURCES
    ${GENERATED_DIR}/*.cpp
    ${GENERATED_DIR}/FunctionBlocks/*.cpp
    ${GENERATED_DIR}/Programs/*.cpp
)

file(GLOB_RECURSE GENERATED_HEADERS
    ${GENERATED_DIR}/*.hpp
    ${GENERATED_DIR}/FunctionBlocks/*.hpp
    ${GENERATED_DIR}/Programs/*.hpp
)

# Create a library for the generated code
add_library(PLC_Generated ${GENERATED_SOURCES})

# Include directories
target_include_directories(PLC_Generated
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/FunctionBlocks
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/Programs
        ${UNDOCORE_INCLUDE_DIR}  # AGGIUNGI QUESTO
)

# Link dependencies (se hai una libreria da linkare)
# Compiler options for the generated code
target_compile_definitions(PLC_Generated
    PUBLIC
        ST2CPP_RUNTIME_NAMESPACE=undoCore
)

# ============================================================================
# Main executable
# ============================================================================

add_executable(plc_test main.cpp)

target_link_libraries(plc_test
    PLC_Generated
)

# Include directories for the main executable
target_include_directories(plc_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/FunctionBlocks
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/Programs
        ${UNDOCORE_INCLUDE_DIR}  # AGGIUNGI QUESTO
)

# ============================================================================
# Build options
# ============================================================================

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(plc_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PLC_Generated PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(plc_test PRIVATE /W4)
    target_compile_options(PLC_Generated PRIVATE /W4)
endif()

# ============================================================================
# Print build information
# ============================================================================

message(STATUS "=== PLC Test Project ===")
message(STATUS "Generated code directory: ${GENERATED_DIR}")
message(STATUS "undoCore include directory: ${UNDOCORE_INCLUDE_DIR}")
if(TARGET undoCore)
    message(STATUS "undoCore: Found as subdirectory")
else()
    message(STATUS "undoCore: Header-only (no library)")
endif()
message(STATUS "=========================")

# ============================================================================
# Optional: Add a custom target to run the test
# ============================================================================

add_custom_target(run_test
    COMMAND ${CMAKE_BINARY_DIR}/plc_test
    DEPENDS plc_test
    COMMENT "Running PLC test application"
)