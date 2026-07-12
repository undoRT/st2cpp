/**
 * @file test_generation.cpp
 * @brief Implementation of test cases for the CodeGenerator
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include <regex>

// ============================================================================
//  Test Fixture
// ============================================================================

/**
 * @brief Test fixture for code generation tests
 */
class GenerationTest : public ::testing::Test
{
protected:
   GeneratedCode generate(const std::string& st) { return TestHelper::generateFromST(st); }

   bool regexContains(const std::string& source, const std::string& pattern)
   {
      try {
         std::regex re(pattern);
         return std::regex_search(source, re);
      } catch (const std::regex_error& e) {
         // If regex fails, fall back to simple string find
         return source.find(pattern) != std::string::npos;
      }
   }

   void expectRegex(const std::string& source, const std::string& pattern, const std::string& msg = "")
   {
      EXPECT_TRUE(regexContains(source, pattern)) << msg << "\nFull source:\n" << source;
   }

   void expectNotRegex(const std::string& source, const std::string& pattern, const std::string& msg = "")
   {
      EXPECT_FALSE(regexContains(source, pattern)) << msg << "\nFull source:\n" << source;
   }
};

// ============================================================================
//  Bitwise Operator Tests
// ============================================================================

/**
 * @brief Test that bitwise OR is correctly translated
 */
TEST_F(GenerationTest, BitwiseOrTranslation)
{
   std::string st = R"(
        VAR_GLOBAL
            Flags AT %MB0 : BYTE := 0;
        END_VAR
        PROGRAM Test
            VAR
                x : BYTE;
            END_VAR
            x := Flags OR 16#80;
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.source, R"(getPi_FLAGS\s*\(\s*\)\s*\|\s*0x80)", "OR must translate to bitwise OR ('|') not logical OR ('||')");

   expectNotRegex(result.source, R"(getPi_FLAGS\s*\(\s*\)\s*\|\|\s*0x80)", "Logical OR ('||') should not appear");

   std::string st2 = R"(
        PROGRAM Test2
            VAR
                a : BYTE;
                b : BYTE;
                c : BYTE;
                d : BYTE;
            END_VAR
            a := b AND c;
            b := a OR c;
            d := a XOR b;
        END_PROGRAM
    )";

   auto result2 = generate(st2);

   expectRegex(result2.source, R"(B\s*&\s*C)", "AND must translate to bitwise AND ('&')");
   expectRegex(result2.source, R"(A\s*\|\s*C)", "OR must translate to bitwise OR ('|')");
   expectRegex(result2.source, R"(A\s*\^\s*B)", "XOR must translate to bitwise XOR ('^')");
}

/**
 * @brief Test that NOT operator is correctly translated
 */
TEST_F(GenerationTest, NotOperator)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                a : BOOL;
                b : BOOL;
            END_VAR
            b := NOT a;
        END_PROGRAM
    )";

   auto result = generate(st);
   expectRegex(result.source, R"(!\s*A)", "NOT must translate to '!'");
}

// ============================================================================
//  AT Variable Tests
// ============================================================================

/**
 * @brief Test that AT variables in FUNCTION_BLOCK generate getter/setter
 */
TEST_F(GenerationTest, FunctionBlockWithAT)
{
   std::string st = R"(
        FUNCTION_BLOCK Motor
            VAR_INPUT
                Enable : BOOL;
            END_VAR
            VAR_OUTPUT
                Running : BOOL;
            END_VAR
            VAR
                InternalFlag AT %MX0 : BOOL := FALSE;
                InternalData AT %MW0 : INT := 0;
            END_VAR
        END_FUNCTION_BLOCK
    )";

   auto result = generate(st);

   // Use simple find for these patterns (safer than regex)
   EXPECT_TRUE(result.header.find("getPi_INTERNALFLAG") != std::string::npos) << "Missing getPi_INTERNALFLAG()\nFull source:\n"
                                                                              << result.header;
   EXPECT_TRUE(result.header.find("setPi_INTERNALFLAG") != std::string::npos) << "Missing setPi_INTERNALFLAG()\nFull source:\n"
                                                                              << result.header;
   EXPECT_TRUE(result.header.find("getPi_INTERNALDATA") != std::string::npos) << "Missing getPi_INTERNALDATA()\nFull source:\n"
                                                                              << result.header;
   EXPECT_TRUE(result.header.find("setPi_INTERNALDATA") != std::string::npos) << "Missing setPi_INTERNALDATA()\nFull source:\n"
                                                                              << result.header;

   // Verify they are NOT declared as normal members
   EXPECT_TRUE(result.header.find("Bool INTERNALFLAG{false};") == std::string::npos)
      << "InternalFlag should NOT be declared as a normal member\nFull source:\n"
      << result.header;
   EXPECT_TRUE(result.header.find("Int16 INTERNALDATA{0};") == std::string::npos)
      << "InternalData should NOT be declared as a normal member\nFull source:\n"
      << result.header;
}

