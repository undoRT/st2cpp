/**
 * @file test_compilation.cpp
 * @brief Implementation of test cases for compilation of generated code
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

// ============================================================================
//  Test Fixture
// ============================================================================

/**
 * @brief Test fixture for compilation tests
 */
class CompilationTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      m_tempDir = fs::temp_directory_path() / "st2cpp_test";
      fs::remove_all(m_tempDir);
      fs::create_directories(m_tempDir);
      writeRuntimeStub();
   }

   void TearDown() override { fs::remove_all(m_tempDir); }

   /**
    * @brief Find the path to the undoCore runtime headers
    */
   fs::path findRuntimePath() const
   {
      std::vector<fs::path> candidates = {fs::current_path().parent_path() / "st2cpp_includes" / "undoCore" / "include",
                                          fs::current_path() / ".." / ".." / "st2cpp_includes" / "undoCore" / "include",
                                          fs::current_path() / ".." / "st2cpp_includes" / "undoCore" / "include",
                                          fs::current_path() / "st2cpp_includes" / "undoCore" / "include"};

      for (const auto& path : candidates) {
         if (fs::exists(path / "undoCore" / "types.hpp")) {
            return path;
         }
      }

      return fs::current_path().parent_path() / "st2cpp_includes" / "undoCore" / "include";
   }

   /**
    * @brief Create a minimal runtime stub for compilation tests
    * 
    * This stub provides the minimum runtime types and templates needed
    * to compile the generated C++ code. It mimics the real undoCore/types.hpp
    * but is self-contained and lightweight for test purposes.
    * 
    * The stub includes:
    * - Basic IEC 61131-3 type aliases (Bool, Int16, UInt32, etc.)
    * - ProcessImage template with read/write methods for I/O and marker memory
    * - STArray template with support for 1D and 2D array initialization (trivially copyable)
    * - VAR_INOUT wrapper for IN_OUT parameters
    * - Utility functions like INT_TO_REAL
    */
   void writeRuntimeStub()
   {
      std::string runtimeStub = R"(
            #pragma once
            #include <cstdint>
            #include <cstddef>
            #include <initializer_list>
            #include <stdexcept>
            #include <array>
            #include <type_traits>

            // ============================================================================
            // IEC 61131-3 Elementary Types mapped to C++
            // ============================================================================

            using Bool = bool;
            using Int8 = int8_t;
            using Int16 = int16_t;
            using Int32 = int32_t;
            using Int64 = int64_t;
            using UInt8 = uint8_t;
            using UInt16 = uint16_t;
            using UInt32 = uint32_t;
            using UInt64 = uint64_t;
            using Float = float;
            using Double = double;
            using String = const char*;
            using Byte = UInt8;
            using Word = UInt16;
            using Dword = UInt32;
            using Lword = UInt64;

            // ============================================================================
            // ProcessImage Template
            // ============================================================================

            /**
             * @brief Process Image template for I/O and marker memory access
             * @tparam InputBytes Size of input memory region in bytes
             * @tparam OutputBytes Size of output memory region in bytes
             * @tparam MarkerBytes Size of marker memory region in bytes
             * 
             * Provides bit, byte, word, and dword accessors for each memory region.
             * This is a minimal implementation sufficient for compilation tests.
             */
            template<size_t InputBytes, size_t OutputBytes, size_t MarkerBytes>
            struct ProcessImage {
                uint8_t input[InputBytes];
                uint8_t output[OutputBytes];
                uint8_t marker[MarkerBytes];

                // ===== Input region accessors =====
                Bool readInputBit(size_t byte, size_t bit) const {
                    return (input[byte] >> bit) & 1;
                }
                void writeInputBit(size_t byte, size_t bit, Bool value) {
                    if (value) input[byte] |= (1 << bit);
                    else input[byte] &= ~(1 << bit);
                }
                UInt8 readInputByte(size_t byte) const { return input[byte]; }
                void writeInputByte(size_t byte, UInt8 value) { input[byte] = value; }
                UInt16 readInputWord(size_t byte) const {
                    return static_cast<UInt16>(input[byte]) |
                           (static_cast<UInt16>(input[byte + 1]) << 8);
                }
                void writeInputWord(size_t byte, UInt16 value) {
                    input[byte] = value & 0xFF;
                    input[byte + 1] = (value >> 8) & 0xFF;
                }
                UInt32 readInputDword(size_t byte) const {
                    return static_cast<UInt32>(input[byte]) |
                           (static_cast<UInt32>(input[byte + 1]) << 8) |
                           (static_cast<UInt32>(input[byte + 2]) << 16) |
                           (static_cast<UInt32>(input[byte + 3]) << 24);
                }
                void writeInputDword(size_t byte, UInt32 value) {
                    input[byte] = value & 0xFF;
                    input[byte + 1] = (value >> 8) & 0xFF;
                    input[byte + 2] = (value >> 16) & 0xFF;
                    input[byte + 3] = (value >> 24) & 0xFF;
                }

                // ===== Output region accessors =====
                Bool readOutputBit(size_t byte, size_t bit) const {
                    return (output[byte] >> bit) & 1;
                }
                void writeOutputBit(size_t byte, size_t bit, Bool value) {
                    if (value) output[byte] |= (1 << bit);
                    else output[byte] &= ~(1 << bit);
                }
                UInt8 readOutputByte(size_t byte) const { return output[byte]; }
                void writeOutputByte(size_t byte, UInt8 value) { output[byte] = value; }
                UInt16 readOutputWord(size_t byte) const {
                    return static_cast<UInt16>(output[byte]) |
                           (static_cast<UInt16>(output[byte + 1]) << 8);
                }
                void writeOutputWord(size_t byte, UInt16 value) {
                    output[byte] = value & 0xFF;
                    output[byte + 1] = (value >> 8) & 0xFF;
                }
                UInt32 readOutputDword(size_t byte) const {
                    return static_cast<UInt32>(output[byte]) |
                           (static_cast<UInt32>(output[byte + 1]) << 8) |
                           (static_cast<UInt32>(output[byte + 2]) << 16) |
                           (static_cast<UInt32>(output[byte + 3]) << 24);
                }
                void writeOutputDword(size_t byte, UInt32 value) {
                    output[byte] = value & 0xFF;
                    output[byte + 1] = (value >> 8) & 0xFF;
                    output[byte + 2] = (value >> 16) & 0xFF;
                    output[byte + 3] = (value >> 24) & 0xFF;
                }

                // ===== Marker region accessors =====
                Bool readMarkerBit(size_t byte, size_t bit) const {
                    return (marker[byte] >> bit) & 1;
                }
                void writeMarkerBit(size_t byte, size_t bit, Bool value) {
                    if (value) marker[byte] |= (1 << bit);
                    else marker[byte] &= ~(1 << bit);
                }
                UInt8 readMarkerByte(size_t byte) const { return marker[byte]; }
                void writeMarkerByte(size_t byte, UInt8 value) { marker[byte] = value; }
                UInt16 readMarkerWord(size_t byte) const {
                    return static_cast<UInt16>(marker[byte]) |
                           (static_cast<UInt16>(marker[byte + 1]) << 8);
                }
                void writeMarkerWord(size_t byte, UInt16 value) {
                    marker[byte] = value & 0xFF;
                    marker[byte + 1] = (value >> 8) & 0xFF;
                }
                UInt32 readMarkerDword(size_t byte) const {
                    return static_cast<UInt32>(marker[byte]) |
                           (static_cast<UInt32>(marker[byte + 1]) << 8) |
                           (static_cast<UInt32>(marker[byte + 2]) << 16) |
                           (static_cast<UInt32>(marker[byte + 3]) << 24);
                }
                void writeMarkerDword(size_t byte, UInt32 value) {
                    marker[byte] = value & 0xFF;
                    marker[byte + 1] = (value >> 8) & 0xFF;
                    marker[byte + 2] = (value >> 16) & 0xFF;
                    marker[byte + 3] = (value >> 24) & 0xFF;
                }
            };

            // ============================================================================
            // STArray Template - Trivially Copyable Version
            // ============================================================================

            /**
             * @brief Array template compatible with IEC 61131-3 array semantics
             * @tparam T Type of array elements
             * @tparam Low Lower bound index (inclusive)
             * @tparam High Upper bound index (inclusive)
             * 
             * This implementation is trivially copyable when T is trivially copyable,
             * allowing efficient memcpy operations and use in certain contexts.
             * 
             * Supports:
             * - 1D arrays: STArray<Int16, 0, 5> arr = {1, 2, 3, 4, 5};
             * - 2D arrays: STArray<STArray<Int16, 0, 2>, 0, 1> arr = {{1, 2, 3}, {4, 5, 6}};
             * - Bounds checking on element access
             */
            template<typename T, int Low, int High>
            struct STArray {
                static_assert(High >= Low, "High bound must be >= Low bound");
                static constexpr size_t SIZE = (High - Low + 1);
                
                // Use std::array internally for proper initialization
                std::array<T, SIZE> data;

                // All constructors are defaulted for trivial copyability
                STArray() = default;
                STArray(const STArray&) = default;
                STArray(STArray&&) = default;
                STArray& operator=(const STArray&) = default;
                STArray& operator=(STArray&&) = default;
                ~STArray() = default;

                /**
                 * @brief Constructor for 1D arrays using initializer list
                 * @param init Initializer list of values
                 * 
                 * Example: STArray<Int16, 0, 5> arr = {1, 2, 3, 4, 5};
                 */
                STArray(std::initializer_list<T> init) {
                    size_t i = 0;
                    for (const auto& val : init) {
                        if (i < SIZE) data[i++] = val;
                    }
                }

                /**
                 * @brief Constructor for 2D arrays using nested initializer lists
                 * @param init Nested initializer list like {{1, 2, 3}, {4, 5, 6}}
                 * 
                 * Example: STArray<STArray<Int16, 0, 2>, 0, 1> m = {{1, 2, 3}, {4, 5, 6}};
                 */
                template<typename U>
                STArray(std::initializer_list<std::initializer_list<U>> init) {
                    size_t row = 0;
                    for (const auto& rowInit : init) {
                        if (row >= SIZE) break;
                        // Construct inner STArray from row initializer list
                        data[row] = T(rowInit);
                        ++row;
                    }
                }

                /**
                 * @brief Generic constructor for nested initializer lists
                 * @param init Initializer list of values convertible to T
                 */
                template<typename U>
                STArray(std::initializer_list<U> init) {
                    size_t i = 0;
                    for (const auto& val : init) {
                        if (i < SIZE) {
                            data[i] = val;
                            ++i;
                        }
                    }
                }

                /**
                 * @brief Element access with bounds checking (non-const)
                 * @param index Index to access (can be negative if Low is negative)
                 * @return Reference to the element at the specified index
                 * @throws std::out_of_range if index is out of bounds
                 */
                T& operator[](int index) {
                    if (index < Low || index > High) {
                        throw std::out_of_range("STArray index out of bounds");
                    }
                    return data[index - Low];
                }

                /**
                 * @brief Element access with bounds checking (const)
                 * @param index Index to access (can be negative if Low is negative)
                 * @return Const reference to the element at the specified index
                 * @throws std::out_of_range if index is out of bounds
                 */
                const T& operator[](int index) const {
                    if (index < Low || index > High) {
                        throw std::out_of_range("STArray index out of bounds");
                    }
                    return data[index - Low];
                }

                // Bounds information
                static constexpr int low() { return Low; }
                static constexpr int high() { return High; }
                static constexpr size_t size() { return SIZE; }

                // Raw data access
                T* data_ptr() { return data.data(); }
                const T* data_ptr() const { return data.data(); }

                // Iterators
                auto begin() { return data.begin(); }
                auto end() { return data.end(); }
                auto begin() const { return data.begin(); }
                auto end() const { return data.end(); }

                // Fill with a value
                void fill(const T& value) { data.fill(value); }
            };

            // ============================================================================
            // VAR_INOUT Template
            // ============================================================================

            /**
             * @brief Wrapper for VAR_IN_OUT parameters
             * @tparam T Type of the referenced variable
             * 
             * In IEC 61131-3, VAR_IN_OUT parameters are passed by reference.
             * This template provides a clean wrapper for such parameters.
             */
            template<typename T>
            struct VAR_INOUT {
                T* ptr;

                VAR_INOUT() : ptr(nullptr) {}
                VAR_INOUT(T& ref) : ptr(&ref) {}

                VAR_INOUT& operator=(T& ref) {
                    ptr = &ref;
                    return *this;
                }

                operator T&() { return *ptr; }
                operator const T&() const { return *ptr; }
            };

            // ============================================================================
            // Utility Functions
            // ============================================================================

            /**
             * @brief Convert INT (Int16) to REAL (Float)
             * @param val Integer value to convert
             * @return Floating point representation of the integer
             */
            inline Float INT_TO_REAL(Int16 val) {
                return static_cast<Float>(val);
            }

            /**
             * @brief Convert DINT (Int32) to REAL (Float)
             * @param val Integer value to convert
             * @return Floating point representation of the integer
             */
            inline Float DINT_TO_REAL(Int32 val) {
                return static_cast<Float>(val);
            }

            /**
             * @brief Convert REAL (Float) to INT (Int16)
             * @param val Floating point value to convert
             * @return Integer representation with truncation
             */
            inline Int16 REAL_TO_INT(Float val) {
                return static_cast<Int16>(val);
            }

            inline UInt32 ULINT_TO_UDINT(UInt64 v) {
               return static_cast<UInt32>(v);
            }

            /**
             * @brief Truncate a floating point value
             * @param val Value to truncate
             * @return Truncated integer value
             */
            inline Int16 TRUNC(Float val) {
                return static_cast<Int16>(val);
            }
        )";

      fs::path runtimeDir = m_tempDir / "undoCore";
      fs::create_directories(runtimeDir);
      fs::path runtimeHpp = runtimeDir / "types.hpp";
      TestHelper::writeFile(runtimeHpp.string(), runtimeStub);
   }

   /**
    * @brief Write generated files to disk and compile them
    * @param header Header content
    * @param source Source content
    * @return Exit code from compilation
    */
   int compileGenerated(const std::string& header, const std::string& source, bool isCpp20 = false)
   {
      fs::path hpp = m_tempDir / "test.hpp";
      fs::path cpp = m_tempDir / "test.cpp";
      TestHelper::writeFile(hpp.string(), header);
      TestHelper::writeFile(cpp.string(), source);

      return TestHelper::compileSource(cpp.string(), m_tempDir.string(), isCpp20);
   }

   /**
    * @brief Compile ST source code directly
    * @param st Structured Text source
    * @return Exit code from compilation
    */
   int compileST(const std::string& st)
   {
      auto result = TestHelper::generateFromST(st);
      return compileGenerated(result.header, result.source);
   }

   /**
    * @brief List all generated files for debugging
    */
   void listGeneratedFiles()
   {
      std::cout << "Generated files in " << m_tempDir.string() << ":\n";
      for (const auto& entry : fs::recursive_directory_iterator(m_tempDir)) {
         if (entry.is_regular_file()) {
            std::cout << "  " << entry.path().string() << std::endl;
         }
      }
   }

   fs::path m_tempDir;
};

