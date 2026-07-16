cmake_minimum_required(VERSION 3.10)
project(st2cpp VERSION 0.2.1 LANGUAGES CXX)

set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
#  Options
# ============================================================================
option(BUILD_TESTS "Build tests" OFF)
option(ST2CPP_USE_EXTERNAL_UNDOCORE "Use system undoCore instead of submodule" OFF)
option(ST2CPP_BUNDLE_UNDOCORE "Bundle undoCore headers in installation" ON)

# ============================================================================
#  Find or use submodule undoCore
# ============================================================================
if(ST2CPP_USE_EXTERNAL_UNDOCORE)
    find_package(undoCore REQUIRED)
    set(UNDOCORE_INCLUDE_DIR ${undoCore_INCLUDE_DIRS})
    set(UNDOCORE_FOUND TRUE)
else()
    # Use submodule
    set(UNDOCORE_SUBMODULE_DIR ${CMAKE_SOURCE_DIR}/st2cpp_includes/undoCore)
    
    if(NOT EXISTS ${UNDOCORE_SUBMODULE_DIR}/CMakeLists.txt)
        message(WARNING "undoCore submodule not found! Run: git submodule update --init")
        message(WARNING "Falling back to header-only mode (no undoCore target)")
        set(UNDOCORE_FOUND FALSE)
        set(UNDOCORE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/st2cpp_includes/undoCore/include)
    else()
        # Try to use undoCore as a CMake subproject
        set(UNDOCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(UNDOCORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory(${UNDOCORE_SUBMODULE_DIR} ${CMAKE_BINARY_DIR}/undoCore)
        set(UNDOCORE_FOUND TRUE)
        set(UNDOCORE_INCLUDE_DIR ${UNDOCORE_SUBMODULE_DIR}/include)
    endif()
endif()

# ============================================================================
#  Include directories
# ============================================================================
set(ST2CPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# ============================================================================
#  Compiler flags (conditional for cross-platform support)
# ============================================================================
if(MSVC)
    # Visual Studio / MSVC on Windows
    set(ST2CPP_COMPILE_FLAGS
        /W4           # Warning level 4 (similar to -Wall)
        /utf-8        # UTF-8 support for source files
        /EHsc         # C++ exception handling
    )
    # Suppress warnings about "unsafe" CRT functions (fopen, strcpy, etc.)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # GCC / Clang / MinGW on Linux/macOS/Windows
    set(ST2CPP_COMPILE_FLAGS
        -Wall         # Enable all warnings
        -Wextra       # Enable extra warnings
        -Wpedantic    # Strict ISO C++ compliance
        -Wno-unused-parameter
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
    ${UNDOCORE_INCLUDE_DIR}
)

target_compile_options(st2cpp_lib PRIVATE
    ${ST2CPP_COMPILE_FLAGS}
)

# If undoCore is available as a CMake target, link it
if(TARGET undoCore)
    target_link_libraries(st2cpp_lib PUBLIC undoCore)
    message(STATUS "undoCore: Linked as CMake target")
elseif(UNDOCORE_FOUND)
    # Header-only mode
    message(STATUS "undoCore: Header-only mode (no CMake target)")
else()
    message(WARNING "undoCore: Not found! Some features may not work.")
endif()

# ============================================================================
#  CLI executable (main compiler driver)
# ============================================================================
add_executable(st2cpp src/cli/main.cpp)

target_link_libraries(st2cpp PRIVATE st2cpp_lib)

target_include_directories(st2cpp PRIVATE
    ${ST2CPP_INCLUDE_DIR}
    ${UNDOCORE_INCLUDE_DIR}
)

target_compile_options(st2cpp PRIVATE
    ${ST2CPP_COMPILE_FLAGS}
)

# ============================================================================
#  Tests (optional, enabled with -DBUILD_TESTS=ON)
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    
    # Find or download Google Test
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "Google Test not found, downloading via FetchContent")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        )
        # For Windows: disable MSVC warnings from googletest
        if(MSVC)
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_MakeAvailable(googletest)
    else()
        message(STATUS "Google Test found: ${GTest_INCLUDE_DIRS}")
    endif()
    
    # Create the test executable
    add_executable(st2cpp_tests
        tests/test_main.cpp
        tests/test_lexer.cpp
        tests/test_parser.cpp
        tests/test_allocator.cpp
        tests/test_scope.cpp
        tests/test_generation.cpp
        tests/test_golden.cpp
        tests/test_compilation.cpp
        tests/test_process_image.cpp         
        tests/test_complex_program.cpp  
        tests/helpers/TestHelper.cpp
    )

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_samples/ 
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples/)

    # Link libraries
    target_link_libraries(st2cpp_tests
        st2cpp_lib
        GTest::gtest_main
        # pthread is required on Linux/Unix, included automatically via GTest on Windows
    )

    # Include directories for tests
    target_include_directories(st2cpp_tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers
        ${UNDOCORE_INCLUDE_DIR}
    )
    
    # Compiler options for tests
    target_compile_options(st2cpp_tests PRIVATE
        ${ST2CPP_COMPILE_FLAGS}
    )
    
    # Copy ST sample files to build directory for golden tests
    file(GLOB ST_SAMPLES "${CMAKE_CURRENT_SOURCE_DIR}/tests/st_samples/*.st")
    set(ST_SAMPLE_FILES "")
    foreach(SAMPLE ${ST_SAMPLES})
        get_filename_component(NAME ${SAMPLE} NAME_WE)
        set(DEST ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples/${NAME}.st)
        add_custom_command(
            OUTPUT ${DEST}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples
            COMMAND ${CMAKE_COMMAND} -E copy ${SAMPLE} ${DEST}
            DEPENDS ${SAMPLE}
            COMMENT "Copying ST sample: ${NAME}.st"
        )
        list(APPEND ST_SAMPLE_FILES ${DEST})
    endforeach()
    
    # Copia anche i file golden
    file(GLOB GOLDEN_CPP "${CMAKE_CURRENT_SOURCE_DIR}/tests/st_samples/*.golden.cpp")
    foreach(GOLDEN ${GOLDEN_CPP})
        get_filename_component(NAME ${GOLDEN} NAME)
        set(DEST ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples/${NAME})
        add_custom_command(
            OUTPUT ${DEST}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples
            COMMAND ${CMAKE_COMMAND} -E copy ${GOLDEN} ${DEST}
            DEPENDS ${GOLDEN}
            COMMENT "Copying golden CPP: ${NAME}"
        )
        list(APPEND ST_SAMPLE_FILES ${DEST})
    endforeach()
    
    file(GLOB GOLDEN_HPP "${CMAKE_CURRENT_SOURCE_DIR}/tests/st_samples/*.golden.hpp")
    foreach(GOLDEN ${GOLDEN_HPP})
        get_filename_component(NAME ${GOLDEN} NAME)
        set(DEST ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples/${NAME})
        add_custom_command(
            OUTPUT ${DEST}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples
            COMMAND ${CMAKE_COMMAND} -E copy ${GOLDEN} ${DEST}
            DEPENDS ${GOLDEN}
            COMMENT "Copying golden HPP: ${NAME}"
        )
        list(APPEND ST_SAMPLE_FILES ${DEST})
    endforeach()
    
    add_custom_target(st_samples ALL DEPENDS ${ST_SAMPLE_FILES})
    add_dependencies(st2cpp_tests st_samples)
    
    # Passa la directory dei sample al test eseguibile
    target_compile_definitions(st2cpp_tests PRIVATE
        ST_SAMPLES_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples"
    )
    
    # Registra i test con CTest
    add_test(NAME st2cpp_tests COMMAND st2cpp_tests)
    
    message(STATUS "Tests: Enabled (Google Test)")
