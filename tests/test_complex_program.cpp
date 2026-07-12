/**
 * @file test_complex_program.cpp
 * @brief Implementation of test cases for complex ST programs
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include <regex>
#include <fstream>

// ============================================================================
//  Test Fixture
// ============================================================================

class ComplexProgramTest : public ::testing::Test
{
protected:
   GeneratedCode result;

   void SetUp() override
   {
      // Each test will load its own file
   }

   /**
    * @brief Generate code from a file in the st_samples directory
    * @param filename Name of the .st file
    */
   void generateFromFile(const std::string& filename)
   {
      std::string content;

      // Use macro defined by CMake
#ifdef ST_SAMPLES_DIR
      std::string stPath = std::string(ST_SAMPLES_DIR) + "/" + filename;
      std::ifstream f(stPath);
      if (f.good()) {
         std::stringstream ss;
         ss << f.rdbuf();
         content = ss.str();
      }
#endif

      // Fallback search paths
      if (content.empty()) {
         std::vector<std::string> searchPaths = {
            "st_samples/" + filename,
            "../st_samples/" + filename,
            "../../st_samples/" + filename,
            "tests/st_samples/" + filename,
            "../tests/st_samples/" + filename,
            "../../tests/st_samples/" + filename,
         };

         for (const auto& path : searchPaths) {
            std::ifstream f(path);
            if (f.good()) {
               std::stringstream ss;
               ss << f.rdbuf();
               content = ss.str();
               break;
            }
         }
      }

      if (content.empty()) {
         FAIL() << "Cannot open file: " << filename;
         return;
      }

      result = TestHelper::generateFromST(content);
   }

   /**
    * @brief Check that a pattern exists in the header
    */
   void expectContains(const std::string& text, const std::string& msg = "")
   {
      EXPECT_NE(result.header.find(text), std::string::npos) << msg << "\nFull header:\n" << result.header;
   }

   /**
    * @brief Check that a pattern exists in the source
    */
   void expectContainsSource(const std::string& text, const std::string& msg = "")
   {
      EXPECT_NE(result.source.find(text), std::string::npos) << msg << "\nFull source:\n" << result.source;
   }

   /**
    * @brief Check that a pattern does NOT exist in the header
    */
   void expectNotContains(const std::string& text, const std::string& msg = "")
   {
      EXPECT_EQ(result.header.find(text), std::string::npos) << msg << "\nFull header:\n" << result.header;
   }

   /**
    * @brief Check that a pattern exists in the header using regex
    */
   void expectRegex(const std::string& pattern, const std::string& msg = "")
   {
      try {
         std::regex re(pattern);
         EXPECT_TRUE(std::regex_search(result.header, re)) << msg << "\nFull header:\n" << result.header;
      } catch (const std::regex_error& e) {
         EXPECT_TRUE(result.header.find(pattern) != std::string::npos) << msg << "\nFull header:\n" << result.header;
      }
   }

   /**
    * @brief Check that a pattern exists in the source using regex
    */
   void expectRegexSource(const std::string& pattern, const std::string& msg = "")
   {
      try {
         std::regex re(pattern);
         EXPECT_TRUE(std::regex_search(result.source, re)) << msg << "\nFull source:\n" << result.source;
      } catch (const std::regex_error& e) {
         EXPECT_TRUE(result.source.find(pattern) != std::string::npos) << msg << "\nFull source:\n" << result.source;
      }
   }
};

// ============================================================================
//  TEST for test_array.st
// ============================================================================

/**
 * @brief Test that ENUM types are correctly generated
 */
TEST_F(ComplexProgramTest, Enums)
{
   generateFromFile("test_array.st");

   // Enum COLOR - UPPERCASE
   expectContains("enum class COLOR");
   expectContains("RED");
   expectContains("GREEN = 5");
   expectContains("BLUE");
   expectContains("YELLOW = 10");

   // Enum STATUS - UPPERCASE
   expectContains("enum class STATUS");
   expectContains("IDLE = 0");
   expectContains("RUNNING = 1");
   expectContains("STOPPED = 2");
   expectContains("ERROR = 3");
}