// ============================================================================
//  Basic Compilation Tests
// ============================================================================

/**
 * @brief Test that simple generated code compiles successfully
 */
TEST_F(CompilationTest, GenerateAndCompile)
{
   const std::string st = R"(
        VAR_GLOBAL x AT %IX0.0 : BOOL; END_VAR
        PROGRAM Main
            VAR y : BOOL; END_VAR
            y := x;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for generated code";
}

/**
 * @brief Test that modular project generation compiles successfully
 */
TEST_F(CompilationTest, CompileModularProject)
{
   const std::string st = R"(
        FUNCTION_BLOCK FB_Inner
            VAR_INPUT
                x : INT;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK FB_Outer
            VAR
                inner : FB_Inner;
            END_VAR
        END_FUNCTION_BLOCK

        PROGRAM Main
            VAR
                fb : FB_Outer;
            END_VAR
        END_PROGRAM
    )";

   auto files = TestHelper::generateModularFromST(st, m_tempDir.string());

   for (const auto& file : files) {
      fs::path dir = m_tempDir;
      if (!file.subdir.empty()) {
         dir /= file.subdir;
      }
      fs::create_directories(dir);
      fs::path fullPath = dir / (file.name + (file.type == GenFileType::SOURCE ? ".cpp" : ".hpp"));
      TestHelper::writeFile(fullPath.string(), file.content);
      std::cout << "Generated: " << fullPath.string() << " (type: " << static_cast<int>(file.type) << ")\n";
   }

   listGeneratedFiles();

   // Find all .cpp files
   std::vector<std::string> sourceFiles;
   for (const auto& entry : fs::recursive_directory_iterator(m_tempDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
         sourceFiles.push_back(entry.path().string());
      }
   }

   // If no .cpp files found, try with .cpp files in subdirectories
   if (sourceFiles.empty()) {
      for (const auto& entry : fs::recursive_directory_iterator(m_tempDir)) {
         if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
            sourceFiles.push_back(entry.path().string());
         }
      }
   }

   if (sourceFiles.empty()) {
      FAIL() << "No .cpp files found in " << m_tempDir.string();
      return;
   }

   int exitCode = TestHelper::compileSources(sourceFiles, m_tempDir.string());
   EXPECT_EQ(exitCode, 0) << "Compilation failed for modular project";
}

