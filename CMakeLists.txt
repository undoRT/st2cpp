cmake_minimum_required(VERSION 3.16)
project(st2cpp VERSION 0.5.0 LANGUAGES CXX)

set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")

# C++17 across the transpiler, the tests and the generated PLC. undoPLC needed
# C++20 only for std::latch, which v0.2.2 replaced with UndoLatch, so undoTasks.hpp
# is C++17 now. The transpiler only ever writes that header name into the output,
# but sharing one standard across the compiler and its output keeps a single rule
# for every translation unit.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
#  Options
# ============================================================================
option(BUILD_TESTS "Build tests" OFF)
option(ST2CPP_USE_EXTERNAL_UNDOPLC "Use an installed undoPLC instead of the submodule" OFF)
option(ST2CPP_BUNDLE_UNDOPLC "Bundle undoPLC/undoCore headers in the installation" ON)
# undoPLC vendors all of Boost (~190 MB) because undoLog.hpp needs Boost.Asio.
# St2cpp never compiles against it, so it is not installed unless asked for.
option(ST2CPP_BUNDLE_BOOST "Also bundle the vendored Boost headers (large)" OFF)

# ============================================================================
#  Locate undoPLC (submodule by default) and derive the runtime include path
#
#  St2cpp links undoPLC headers only. The undoPLC *library* belongs to the
#  generated PLC application, which links it itself; the transpiler merely has
#  to put the headers on the include path so its tests can compile the code it
#  emits. undoPLC is deliberately not added with add_subdirectory(): its
#  CMakeLists resolves its own paths through CMAKE_SOURCE_DIR, which is the
#  st2cpp root when it is consumed as a subproject and would point at the wrong
#  directories.
# ============================================================================
if(ST2CPP_USE_EXTERNAL_UNDOPLC)
    find_package(undoPLC REQUIRED)
    set(UNDOPLC_ROOT_DIR ${undoPLC_ROOT_DIR})
    set(UNDOPLC_INCLUDE_DIR ${undoPLC_INCLUDE_DIRS})
    set(UNDOPLC_FOUND TRUE)
else()
    set(UNDOPLC_ROOT_DIR ${CMAKE_SOURCE_DIR}/st2cpp_includes/undoPLC)
    set(UNDOPLC_INCLUDE_DIR ${UNDOPLC_ROOT_DIR}/include)

    if(NOT EXISTS ${UNDOPLC_INCLUDE_DIR}/undoTasks.hpp)
        message(WARNING "undoPLC submodule not found! Run: git submodule update --init --recursive")
        set(UNDOPLC_FOUND FALSE)
    else()
        set(UNDOPLC_FOUND TRUE)
    endif()
endif()

# ============================================================================
#  Include directories
# ============================================================================
set(ST2CPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# undoCore reaches the generated code transitively through undoPLC, which keeps
# it as its own submodule, and undoPLC vendors Boost next to it. Both therefore
# hang off the undoPLC root, whichever way that root was found.
set(UNDOCORE_INCLUDE_DIR ${UNDOPLC_ROOT_DIR}/third_party/undoCore/include)
set(UNDOPLC_BOOST_INCLUDE_DIR ${UNDOPLC_ROOT_DIR}/third_party/boost_1_91_0)

# Every directory the emitted code may include from, in include-path order.
set(ST2CPP_RUNTIME_INCLUDE_DIRS
    ${ST2CPP_INCLUDE_DIR}
    ${UNDOPLC_INCLUDE_DIR}
)
if(EXISTS ${UNDOCORE_INCLUDE_DIR}/undoCore)
    list(APPEND ST2CPP_RUNTIME_INCLUDE_DIRS ${UNDOCORE_INCLUDE_DIR})
endif()
if(EXISTS ${UNDOPLC_BOOST_INCLUDE_DIR}/boost)
    list(APPEND ST2CPP_RUNTIME_INCLUDE_DIRS ${UNDOPLC_BOOST_INCLUDE_DIR})
endif()

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
    src/codegen/ProjectEmitter.cpp
    src/codegen/RuntimeEmitter.cpp
    src/codegen/DeclEmitter.cpp
    src/codegen/BodyEmitter.cpp
    src/codegen/SemanticBridge.cpp
    src/codegen/LibraryIncludeTracker.cpp
    src/codegen/DependencyOrdering.cpp
    src/codegen/TypeMapper.cpp
    src/codegen/ProcessImage.cpp
    src/codegen/ScopeManager.cpp
    src/codegen/DiagEmitter.cpp
)

target_include_directories(st2cpp_lib PUBLIC
    ${ST2CPP_RUNTIME_INCLUDE_DIRS}
)