/**
 * @brief Test that STRUCT types are correctly generated
 */
TEST_F(ComplexProgramTest, Structs)
{
   generateFromFile("test_array.st");

   expectContains("struct POINT3D");
   expectContains("Int16 X{};");
   expectContains("Int16 Y{};");
   expectContains("Int16 Z{};");

   expectContains("struct MATRIX3X3");
   expectContains("STArray<STArray<Int16, 1, 3>, 1, 3> DATA{};");

   expectContains("struct MULTIARRAYSTRUCT");
   expectContains("STArray<Int16, 0, 4> ARR1D{};");
   expectContains("STArray<STArray<Int16, 0, 2>, 1, 3> ARR2D{};");
   expectContains("STArray<STArray<STArray<Float, 0, 2>, 2, 3>, 0, 1> ARR3D{};");
}

/**
 * @brief Test that array types are correctly generated
 */
TEST_F(ComplexProgramTest, Arrays)
{
   generateFromFile("test_array.st");

   // Check for array type declarations (spaces may vary)
   EXPECT_TRUE(result.header.find("STArray<Int16, 0, 5> SIMPLE_ARRAY") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<COLOR, 1, 3> COLOR_ARRAY") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<Float, -2, 2> EMPTY_ARRAY") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<STArray<Int16, 0, 2>, 0, 1> MATRIX_2X3") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<STArray<Int16, 1, 3>, 1, 3> IDENTITY_3X3") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<STArray<STArray<Int16, 0, 1>, 0, 1>, 0, 1> CUBE_2X2X2") != std::string::npos);
   EXPECT_TRUE(result.header.find("STArray<STArray<STArray<Float, 0, 1>, 2, 3>, 1, 2> TENSOR") != std::string::npos);
}

/**
 * @brief Test that array initializers are correctly generated
 */
TEST_F(ComplexProgramTest, ArrayInitialization)
{
   generateFromFile("test_array.st");

   // Array initializers are in the HEADER (inline global variables)
   EXPECT_TRUE(result.header.find("SIMPLE_ARRAY = {10, 20, 30, 40, 50, 60}") != std::string::npos)
      << "SIMPLE_ARRAY initialization not found in header";
   EXPECT_TRUE(result.header.find("MATRIX_2X3 = {{1, 2, 3}, {4, 5, 6}}") != std::string::npos)
      << "MATRIX_2X3 initialization not found in header";
   EXPECT_TRUE(result.header.find("IDENTITY_3X3 = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}") != std::string::npos)
      << "IDENTITY_3X3 initialization not found in header";
   EXPECT_TRUE(result.header.find("TENSOR = {{{1.1, 2.2}, {3.3, 4.4}}, {{5.5, 6.6}, {7.7, 8.8}}}") != std::string::npos)
      << "TENSOR initialization not found in header";
}

/**
 * @brief Test that array access is correctly generated
 */
TEST_F(ComplexProgramTest, ArrayAccess)
{
   generateFromFile("test_array.st");

   // Array accesses are in the SOURCE
   EXPECT_TRUE(result.source.find("SIMPLE_ARRAY[0]") != std::string::npos) << "SIMPLE_ARRAY[0] not found in source";
   EXPECT_TRUE(result.source.find("MATRIX_2X3[0][1]") != std::string::npos) << "MATRIX_2X3[0][1] not found in source";
   EXPECT_TRUE(result.source.find("CUBE[0][0][0]") != std::string::npos)
      << "CUBE[0][0][0] not found in source (note: local variable is CUBE, not CUBE_2X2X2)";
   EXPECT_TRUE(result.source.find("SIMPLE_ARRAY[2] = 100") != std::string::npos) << "SIMPLE_ARRAY[2] = 100 not found in source";
   EXPECT_TRUE(result.source.find("MATRIX_2X3[0][1] = 99") != std::string::npos) << "MATRIX_2X3[0][1] = 99 not found in source";
}