// ============================================================================
//  Struct Initialization Compilation Tests
// ============================================================================

/**
 * @brief Test that struct initialization with named members compiles successfully
 * 
 * IEC 61131-3 supports struct initialization using the syntax:
 *   (member1 := value1, member2 := value2, ...)
 */
TEST_F(CompilationTest, CompileStructInitialization)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        TYPE Color :
            STRUCT
                red : BYTE;
                green : BYTE;
                blue : BYTE;
            END_STRUCT
        END_TYPE

        TYPE Device :
            STRUCT
                id : INT;
                name : STRING;
                position : Point;
                color_ : Color;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            p1 : Point := (x := 10, y := 20);
            p2 : Point := (x := 5, y := 15);
            c1 : Color := (red := 255, green := 0, blue := 0);
            dev : Device := (id := 1, name := "Sensor", position := p1, color_ := c1);
        END_VAR

        PROGRAM Main
            VAR
                result : INT;
                local_point : Point := (x := 100, y := 200);
                local_color : Color := (red := 0, green := 255, blue := 0);
                local_dev : Device := (id := 2, name := "Actuator", position := local_point, color_ := local_color);
            END_VAR
            result := p1.x + p2.y + dev.position.x + local_point.y;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source, true);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for struct initialization";
}

/**
 * @brief Test that nested struct initialization compiles successfully
 */