target_compile_options(st2cpp_lib PRIVATE
    ${ST2CPP_COMPILE_FLAGS}
)

# Semantic analysis library
add_subdirectory(include/semantic)

# JSON module library
add_subdirectory(include/json)

# Library Descriptor module library
add_subdirectory(include/library)

# Project Configuration module library
add_subdirectory(include/project)

# Task Configuration module library
add_subdirectory(include/task)

# The runtime emitter groups tasks by PLC instance (task::groupByPlc), so the
# transpiler depends on the task module and not only the driver.
target_link_libraries(st2cpp_lib PUBLIC st2cpp_task)

# undoPLC headers are all the generated runtime needs from this side; the
# application links the undoPLC library. Threads are required because
# undoTasks.hpp pulls in <thread> and pthread.h.
find_package(Threads REQUIRED)

# ============================================================================
#  CLI executable (main compiler driver)
# ============================================================================
add_executable(st2cpp src/cli/main.cpp)

target_link_libraries(st2cpp PRIVATE st2cpp_semantic st2cpp_lib st2cpp_project st2cpp_task)

target_include_directories(st2cpp PRIVATE
    ${ST2CPP_RUNTIME_INCLUDE_DIRS}
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
        tests/semantic/test_diagnostics.cpp
        tests/semantic/test_source_location.cpp
        tests/semantic/test_symbol_table.cpp
        tests/semantic/test_type_system.cpp
        tests/semantic/test_decl_visitor.cpp
        tests/semantic/test_body_visitor.cpp
        tests/semantic/test_iec_time.cpp
        tests/semantic/test_case_sensitivity.cpp
        tests/semantic/test_codegen_semantic.cpp
        tests/library/test_json.cpp
        tests/library/test_library_loader.cpp
        tests/library/test_library_registry.cpp
        tests/library/test_library_descriptor_builder.cpp
        tests/library/test_cli_export.cpp
        tests/project/test_project_config_loader.cpp
        tests/project/test_project_loader.cpp
        tests/task/test_task_config_loader.cpp
        tests/codegen/test_codegen_runtime.cpp
        tests/semantic/test_semantic_library.cpp
        tests/codegen/test_codegen_external_library.cpp
        tests/codegen/test_codegen_diag.cpp
    )

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/st_samples/ 
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples/)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/library/data/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/library/data/)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/project/data/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/project/data/)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tests/task/data/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/tests/task/data/)

    # Link libraries
    target_link_libraries(st2cpp_tests
        st2cpp_lib
        st2cpp_semantic
        st2cpp_library
        st2cpp_json
        st2cpp_project
        st2cpp_task
        GTest::gtest_main
        # The generated undoPLC runtime compiles in-process (test_compilation.cpp),
        # so the tests need the same real-time headers and pthread the runtime
        # application links against.
        Threads::Threads
    )

    # Include directories for tests
    target_include_directories(st2cpp_tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${ST2CPP_RUNTIME_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/helpers
    )
    
    # Compiler options for tests
    target_compile_options(st2cpp_tests PRIVATE
        ${ST2CPP_COMPILE_FLAGS}
    )

    # Path of the CLI binary injected into the CLI integration tests
    target_compile_definitions(st2cpp_tests PRIVATE
        ST2CPP_CLI_PATH="${CMAKE_CURRENT_BINARY_DIR}/st2cpp"
        # Repo root, so a test that compiles generated code can point the
        # generated-code compiler at the real undoDiag.hpp and Boost instead of a
        # hand-written stub (see TestHelper::compileSources).
        ST2CPP_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    
    # Copy ST sample files to build directory for golden tests.
    # In an in-source build (binary dir == source dir) the copies below would
    # target a file onto itself and fail, so they are skipped: the samples are
    # already where the tests expect them.
    if(NOT CMAKE_CURRENT_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
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
    endif()
    
    # Passa la directory dei sample al test eseguibile
    target_compile_definitions(st2cpp_tests PRIVATE
        ST_SAMPLES_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/st_samples"
        LIBRARY_SAMPLES_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/library/data"
        PROJECT_SAMPLES_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/project/data"
        TASK_SAMPLES_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/task/data"
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

# The generated PLC application includes <undoTasks.hpp> (which pulls in
# undoCore) and needs Boost.Asio for the logger. Only undoPLC and undoCore are
# bundled by default; the vendored Boost tree is ~190 MB, so it ships only on
# request and otherwise the application is expected to provide its own.
if(ST2CPP_BUNDLE_UNDOPLC)
    install(DIRECTORY ${UNDOPLC_INCLUDE_DIR}/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
    )
    if(EXISTS ${UNDOCORE_INCLUDE_DIR}/undoCore)
        install(DIRECTORY ${UNDOCORE_INCLUDE_DIR}/ DESTINATION include
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
        )
    endif()

    # Headers alone do not link: the Master/Worker classes the generated
    # Runtime.cpp derives from are implemented in libundoPLC.a. Build it from
    # the same sources undoPLC's own CMakeLists uses so an installed st2cpp can
    # produce a runnable PLC without a second checkout. undoCore is
    # header-only, so nothing else is needed.
    if(EXISTS ${UNDOPLC_ROOT_DIR}/src/undoTasks.cpp)
        add_library(st2cpp_undoplc STATIC
            ${UNDOPLC_ROOT_DIR}/src/undoSystem.cpp
            ${UNDOPLC_ROOT_DIR}/src/undoLog.cpp
            ${UNDOPLC_ROOT_DIR}/src/undoTasks.cpp
        )
        target_include_directories(st2cpp_undoplc PUBLIC
            ${UNDOPLC_INCLUDE_DIR}
            ${UNDOCORE_INCLUDE_DIR}
            ${UNDOPLC_BOOST_INCLUDE_DIR}
        )
        target_compile_features(st2cpp_undoplc PUBLIC cxx_std_17)
        set_target_properties(st2cpp_undoplc PROPERTIES
            CXX_STANDARD 17
            OUTPUT_NAME undoPLC
            POSITION_INDEPENDENT_CODE ON
        )
        # undoPLC uses std::thread, sched_setaffinity and clock_nanosleep.
        find_package(Threads REQUIRED)
        target_link_libraries(st2cpp_undoplc PUBLIC Threads::Threads rt)

        install(TARGETS st2cpp_undoplc
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin
        )
        message(STATUS "undoPLC: static library will be bundled in installation")
    else()
        message(WARNING "undoPLC sources not found under ${UNDOPLC_ROOT_DIR}/src: "
                        "the installation will ship headers only, and generated PLCs "
                        "will need libundoPLC from elsewhere")
    endif()
    message(STATUS "undoPLC: Will be bundled in installation")
endif()

if(ST2CPP_BUNDLE_BOOST)
    # Boost.Asio #includes .ipp implementation fragments from its headers
    # (BOOST_ASIO_SEPARATE_COMPILATION off), so the install has to carry those
    # too or the generated PLC will not compile against it.
    install(DIRECTORY ${UNDOPLC_BOOST_INCLUDE_DIR}/ DESTINATION include
        FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h"
        PATTERN "*.ipp"
        PATTERN "*.inc"
    )
    message(STATUS "Boost: Will be bundled in installation (large)")
elseif(EXISTS ${UNDOPLC_BOOST_INCLUDE_DIR}/boost)
    # undoLog.hpp reaches into Boost.Asio, so a PLC generated from an install
    # that skips Boost only builds where the target system provides its own.
    message(WARNING "Boost will NOT be bundled, but undoLog.hpp includes <boost/asio/...>. "
                    "PLCs generated from this installation need a system-provided Boost "
                    "(same major version, plus Boost.Lockfree). Re-run with "
                    "-DST2CPP_BUNDLE_BOOST=ON to ship the vendored tree.")
endif()

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
if(UNDOPLC_FOUND)
    message(STATUS "  undoPLC:           ${UNDOPLC_INCLUDE_DIR}")
    if(EXISTS ${UNDOCORE_INCLUDE_DIR}/undoCore)
        message(STATUS "  undoCore:          ${UNDOCORE_INCLUDE_DIR} (via undoPLC)")
    else()
        message(WARNING "  undoCore:          not found — run: git submodule update --init --recursive")
    endif()
    if(EXISTS ${UNDOPLC_BOOST_INCLUDE_DIR}/boost)
        message(STATUS "  Boost:             ${UNDOPLC_BOOST_INCLUDE_DIR}")
    else()
        message(WARNING "  Boost:             not found — undoTasks.hpp needs Boost.Asio")
    endif()
else()
    message(WARNING "  undoPLC:           NOT FOUND! Run: git submodule update --init --recursive")
endif()
message(STATUS "  Bundle undoPLC:    ${ST2CPP_BUNDLE_UNDOPLC}")
message(STATUS "  Bundle Boost:      ${ST2CPP_BUNDLE_BOOST}")
if(DOXYGEN_FOUND)
    message(STATUS "  Documentation:     Enabled")
else()
    message(STATUS "  Documentation:     Disabled (Doxygen not found)")
endif()
message(STATUS "")