/**
 * @brief Test that enum arrays are correctly generated
 */
TEST_F(ComplexProgramTest, EnumArray)
{
   generateFromFile("test_array.st");

   // Enum array initializer is in the HEADER
   EXPECT_TRUE(result.header.find("COLOR_ARRAY = {COLOR::RED, COLOR::GREEN, COLOR::BLUE}") != std::string::npos)
      << "COLOR_ARRAY initialization not found in header";

   // Enum array usage is in the SOURCE
   expectContainsSource("COLORVAR = COLOR_ARRAY[2]");
   expectContainsSource("COLOR::GREEN");
}

/**
 * @brief Test that function blocks with methods are correctly generated
 */
TEST_F(ComplexProgramTest, FunctionBlockWithMethods)
{
   generateFromFile("test_array.st");

   expectContains("struct MATHPROCESSOR");
   expectContains("// VAR_INPUT");
   expectContains("Int16 A{};");
   expectContains("Int16 B{};");
   expectContains("// VAR_OUTPUT");
   expectContains("Int16 SUM{};");
   expectContains("Int16 PRODUCT{};");
   expectContains("// VAR");
   expectContains("Int16 TEMP{};");
   expectContains("// VAR_TEMP (local)");
   expectContains("Int16 LOCAL_CALC{};");

   expectContains("virtual Int16 ADD(Int16 X, Int16 Y);");
   expectContains("virtual Int16 MULTIPLY(Int16 X, Int16 Y);");
}

/**
 * @brief Test that arrays of structs and FBs are correctly generated
 */
TEST_F(ComplexProgramTest, ArrayOfStructsAndFB)
{
   generateFromFile("test_array.st");

   expectContains("STArray<POINT3D, 0, 2> POINTS{};");
   expectContains("STArray<MATHPROCESSOR, 1, 2> PROCESSORS{};");
   expectContainsSource("POINTS[0].X = 10");
   expectContainsSource("PROCESSORS[1]");
}

// ============================================================================
//  TEST for test_inheritance.st
// ============================================================================

/**
 * @brief Test that interfaces are correctly generated
 */
TEST_F(ComplexProgramTest, Interfaces)
{
   generateFromFile("test_inheritance.st");

   // UPPERCASE: I_DEVICE, I_COMMUNICATE
   expectContains("struct I_DEVICE");
   expectContains("virtual Bool INIT(String NAME) = 0");
   expectContains("virtual String GETNAME() = 0");
   expectContains("virtual Bool PROCESS() = 0");
   expectContains("virtual Int16 GETSTATUS() = 0");

   expectContains("struct I_COMMUNICATE");
   expectContains("virtual Bool SEND");
   expectContains("virtual Bool RECEIVE");
}

/**
 * @brief Test that abstract classes are correctly generated
 */
TEST_F(ComplexProgramTest, AbstractClass)
{
   generateFromFile("test_inheritance.st");

   // FB_DEVICE is abstract (PROCESS() = 0)
   expectContains("struct FB_DEVICE : public I_DEVICE");
   expectContains("virtual Bool PROCESS() = 0");
   expectContains("virtual Bool INIT(String NAME)");
   expectContains("virtual String GETNAME()");
   expectContains("virtual Int16 GETSTATUS()");
   expectContains("virtual void LOGERROR(Int16 ERRORCODE)");
   expectContains("virtual Int16 GETERRORCOUNT()");
}

/**
 * @brief Test that inheritance is correctly generated
 */