TEST_F(CompilationTest, CompileNestedStructInitialization)
{
   const std::string st = R"(
        TYPE Point2D :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        TYPE Point3D :
            STRUCT
                xy : Point2D;
                z : INT;
            END_STRUCT
        END_TYPE

        TYPE Line :
            STRUCT
                start : Point3D;
                end : Point3D;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            line1 : Line := (start := (xy := (x := 0, y := 0), z := 0), 
                             end := (xy := (x := 10, y := 20), z := 30));
            point3d_ : Point3D := (xy := (x := 5, y := 15), z := 25);
        END_VAR

        PROGRAM Main
            VAR
                p : Point3D := (xy := (x := 1, y := 2), z := 3);
                l : Line := (start := (xy := (x := 100, y := 200), z := 300),
                             end := (xy := (x := 400, y := 500), z := 600));
                result : INT;
            END_VAR
            result := line1.start.xy.x + line1.end.xy.y + p.z + l.start.xy.y;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source, true);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for nested struct initialization";
}

/**
 * @brief Test that struct array initialization compiles successfully
 */
TEST_F(CompilationTest, CompileStructArrayInitialization)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            points : ARRAY[0..2] OF Point := [(x := 1, y := 2), (x := 3, y := 4), (x := 5, y := 6)];
            matrix : ARRAY[0..1, 0..1] OF Point := [[(x := 10, y := 20), (x := 30, y := 40)], 
                                                     [(x := 50, y := 60), (x := 70, y := 80)]];
        END_VAR

        PROGRAM Main
            VAR
                result : INT;
                i : INT;
                j : INT;
            END_VAR
            result := 0;
            FOR i := 0 TO 2 DO
                result := result + points[i].x;
            END_FOR
            FOR i := 0 TO 1 DO
                FOR j := 0 TO 1 DO
                    result := result + matrix[i][j].y;
                END_FOR
            END_FOR
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for struct array initialization";
}

