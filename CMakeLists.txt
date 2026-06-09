cmake_minimum_required(VERSION 3.16)
project(st2cpp VERSION 0.1.0 LANGUAGES CXX)

set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option for building tests (default OFF)
option(BUILD_TESTS "Build tests" OFF)

# Include directories
set(ST2CPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(ST2CPP_INCLUDES_DIR ${CMAKE_SOURCE_DIR}/st2cpp_includes)

# ============================================================================
#  Compiler flags (conditional for cross-platform support)
# ============================================================================
if(MSVC)
    # Visual Studio / MSVC on Windows
    set(ST2CPP_COMPILE_FLAGS
        /W4           # Warning level 4 (similar to -Wall)
        /utf-8        # UTF-8 support for source files
    )
    # Suppress warnings about "unsafe" CRT functions (fopen, strcpy, etc.)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC / Clang / MinGW on Linux/macOS/Windows
    set(ST2CPP_COMPILE_FLAGS
        -Wall         # Enable all warnings
        -Wextra       # Enable extra warnings
        -Wpedantic    # Strict ISO C++ compliance
    )
endif()

# ============================================================================
#  Static library (core compiler logic)
# ============================================================================
add_library(st2cpp_lib STATIC
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/parser/ParserStmt.cpp
    src/codegen/CodeGenerator.cpp
)

target_include_directories(st2cpp_lib PUBLIC
    ${ST2CPP_INCLUDE_DIR}
    ${ST2CPP_INCLUDES_DIR}
)

target_compile_options(st2cpp_lib PRIVATE
    ${ST2CPP_COMPILE_FLAGS}
)

# ============================================================================
#  CLI executable (main compiler driver)
# ============================================================================
add_executable(st2cpp src/cli/main.cpp)

target_link_libraries(st2cpp PRIVATE st2cpp_lib)

target_include_directories(st2cpp PRIVATE
    ${ST2CPP_INCLUDE_DIR}
    ${ST2CPP_INCLUDES_DIR}
)

target_compile_options(st2cpp PRIVATE
    ${ST2CPP_COMPILE_FLAGS}
)

# ============================================================================
#  Tests (optional, enabled with -DBUILD_TESTS=ON)
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    # Add test directory if you have unit tests
    # add_subdirectory(tests)
endif()

# ============================================================================
#  Installation
# ============================================================================
install(TARGETS st2cpp RUNTIME DESTINATION bin)
install(TARGETS st2cpp_lib ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY st2cpp_includes/ DESTINATION include)

# ============================================================================
#  CPack - Package generation for distribution
# ============================================================================
include(CPack)

set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "st2cpp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Structured Text to C++ Compiler")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VENDOR "undoRT")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# ============================================================================
#  Doxygen documentation (optional)
# ============================================================================
find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
    # Use the existing Doxyfile in the source directory
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    # Copy Doxyfile to build directory (or use it directly)
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} COPYONLY)
    
    # Custom target for generating documentation
    add_custom_target(docs
        ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    message(STATUS "Doxygen: Enabled (documentation will be generated in docs/)")
else()
    message(STATUS "Doxygen: Disabled (install doxygen to generate API documentation)")
endif()

# ============================================================================
#  Print configuration summary
# ============================================================================
message(STATUS "")
message(STATUS "st2cpp Configuration:")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build tests:       ${BUILD_TESTS}")
message(STATUS "  Install prefix:    ${CMAKE_INSTALL_PREFIX}")
if(DOXYGEN_FOUND)
    message(STATUS "  Documentation:     Enabled")
else()
    message(STATUS "  Documentation:     Disabled (Doxygen not found)")
endif()
message(STATUS "")