endif()

# ============================================================================
#  Installation
# ============================================================================
install(TARGETS st2cpp RUNTIME DESTINATION bin)
install(TARGETS st2cpp_lib ARCHIVE DESTINATION lib)

# Install st2cpp headers
install(DIRECTORY include/ DESTINATION include/st2cpp
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

# Install undoCore headers if bundling is enabled
if(ST2CPP_BUNDLE_UNDOCORE)
    install(DIRECTORY st2cpp_includes/undoCore/include/ 
        DESTINATION include/undoCore
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
    )
    message(STATUS "undoCore: Will be bundled in installation")
endif()

# Install runtime headers
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

# Include submodules in CPack
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.github/"
    "/build/"
    "/CMakeCache.txt"
    "/CMakeFiles/"
)

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
message(STATUS "  undoCore:          ${UNDOCORE_FOUND}")
if(UNDOCORE_FOUND AND TARGET undoCore)
    message(STATUS "  undoCore mode:     CMake target")
elseif(UNDOCORE_FOUND)
    message(STATUS "  undoCore mode:     Header-only")
else()
    message(STATUS "  undoCore mode:     NOT FOUND")
endif()
message(STATUS "  Bundle undoCore:   ${ST2CPP_BUNDLE_UNDOCORE}")
if(DOXYGEN_FOUND)
    message(STATUS "  Documentation:     Enabled")
else()
    message(STATUS "  Documentation:     Disabled (Doxygen not found)")
endif()
message(STATUS "")