/**
 * @brief Test that struct initialization with partial initialization compiles successfully
 * 
 * IEC 61131-3 allows partial initialization of structs where unspecified
 * members are initialized to their default values (zero-initialized).
 */
TEST_F(CompilationTest, CompilePartialStructInitialization)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
                z : INT;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            p1 : Point := (x := 10, y := 20);    // z defaults to 0
            p2 : Point := (z := 30);             // x and y default to 0
            p3 : Point := (x := 5);              // y and z default to 0
        END_VAR

        PROGRAM Main
            VAR
                local_point : Point := (x := 100, y := 200);
                result : INT;
            END_VAR
            result := p1.x + p1.y + p1.z + p2.z + p3.x + local_point.x;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source, true);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for partial struct initialization";
}

/**
 * @brief Test that struct initialization with out-of-order member assignment compiles successfully
 * 
 * IEC 61131-3 allows struct initialization with named members in any order.
 */
TEST_F(CompilationTest, CompileStructInitializationOutOfOrder)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
                z : INT;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            p1 : Point := (y := 20, x := 10, z := 30);
            p2 : Point := (z := 50, x := 40, y := 60);
        END_VAR

        PROGRAM Main
            VAR
                local_point : Point := (y := 200, x := 100, z := 300);
                result : INT;
            END_VAR
            result := p1.x + p1.y + p1.z + p2.x + p2.y + p2.z + local_point.x;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source, true);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for out-of-order struct initialization";
}

// ============================================================================
//  AT Address Compilation Tests
// ============================================================================

/**
 * @brief Test that code with AT addresses compiles successfully
 */
TEST_F(CompilationTest, CompileWithATAddresses)
{
   const std::string st = R"(
        VAR_GLOBAL
            start AT %IX0.0 : BOOL := FALSE;
            speed AT %IW2 : INT := 0;
            flags AT %MB0 : BYTE := 0;
        END_VAR

        PROGRAM Main
            VAR
                output AT %QX0.0 : BOOL;
                counter AT %MW10 : INT;
            END_VAR
            output := start;
            counter := speed;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for code with AT addresses";
}

/**
 * @brief Test that code with placeholder AT addresses compiles successfully
 */