TEST_F(ComplexProgramTest, Inheritance)
{
   generateFromFile("test_inheritance.st");

   // FB_SENSOR extends FB_DEVICE
   expectContains("struct FB_SENSOR : public FB_DEVICE");
   expectContains("virtual Bool PROCESS() override");
   expectContains("virtual void SETTHRESHOLD(Float THRESHOLD)");
   expectContains("virtual Float READVALUE()");
   expectContains("virtual Bool SAMPLE(Float NEWVALUE)");
   expectContains("virtual Int16 GETSAMPLECOUNT()");
}

/**
 * @brief Test that SUPER^ calls are correctly generated
 */
TEST_F(ComplexProgramTest, SuperCall)
{
   generateFromFile("test_inheritance.st");

   // SUPER^ calls are in the SOURCE
   expectContainsSource("FB_DEVICE::LOGERROR(100)");
   expectContainsSource("FB_SENSOR::PROCESS()");
   expectContainsSource("FB_SENSOR::SAMPLE(CONVERTED)");
}

/**
 * @brief Test that final classes are correctly generated
 */
TEST_F(ComplexProgramTest, FinalClass)
{
   generateFromFile("test_inheritance.st");

   // FB_ACTUATOR is final
   expectContains("struct FB_ACTUATOR");
   expectContains("virtual Bool PROCESS() override");
   expectContains("virtual Bool START()");
   expectContains("virtual Bool STOP()");
   expectContains("virtual void SETPOWER(Float POWERPERCENT)");
   expectContains("virtual void QUEUECOMMAND(Int16 CMD)");
   expectContains("virtual Bool PROCESSQUEUE()");
}

/**
 * @brief Test that multiple interfaces are correctly implemented
 */
TEST_F(ComplexProgramTest, MultipleInterfaces)
{
   generateFromFile("test_inheritance.st");

   // FB_ACTUATOR implements I_DEVICE and I_COMMUNICATE
   expectContains("struct FB_ACTUATOR : public FB_DEVICE, public I_COMMUNICATE");
}

// ============================================================================
//  Additional Tests
// ============================================================================

/**
 * @brief Test that the generated code has the correct namespace
 */
TEST_F(ComplexProgramTest, Namespace)
{
   generateFromFile("test_array.st");

   expectContains("namespace undoCore");
   expectContains("} // namespace undoCore");
}

/**
 * @brief Test that the generated header has proper include guards
 */
TEST_F(ComplexProgramTest, IncludeGuard)
{
   generateFromFile("test_array.st");

   // The header guard should be based on the filename (test.hpp -> TEST_HPP)
   EXPECT_TRUE(result.header.find("#pragma once") != std::string::npos) << "Header should have #pragma once";
}

/**
 * @brief Test that the generated code includes the runtime header
 */
TEST_F(ComplexProgramTest, RuntimeInclude)
{
   generateFromFile("test_array.st");

   EXPECT_TRUE(result.header.find("#include \"undoCore/types.hpp\"") != std::string::npos) << "Should include undoCore/types.hpp";
}

/**
 * @brief Test that global variables are correctly generated
 */
TEST_F(ComplexProgramTest, GlobalVariables)
{
   generateFromFile("test_array.st");

   // Check for global variable declarations
   EXPECT_TRUE(result.header.find("SIMPLE_ARRAY") != std::string::npos);
   EXPECT_TRUE(result.header.find("MATRIX_2X3") != std::string::npos);
   EXPECT_TRUE(result.header.find("PROCESSORS") != std::string::npos);
}

/**
 * @brief Test that function declarations are correctly generated
 */
TEST_F(ComplexProgramTest, FunctionDeclarations)
{
   generateFromFile("test_array.st");

   expectContains("Int16 SUMARRAY(STArray<Int16, 0, 4> ARR);");
   expectContains(
      "Int16 MATRIXMULTIPLY(STArray<STArray<Int16, 1, 2>, 1, 2> A, STArray<STArray<Int16, 1, 2>, 1, 2> B, Int16 ROW, Int16 COL);");
}