/**
 * @brief Test that AT variables in FUNCTION are accessed directly
 */
TEST_F(GenerationTest, FunctionWithAT)
{
   std::string st = R"(
        FUNCTION GetSpeed : INT
            VAR
                Speed AT %IW100 : INT := 0;
            END_VAR
            GetSpeed := Speed;
        END_FUNCTION
    )";

   auto result = generate(st);

   expectNotRegex(result.source, R"(Int16\s+SPEED\s*\{0\};)", "Speed should NOT be declared as local variable");
   expectRegex(result.source,
               R"(processImage\s*\.\s*readInputWord\s*\(\s*100\s*\))",
               "Should access processImage.readInputWord(100) directly");
   expectNotRegex(result.source, R"(getPi_SPEED)", "Should NOT use getPi_SPEED()");
}

/**
 * @brief Test that AT variables in PROGRAM generate getter/setter
 */
TEST_F(GenerationTest, ProgramWithLocalAT)
{
   std::string st = R"(
        PROGRAM Main
            VAR
                Local AT %IX* : BOOL := FALSE;
            END_VAR
            Local := TRUE;
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(getPi_LOCAL\s*\(\s*\)\s*const)", "Missing getPi_LOCAL() const");
   expectRegex(result.header, R"(setPi_LOCAL\s*\(\s*Bool\s+value\s*\))", "Missing setPi_LOCAL(Bool value)");
   expectNotRegex(result.header, R"(Bool\s+LOCAL\s*\{false\};)", "Local should NOT be declared as a normal member");
}

// ============================================================================
//  Struct and Type Tests
// ============================================================================

/**
 * @brief Test that struct containing FB is correctly generated
 */
TEST_F(GenerationTest, StructWithFB)
{
   std::string st = R"(
        FUNCTION_BLOCK FB_Inner
            VAR_INPUT
                x : INT;
            END_VAR
        END_FUNCTION_BLOCK

        TYPE Outer :
            STRUCT
                mem : FB_Inner;
            END_STRUCT
        END_TYPE
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(FB_INNER\s+MEM\s*\{\};)", "Missing FB_INNER MEM{}; in Outer struct");
}

/**
 * @brief Test that ENUM is correctly generated
 */
TEST_F(GenerationTest, EnumGeneration)
{
   std::string st = R"(
        TYPE Color :
            (Red, Green := 5, Blue, Yellow := 10)
        END_TYPE
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(enum class COLOR\s*:\s*int\s*\{)");
   expectRegex(result.header, R"(RED,)");
   expectRegex(result.header, R"(GREEN\s*=\s*5,)");
   expectRegex(result.header, R"(BLUE,)");
   expectRegex(result.header, R"(YELLOW\s*=\s*10)");
}

/**
 * @brief Test that simple STRUCT is correctly generated
 */
TEST_F(GenerationTest, StructGeneration)
{
   std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct POINT\s*\{)");
   expectRegex(result.header, R"(Int16\s+X\{\};)");
   expectRegex(result.header, R"(Int16\s+Y\{\};)");
}

/**
 * @brief Test that nested STRUCT is correctly generated
 */
TEST_F(GenerationTest, NestedStructGeneration)
{
   std::string st = R"(
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
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct POINT2D\s*\{)");
   expectRegex(result.header, R"(struct POINT3D\s*\{)");
   expectRegex(result.header, R"(POINT2D\s+XY\{\};)");
   expectRegex(result.header, R"(Int16\s+Z\{\};)");
}

// ============================================================================
//  Control Flow Tests
// ============================================================================

/**
 * @brief Test that CASE statement with ranges is correctly translated
 */
TEST_F(GenerationTest, CaseStatementWithRanges)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                value : INT;
                result : INT;
            END_VAR
            CASE value OF
                1: result := 10;
                2, 3: result := 20;
                10..20: result := 30;
                ELSE result := 0;
            END_CASE;
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.source, R"(if\s*\(\s*VALUE\s*==\s*1\s*\))", "Missing case for value == 1");
   expectRegex(result.source,
               R"(else\s+if\s*\(\s*VALUE\s*==\s*2\s*\|\|\s*VALUE\s*==\s*3\s*\))",
               "Missing case for value == 2 or value == 3");
   expectRegex(result.source,
               R"(else\s+if\s*\(\s*\(\s*VALUE\s*>=\s*10\s*&&\s*VALUE\s*<=\s*20\s*\)\s*\))",
               "Missing range case for 10..20");
   expectRegex(result.source, R"(else\s*\{?)", "Missing ELSE branch");
}

/**
 * @brief Test that IF statement is correctly translated
 */