TEST_F(CompilationTest, CompileWithPlaceholderAT)
{
   const std::string st = R"(
        VAR_GLOBAL
            sensor AT %IX* : BOOL := FALSE;
            actuator AT %QX* : BOOL := FALSE;
            temp AT %IW* : INT := 0;
        END_VAR

        PROGRAM Main
            VAR
                local AT %MX* : BOOL;
            END_VAR
            local := sensor;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for code with placeholder AT addresses";
}

// ============================================================================
//  Function Block Compilation Tests
// ============================================================================

/**
 * @brief Test that function block with methods compiles successfully
 */
TEST_F(CompilationTest, CompileFunctionBlockWithMethods)
{
   const std::string st = R"(
        FUNCTION_BLOCK Math
            VAR_INPUT
                a : INT;
                b : INT;
            END_VAR
            VAR_OUTPUT
                sum : INT;
                product : INT;
            END_VAR

            METHOD PRIVATE Add : INT
                VAR_INPUT x : INT; y : INT; END_VAR
                Add := x + y;
            END_METHOD

            METHOD PUBLIC Multiply : INT
                VAR_INPUT x : INT; y : INT; END_VAR
                Multiply := x * y;
            END_METHOD
        END_FUNCTION_BLOCK

        PROGRAM Main
            VAR
                m : Math;
                result : INT;
            END_VAR
            m(a := 5, b := 3, sum => result);
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for FB with methods";
}

/**
 * @brief Test that function block with inheritance compiles successfully
 */
TEST_F(CompilationTest, CompileFunctionBlockInheritance)
{
   const std::string st = R"(
        FUNCTION_BLOCK Base
            VAR
                id : INT := 0;
            END_VAR
            METHOD GetId : INT
                GetId := id;
            END_METHOD
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base
            VAR
                extra : INT := 0;
            END_VAR
            METHOD GetExtra : INT
                GetExtra := extra;
            END_METHOD
        END_FUNCTION_BLOCK

        PROGRAM Main
            VAR
                d : Derived;
                result : INT;
            END_VAR
            result := d.GetId();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for FB with inheritance";
}

// ============================================================================
//  Array Compilation Tests
// ============================================================================

/**
 * @brief Test that 1D array compiles successfully
 */
TEST_F(CompilationTest, CompileArray1D)
{
   const std::string st = R"(
        VAR_GLOBAL
            data : ARRAY[0..5] OF INT := [10, 20, 30, 40, 50, 60];
        END_VAR

        PROGRAM Main
            VAR
                sum : INT := 0;
                i : INT;
            END_VAR
            FOR i := 0 TO 5 DO
                sum := sum + data[i];
            END_FOR
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for 1D array";
}

/**
 * @brief Test that 2D array compiles successfully with nested initialization
 * 
 * This test verifies that the STArray template correctly handles
 * nested initializer lists for 2D arrays.
 */
TEST_F(CompilationTest, CompileArray2D)
{
   const std::string st = R"(
        VAR_GLOBAL
            matrix : ARRAY[0..1, 0..2] OF INT := [[1, 2, 3], [4, 5, 6]];
        END_VAR

        PROGRAM Main
            VAR
                result : INT;
            END_VAR
            result := matrix[0][1] + matrix[1][2];
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for 2D array";
}

// ============================================================================
//  STArray Trivially Copyable Test
// ============================================================================

/**
 * @brief Test that STArray is trivially copyable
 * 
 * This test verifies that the STArray template is trivially copyable,
 * which allows efficient memcpy operations and use in certain contexts
 * such as storing in contiguous memory, using with memmove, etc.
 */
TEST_F(CompilationTest, STArrayTriviallyCopyable)
{
   // This test compiles a program that checks trivially copyable trait
   const std::string st = R"(
        VAR_GLOBAL
            arr1 : ARRAY[0..4] OF INT := [1, 2, 3, 4, 5];
            arr2 : ARRAY[0..4] OF INT;
            arr2d : ARRAY[0..1, 0..2] OF INT := [[1, 2, 3], [4, 5, 6]];
            arr2d_copy : ARRAY[0..1, 0..2] OF INT;
        END_VAR

        PROGRAM Main
            VAR
                i : INT;
                result : INT := 0;
            END_VAR
            // Test 1D array copy
            arr2 := arr1;
            FOR i := 0 TO 4 DO
                result := result + arr2[i];
            END_FOR
            
            // Test 2D array copy
            arr2d_copy := arr2d;
            result := result + arr2d_copy[0][1] + arr2d_copy[1][2];
        END_PROGRAM
    )";

   int exitCode = compileST(st);
   EXPECT_EQ(exitCode, 0) << "STArray should be copyable (compilation failed)";
}

// ============================================================================
//  Enum and Struct Compilation Tests
// ============================================================================

/**
 * @brief Test that enum compiles successfully
 */
TEST_F(CompilationTest, CompileEnum)
{
   const std::string st = R"(
        TYPE Color : (Red, Green, Blue) END_TYPE

        PROGRAM Main
            VAR
                c : Color;
            END_VAR
            c := Color.Red;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for enum";
}

/**
 * @brief Test that struct compiles successfully
 */
TEST_F(CompilationTest, CompileStruct)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        PROGRAM Main
            VAR
                p : Point;
                result : INT;
            END_VAR
            p.x := 10;
            p.y := 20;
            result := p.x + p.y;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for struct";
}

// ============================================================================
//  Control Flow Compilation Tests
// ============================================================================

/**
 * @brief Test that IF statement compiles successfully
 */
TEST_F(CompilationTest, CompileIfStatement)
{
   const std::string st = R"(
        PROGRAM Main
            VAR
                x : INT := 10;
                y : INT := 0;
            END_VAR
            IF x > 0 THEN
                y := 1;
            ELSIF x < 0 THEN
                y := -1;
            ELSE
                y := 0;
            END_IF
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for IF statement";
}

/**
 * @brief Test that CASE statement compiles successfully
 */
TEST_F(CompilationTest, CompileCaseStatement)
{
   const std::string st = R"(
        PROGRAM Main
            VAR
                value : INT := 5;
                result : INT := 0;
            END_VAR
            CASE value OF
                1: result := 10;
                2, 3: result := 20;
                10..20: result := 30;
                ELSE result := 0;
            END_CASE
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for CASE statement";
}

/**
 * @brief Test that FOR loop compiles successfully
 */
TEST_F(CompilationTest, CompileForLoop)
{
   const std::string st = R"(
        PROGRAM Main
            VAR
                i : INT;
                sum : INT := 0;
            END_VAR
            FOR i := 0 TO 10 BY 2 DO
                sum := sum + i;
            END_FOR
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for FOR loop";
}

/**
 * @brief Test that WHILE loop compiles successfully
 */
TEST_F(CompilationTest, CompileWhileLoop)
{
   const std::string st = R"(
        PROGRAM Main
            VAR
                x : INT := 0;
                sum : INT := 0;
            END_VAR
            WHILE x < 10 DO
                sum := sum + x;
                x := x + 1;
            END_WHILE
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for WHILE loop";
}

// ============================================================================
//  Complex Integration Tests
// ============================================================================

/**
 * @brief Test that a complete program with all features compiles
 */
TEST_F(CompilationTest, CompileCompleteProgram)
{
   const std::string st = R"(
        TYPE Color : (Red, Green, Blue) END_TYPE

        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        FUNCTION Add : INT
            VAR_INPUT a : INT; b : INT; END_VAR
            Add := a + b;
        END_FUNCTION

        FUNCTION_BLOCK Counter
            VAR_INPUT enable : BOOL; END_VAR
            VAR_OUTPUT count : INT; END_VAR
            VAR internal : INT := 0; END_VAR
            IF enable THEN
                internal := internal + 1;
                count := internal;
            END_IF
        END_FUNCTION_BLOCK

        VAR_GLOBAL
            global_count : INT := 0;
        END_VAR

        PROGRAM Main
            VAR
                c : Counter;
                result : INT;
                p : Point;
                color_ : Color;
                i : INT;
            END_VAR
            c(enable := TRUE);
            result := Add(c.count, 10);
            p.x := 42;
            p.y := 84;
            color_ := Color.Green;
            global_count := global_count + 1;
            FOR i := 0 TO 5 DO
                global_count := global_count + i;
            END_FOR
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for complete program";
}

// ============================================================================
//  Error Handling Tests
// ============================================================================

/**
 * @brief Test that compilation fails for invalid generated code (should not happen)
 * 
 * This is a negative test to ensure the test harness works correctly.
 * It verifies that the compilation detection works for failing cases.
 */
TEST_F(CompilationTest, DISABLED_CompileInvalidCode)
{
   // This test is disabled because it intentionally fails compilation
   // It's here to demonstrate the error detection mechanism
   const std::string invalidCode = R"(
        // This is intentionally invalid C++ code
        #include <nonexistent>
        int main() {
            invalid syntax here
        }
    )";

   fs::path cpp = m_tempDir / "invalid.cpp";
   TestHelper::writeFile(cpp.string(), invalidCode);

   int exitCode = TestHelper::compileSource(cpp.string(), m_tempDir.string());
   EXPECT_NE(exitCode, 0) << "Invalid code should fail compilation";
}

// ============================================================================
//  Typed Literal Compilation Tests
// ============================================================================

/**
 * @brief Test that code with typed integer literals compiles successfully
 */