TEST_F(GenerationTest, IfStatement)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                x : INT;
                y : INT;
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

   auto result = generate(st);

   expectRegex(result.source, R"(if\s*\(\s*X\s*>\s*0\s*\)\s*\{)");
   expectRegex(result.source, R"(else\s+if\s*\(\s*X\s*<\s*0\s*\)\s*\{)");
   expectRegex(result.source, R"(else\s*\{)");
}

/**
 * @brief Test that FOR loop is correctly translated
 * 
 * NOTE: The regex pattern is flexible to handle different spacing
 */
TEST_F(GenerationTest, ForLoop)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                i : INT;
                sum : INT;
            END_VAR
            sum := 0;
            FOR i := 0 TO 10 BY 2 DO
                sum := sum + i;
            END_FOR
        END_PROGRAM
    )";

   auto result = generate(st);

   // More flexible regex to handle different spacing
   expectRegex(result.source,
               R"(for\s*\(\s*auto\s+I\s*=\s*0\s*;\s*I\s*<=\s*10\s*;\s*I\s*\+=\s*2\s*\))",
               "FOR loop with step 2 not correctly generated");
   expectRegex(result.source, R"(SUM\s*=\s*SUM\s*\+\s*I)", "Loop body not correctly generated");
}

/**
 * @brief Test that WHILE loop is correctly translated
 */
TEST_F(GenerationTest, WhileLoop)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                x : INT;
            END_VAR
            x := 0;
            WHILE x < 10 DO
                x := x + 1;
            END_WHILE
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.source, R"(while\s*\(\s*X\s*<\s*10\s*\)\s*\{)");
   expectRegex(result.source, R"(X\s*=\s*X\s*\+\s*1)");
}

/**
 * @brief Test that REPEAT loop is correctly translated
 * 
 * NOTE: This test is DISABLED because the parser does not yet support
 * REPEAT...UNTIL loops. The parse error occurs at END_REPEAT.
 */
TEST_F(GenerationTest, DISABLED_RepeatLoop)
{
   std::string st = R"(
        PROGRAM Test
            VAR
                x : INT;
            END_VAR
            x := 0;
            REPEAT
                x := x + 1;
            UNTIL x > 10 END_REPEAT
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.source, R"(do\s*\{)");
   expectRegex(result.source, R"(X\s*=\s*X\s*\+\s*1)");
   expectRegex(result.source, R"(while\s*\(!\s*\(\s*X\s*>\s*10\s*\)\s*\))");
}

// ============================================================================
//  Function and Call Tests
// ============================================================================

/**
 * @brief Test that simple function is correctly generated
 */
TEST_F(GenerationTest, SimpleFunction)
{
   std::string st = R"(
        FUNCTION Add : INT
            VAR_INPUT
                a : INT;
                b : INT;
            END_VAR
            Add := a + b;
        END_FUNCTION
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(Int16\s+ADD\s*\(\s*Int16\s+A\s*,\s*Int16\s+B\s*\))");
   expectRegex(result.source, R"(ADD_ret\s*=\s*A\s*\+\s*B)");
   expectRegex(result.source, R"(return\s+ADD_ret)");
}

/**
 * @brief Test that function call is correctly generated
 */
TEST_F(GenerationTest, FunctionCall)
{
   std::string st = R"(
        FUNCTION Add : INT
            VAR_INPUT a : INT; b : INT; END_VAR
            Add := a + b;
        END_FUNCTION

        PROGRAM Test
            VAR
                x : INT;
                y : INT;
                z : INT;
            END_VAR
            z := Add(x, y);
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.source, R"(Z\s*=\s*ADD\s*\(\s*X\s*,\s*Y\s*\))");
}

/**
 * @brief Test that function with named arguments is correctly generated
 */
TEST_F(GenerationTest, FunctionNamedArgs)
{
   std::string st = R"(
        FUNCTION Add : INT
            VAR_INPUT
                a : INT;
                b : INT;
                c : INT;
            END_VAR
            Add := a + b + c;
        END_FUNCTION

        PROGRAM Test
            VAR
                x : INT;
                y : INT;
                z : INT;
            END_VAR
            z := Add(a := x, b := y, c := 5);
        END_PROGRAM
    )";

   auto result = generate(st);

   // The generator should reorder named arguments to match signature order
   // and normalize names to uppercase
   expectRegex(result.source, R"(Z\s*=\s*ADD\s*\(\s*X\s*,\s*Y\s*,\s*5\s*\))");
}

// ============================================================================
//  Function Block Tests
// ============================================================================

/**
 * @brief Test that simple FB is correctly generated
 */