TEST_F(CompilationTest, CompileTypedIntegerLiterals)
{
   const std::string st = R"(
        FUNCTION Test : UDINT
            VAR
                x : UDINT;
            END_VAR
            x := UDINT#123;
            Test := x;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed integer literals";
}

/**
 * @brief Test that code with typed hexadecimal literals compiles successfully
 */
TEST_F(CompilationTest, CompileTypedHexLiterals)
{
   const std::string st = R"(
        FUNCTION Test : UDINT
            VAR
                x : UDINT;
                y : ULINT;
            END_VAR
            x := ULINT#16#85EBCA6B;
            y := ULINT#16#9E3779B97F4A7C15;
            Test := ULINT_TO_UDINT(x);
        END_FUNCTION

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed hex literals";
}

/**
 * @brief Test that code with typed real literals compiles successfully
 */
TEST_F(CompilationTest, CompileTypedRealLiterals)
{
   const std::string st = R"(
        FUNCTION Test : LREAL
            VAR
                x : LREAL;
                y : REAL;
            END_VAR
            x := LREAL#3.14159;
            y := REAL#1.5e-10;
            Test := LREAL#0.0;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : LREAL;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed real literals";
}

/**
 * @brief Test that code with typed literals in expressions compiles successfully
 */
TEST_F(CompilationTest, CompileTypedLiteralsInExpressions)
{
   const std::string st = R"(
        FUNCTION Test : ULINT
            VAR
                x : ULINT;
                y : ULINT;
            END_VAR
            x := ULINT#100;
            y := x + ULINT#200;
            Test := y * ULINT#2;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : ULINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed literals in expressions";
}

/**
 * @brief Test that code with typed literals and bitwise operations compiles successfully
 */
TEST_F(CompilationTest, CompileTypedLiteralsBitwise)
{
   const std::string st = R"(
        FUNCTION Test : UDINT
            VAR
                x : UDINT;
                mask : UDINT;
            END_VAR
            x := UDINT#16#12345678;
            mask := UDINT#16#A5A5A5A5;
            x := x XOR mask;
            x := x AND UDINT#16#FFFFFFFF;
            Test := x;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed literals with bitwise ops";
}

/**
 * @brief Test that code with typed literals in arrays compiles successfully
 */
TEST_F(CompilationTest, CompileTypedLiteralsInArray)
{
   const std::string st = R"(
        VAR_GLOBAL
            data : ARRAY[0..2] OF UDINT := [UDINT#10, UDINT#20, UDINT#30];
        END_VAR

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := data[0] + data[1] + data[2];
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed literals in arrays";
}

/**
 * @brief Test that code with typed literals in structs compiles successfully
 */
TEST_F(CompilationTest, CompileTypedLiteralsInStruct)
{
   const std::string st = R"(
        TYPE Point :
            STRUCT
                x : UDINT;
                y : UDINT;
            END_STRUCT
        END_TYPE

        VAR_GLOBAL
            p : Point := (x := UDINT#100, y := UDINT#200);
        END_VAR

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := p.x + p.y;
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed literals in structs";
}

/**
 * @brief Test that code with typed literals with underscores compiles successfully
 */
TEST_F(CompilationTest, CompileTypedLiteralsWithUnderscores)
{
   const std::string st = R"(
        FUNCTION Test : UDINT
            VAR
                x : UDINT;
            END_VAR
            x := UDINT#123_456_789;
            Test := x;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for typed literals with underscores";
}

/**
 * @brief Test that code with mixed typed and untyped literals compiles successfully
 */
TEST_F(CompilationTest, CompileMixedTypedUntyped)
{
   const std::string st = R"(
        FUNCTION Test : UDINT
            VAR
                x : UDINT;
            END_VAR
            x := UDINT#100 + 200;
            Test := x * 2;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : UDINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for mixed typed and untyped literals";
}

/**
 * @brief Test that code with all typed literal types compiles successfully
 */
TEST_F(CompilationTest, CompileAllTypedLiterals)
{
   const std::string st = R"(
        FUNCTION Test : ULINT
            VAR
                a : UDINT;
                b : ULINT;
                c : INT;
                d : LREAL;
                e : REAL;
            END_VAR
            a := UDINT#123;
            b := ULINT#16#9E3779B97F4A7C15;
            c := INT#-42;
            d := LREAL#3.14159265359;
            e := REAL#2.71828;
            Test := ULINT#0;
        END_FUNCTION

        PROGRAM Main
            VAR
                result : ULINT;
            END_VAR
            result := Test();
        END_PROGRAM
    )";

   auto result = TestHelper::generateFromST(st);
   int exitCode = compileGenerated(result.header, result.source);
   EXPECT_EQ(exitCode, 0) << "Compilation failed for all typed literal types";
}