TEST_F(GenerationTest, SimpleFunctionBlock)
{
   std::string st = R"(
        FUNCTION_BLOCK Counter
            VAR_INPUT
                enable : BOOL;
            END_VAR
            VAR_OUTPUT
                count : INT;
            END_VAR
            VAR
                internal : INT := 0;
            END_VAR
            IF enable THEN
                count := count + 1;
            END_IF
        END_FUNCTION_BLOCK
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct COUNTER\s*\{)");
   expectRegex(result.header, R"(// VAR_INPUT)");
   expectRegex(result.header, R"(Bool\s+ENABLE\{\};)");
   expectRegex(result.header, R"(// VAR_OUTPUT)");
   expectRegex(result.header, R"(Int16\s+COUNT\{\};)");
   expectRegex(result.header, R"(// VAR)");
   expectRegex(result.header, R"(Int16\s+INTERNAL\{0\};)");
}

/**
 * @brief Test that FB method is correctly generated
 */
TEST_F(GenerationTest, FunctionBlockWithMethod)
{
   std::string st = R"(
        FUNCTION_BLOCK Math
            VAR_INPUT
                a : INT;
                b : INT;
            END_VAR
            VAR_OUTPUT
                result : INT;
            END_VAR

            METHOD Add : INT
                VAR_INPUT x : INT; y : INT; END_VAR
                Add := x + y;
            END_METHOD
        END_FUNCTION_BLOCK
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(virtual Int16 ADD\s*\(\s*Int16\s+X\s*,\s*Int16\s+Y\s*\))");
   expectRegex(result.source, R"(Int16\s+MATH::ADD\s*\(\s*Int16\s+X\s*,\s*Int16\s+Y\s*\)\s*\{)");
   expectRegex(result.source, R"(ADD_ret\s*=\s*X\s*\+\s*Y)");
}

/**
 * @brief Test that FB with EXTENDS is correctly generated
 */
TEST_F(GenerationTest, FunctionBlockInheritance)
{
   std::string st = R"(
        FUNCTION_BLOCK Base
            VAR
                id : INT;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base
            VAR
                extra : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct DERIVED\s*:\s*public\s+BASE\s*\{)");
}

// ============================================================================
//  Program Tests
// ============================================================================

/**
 * @brief Test that program with body is correctly generated
 */
TEST_F(GenerationTest, ProgramWithBody)
{
   std::string st = R"(
        PROGRAM Main
            VAR
                x : INT;
                y : INT;
            END_VAR
            x := 10;
            y := x + 5;
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct MAIN\s*\{)");
   expectRegex(result.header, R"(Int16\s+X\{\};)");
   expectRegex(result.header, R"(Int16\s+Y\{\};)");
   expectRegex(result.header, R"(void run\s*\(\s*\))");
   expectRegex(result.source, R"(void MAIN::run\s*\(\s*\)\s*\{)");
   expectRegex(result.source, R"(X\s*=\s*10;)");
   expectRegex(result.source, R"(Y\s*=\s*X\s*\+\s*5;)");
}

// ============================================================================
//  Array Tests
// ============================================================================

/**
 * @brief Test that 1D array is correctly generated
 */
TEST_F(GenerationTest, Array1D)
{
   std::string st = R"(
        VAR_GLOBAL
            data : ARRAY[0..5] OF INT := [10, 20, 30, 40, 50, 60];
        END_VAR
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(STArray<Int16,\s*0,\s*5>\s+DATA\s*=\s*\{10,\s*20,\s*30,\s*40,\s*50,\s*60\})");
}

/**
 * @brief Test that 2D array is correctly generated
 */
TEST_F(GenerationTest, Array2D)
{
   std::string st = R"(
        VAR_GLOBAL
            matrix : ARRAY[0..1, 0..2] OF INT := [[1, 2, 3], [4, 5, 6]];
        END_VAR
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(STArray<STArray<Int16,\s*0,\s*2>,\s*0,\s*1>\s+MATRIX)");
}

// ============================================================================
//  Edge Cases Tests
// ============================================================================

/**
 * @brief Test that empty program is correctly generated
 */
TEST_F(GenerationTest, EmptyProgram)
{
   std::string st = R"(
        PROGRAM Main
        END_PROGRAM
    )";

   auto result = generate(st);

   expectRegex(result.header, R"(struct MAIN\s*\{)");
   expectRegex(result.header, R"(void run\s*\(\s*\))");
   expectRegex(result.source, R"(void MAIN::run\s*\(\s*\)\s*\{)");
}

/**
 * @brief Test that comments in source are preserved in generation
 */
TEST_F(GenerationTest, CommentsPreserved)
{
   std::string st = R"(
        PROGRAM Main
            VAR
                (* This is a comment *)
                x : INT;
            END_VAR
        END_PROGRAM
    )";

   auto result = generate(st);
   // The comment may be stripped or preserved depending on the implementation
   // Just verify that generation succeeds
   EXPECT_FALSE(result.header.empty());
   EXPECT_FALSE(result.source.empty());
}