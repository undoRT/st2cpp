/**
 * @file test_parser.cpp
 * @brief Implementation of test cases for the Parser
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "parser/Parser.h"
#include "lexer/Lexer.h"

// ============================================================================
//  Global Variables Tests
// ============================================================================

/**
 * @brief Test parsing a simple global variable with AT address
 */
TEST(ParserTest, ParseGlobalVar)
{
   std::string st = R"(
        VAR_GLOBAL
            x AT %IX0.0 : BOOL;
        END_VAR
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.globals.size(), 1);
   ASSERT_EQ(tu.globals[0].decls.size(), 1);
   EXPECT_EQ(tu.globals[0].decls[0].name, "x");
   EXPECT_EQ(tu.globals[0].decls[0].atAddress, "%IX0.0");
   EXPECT_EQ(tu.globals[0].decls[0].type.base, BaseType::BOOL);
}

/**
 * @brief Test parsing multiple global variables
 */
TEST(ParserTest, ParseMultipleGlobals)
{
   std::string st = R"(
        VAR_GLOBAL
            x : INT := 10;
            y : REAL := 3.14;
            z : BOOL := TRUE;
        END_VAR
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.globals.size(), 1);
   ASSERT_EQ(tu.globals[0].decls.size(), 3);
   EXPECT_EQ(tu.globals[0].decls[0].name, "x");
   EXPECT_EQ(tu.globals[0].decls[0].type.base, BaseType::INT);
   EXPECT_EQ(tu.globals[0].decls[1].name, "y");
   EXPECT_EQ(tu.globals[0].decls[1].type.base, BaseType::REAL);
   EXPECT_EQ(tu.globals[0].decls[2].name, "z");
   EXPECT_EQ(tu.globals[0].decls[2].type.base, BaseType::BOOL);
}

/**
 * @brief Test parsing global variables with initial values
 */
TEST(ParserTest, ParseGlobalsWithInit)
{
   std::string st = R"(
        VAR_GLOBAL
            count : INT := 42;
            flag : BOOL := FALSE;
            temp : REAL := 25.5;
        END_VAR
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.globals.size(), 1);
   ASSERT_EQ(tu.globals[0].decls.size(), 3);

   // Check that initial values exist
   EXPECT_NE(tu.globals[0].decls[0].initialValue, nullptr);
   EXPECT_NE(tu.globals[0].decls[1].initialValue, nullptr);
   EXPECT_NE(tu.globals[0].decls[2].initialValue, nullptr);
}

// ============================================================================
//  Function Tests
// ============================================================================

/**
 * @brief Test parsing a simple function with input parameters
 */
TEST(ParserTest, ParseFunction)
{
   std::string st = R"(
        FUNCTION Add : INT
            VAR_INPUT a : INT; b : INT; END_VAR
            Add := a + b;
        END_FUNCTION
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].kind, POUKind::FUNCTION);
   EXPECT_EQ(tu.pous[0].name, "Add");
   EXPECT_EQ(tu.pous[0].returnType.base, BaseType::INT);
}

/**
 * @brief Test parsing a function with multiple input parameters
 */
TEST(ParserTest, ParseFunctionMultipleParams)
{
   std::string st = R"(
        FUNCTION Multiply : INT
            VAR_INPUT
                a : INT;
                b : INT;
                c : INT;
            END_VAR
            Multiply := a * b * c;
        END_FUNCTION
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].kind, POUKind::FUNCTION);
   EXPECT_EQ(tu.pous[0].name, "Multiply");

   // Check input parameters
   bool foundA = false, foundB = false, foundC = false;
   for (const auto& sec : tu.pous[0].varSections) {
      if (sec.kind == VarKind::INPUT) {
         for (const auto& decl : sec.decls) {
            if (decl.name == "a") {
               foundA = true;
            }
            if (decl.name == "b") {
               foundB = true;
            }
            if (decl.name == "c") {
               foundC = true;
            }
         }
      }
   }
   EXPECT_TRUE(foundA);
   EXPECT_TRUE(foundB);
   EXPECT_TRUE(foundC);
}

/**
 * @brief Test parsing a function with output parameters
 */
TEST(ParserTest, ParseFunctionWithOutput)
{
   std::string st = R"(
        FUNCTION Divide : INT
            VAR_INPUT
                numerator : INT;
                denominator : INT;
            END_VAR
            VAR_OUTPUT
                remainder : INT;
            END_VAR
            Divide := numerator / denominator;
            remainder := numerator MOD denominator;
        END_FUNCTION
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].kind, POUKind::FUNCTION);
   EXPECT_EQ(tu.pous[0].name, "Divide");

   // Check output parameter
   bool foundRemainder = false;
   for (const auto& sec : tu.pous[0].varSections) {
      if (sec.kind == VarKind::OUTPUT) {
         for (const auto& decl : sec.decls) {
            if (decl.name == "remainder") {
               foundRemainder = true;
            }
         }
      }
   }
   EXPECT_TRUE(foundRemainder);
}

// ============================================================================
//  Function Block Tests
// ============================================================================

/**
 * @brief Test parsing a simple function block
 */
TEST(ParserTest, ParseFunctionBlock)
{
   std::string st = R"(
        FUNCTION_BLOCK Counter
            VAR_INPUT
                enable : BOOL;
                reset : BOOL;
            END_VAR
            VAR_OUTPUT
                count : INT;
            END_VAR
            VAR
                internal : INT := 0;
            END_VAR
            IF reset THEN
                count := 0;
            ELSIF enable THEN
                count := count + 1;
            END_IF
        END_FUNCTION_BLOCK
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].kind, POUKind::FUNCTION_BLOCK);
   EXPECT_EQ(tu.pous[0].name, "Counter");

   // Check for presence of variable sections
   bool hasInput = false, hasOutput = false, hasVar = false;
   for (const auto& sec : tu.pous[0].varSections) {
      if (sec.kind == VarKind::INPUT) {
         hasInput = true;
      }
      if (sec.kind == VarKind::OUTPUT) {
         hasOutput = true;
      }
      if (sec.kind == VarKind::VAR) {
         hasVar = true;
      }
   }
   EXPECT_TRUE(hasInput);
   EXPECT_TRUE(hasOutput);
   EXPECT_TRUE(hasVar);
}

/**
 * @brief Test parsing a function block with methods
 */
TEST(ParserTest, ParseFunctionBlockWithMethod)
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
            
            METHOD Multiply : INT
                VAR_INPUT x : INT; y : INT; END_VAR
                Multiply := x * y;
            END_METHOD
        END_FUNCTION_BLOCK
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Math");
   ASSERT_EQ(tu.pous[0].methods.size(), 2);
   EXPECT_EQ(tu.pous[0].methods[0].name, "Add");
   EXPECT_EQ(tu.pous[0].methods[1].name, "Multiply");
}

/**
 * @brief Test parsing a function block with EXTENDS and IMPLEMENTS
 */
TEST(ParserTest, ParseFunctionBlockInheritance)
{
   std::string st = R"(
        FUNCTION_BLOCK Base
            VAR
                id : INT;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base IMPLEMENTS I_Interface
            VAR
                extra : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 2);

   // Check Derived FB
   const auto& derived = tu.pous[1];
   EXPECT_EQ(derived.name, "Derived");
   EXPECT_EQ(derived.extends, "Base");
   ASSERT_EQ(derived.implements.size(), 1);
   EXPECT_EQ(derived.implements[0], "I_Interface");
}

// ============================================================================
//  Program Tests
// ============================================================================

/**
 * @brief Test parsing a simple program
 */
TEST(ParserTest, ParseProgram)
{
   std::string st = R"(
        PROGRAM Main
            VAR
                x : INT;
                y : BOOL;
            END_VAR
            x := 10;
            y := TRUE;
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].kind, POUKind::PROGRAM);
   EXPECT_EQ(tu.pous[0].name, "Main");

   // Check variables
   bool foundX = false, foundY = false;
   for (const auto& sec : tu.pous[0].varSections) {
      if (sec.kind == VarKind::VAR) {
         for (const auto& decl : sec.decls) {
            if (decl.name == "x") {
               foundX = true;
            }
            if (decl.name == "y") {
               foundY = true;
            }
         }
      }
   }
   EXPECT_TRUE(foundX);
   EXPECT_TRUE(foundY);
}

/**
 * @brief Test parsing a program with local AT variables
 */
TEST(ParserTest, ParseProgramWithAT)
{
   std::string st = R"(
        PROGRAM Main
            VAR
                input AT %IX0.0 : BOOL;
                output AT %QX0.0 : BOOL;
            END_VAR
            output := input;
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Main");

   // Check AT variables
   bool foundInput = false, foundOutput = false;
   for (const auto& sec : tu.pous[0].varSections) {
      if (sec.kind == VarKind::VAR) {
         for (const auto& decl : sec.decls) {
            if (decl.name == "input" && decl.atAddress == "%IX0.0") {
               foundInput = true;
            }
            if (decl.name == "output" && decl.atAddress == "%QX0.0") {
               foundOutput = true;
            }
         }
      }
   }
   EXPECT_TRUE(foundInput);
   EXPECT_TRUE(foundOutput);
}

// ============================================================================
//  Type Declaration Tests
// ============================================================================

/**
 * @brief Test parsing an ENUM type
 */
TEST(ParserTest, ParseEnum)
{
   std::string st = R"(
        TYPE Color :
            (Red, Green, Blue)
        END_TYPE
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.enums.size(), 1);
   EXPECT_EQ(tu.enums[0].name, "Color");
   ASSERT_EQ(tu.enums[0].enumerators.size(), 3);
   EXPECT_EQ(tu.enums[0].enumerators[0].name, "Red");
   EXPECT_EQ(tu.enums[0].enumerators[1].name, "Green");
   EXPECT_EQ(tu.enums[0].enumerators[2].name, "Blue");
}

/**
 * @brief Test parsing an ENUM with explicit values
 */
TEST(ParserTest, ParseEnumWithValues)
{
   std::string st = R"(
        TYPE Status :
            (
            Idle := 0,
            Running := 1,
            Error := 99
            )
        END_TYPE
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.enums.size(), 1);
   EXPECT_EQ(tu.enums[0].name, "Status");
   ASSERT_EQ(tu.enums[0].enumerators.size(), 3);
   EXPECT_EQ(tu.enums[0].enumerators[0].name, "Idle");
   EXPECT_EQ(tu.enums[0].enumerators[1].name, "Running");
   EXPECT_EQ(tu.enums[0].enumerators[2].name, "Error");
   // Values are stored as expressions; we can check they exist
   EXPECT_NE(tu.enums[0].enumerators[0].value, nullptr);
   EXPECT_NE(tu.enums[0].enumerators[1].value, nullptr);
   EXPECT_NE(tu.enums[0].enumerators[2].value, nullptr);
}

/**
 * @brief Test parsing a STRUCT type
 */
TEST(ParserTest, ParseStruct)
{
   std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.structs.size(), 1);
   EXPECT_EQ(tu.structs[0].name, "Point");
   ASSERT_EQ(tu.structs[0].members.size(), 2);
   EXPECT_EQ(tu.structs[0].members[0].name, "x");
   EXPECT_EQ(tu.structs[0].members[0].type.base, BaseType::INT);
   EXPECT_EQ(tu.structs[0].members[1].name, "y");
   EXPECT_EQ(tu.structs[0].members[1].type.base, BaseType::INT);
}

/**
 * @brief Test parsing a STRUCT with nested STRUCT
 */
TEST(ParserTest, ParseNestedStruct)
{
   std::string st = R"(
        TYPE Point3D :
            STRUCT
                x : INT;
                y : INT;
                z : INT;
            END_STRUCT
        END_TYPE

        TYPE Line :
            STRUCT
                start : Point3D;
                end : Point3D;
            END_STRUCT
        END_TYPE
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.structs.size(), 2);
   EXPECT_EQ(tu.structs[0].name, "Point3D");
   EXPECT_EQ(tu.structs[1].name, "Line");
   ASSERT_EQ(tu.structs[1].members.size(), 2);
   EXPECT_EQ(tu.structs[1].members[0].name, "start");
   EXPECT_EQ(tu.structs[1].members[0].type.base, BaseType::NAMED);
   EXPECT_EQ(tu.structs[1].members[0].type.name, "Point3D");
}

// ============================================================================
//  Array Tests
// ============================================================================

/**
 * @brief Test parsing a 1D array
 */
TEST(ParserTest, ParseArray1D)
{
   std::string st = R"(
        VAR_GLOBAL
            data : ARRAY[0..5] OF INT;
        END_VAR
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.globals.size(), 1);
   ASSERT_EQ(tu.globals[0].decls.size(), 1);
   const auto& decl = tu.globals[0].decls[0];
   EXPECT_EQ(decl.name, "data");
   ASSERT_EQ(decl.type.arrayDims.size(), 1);
   EXPECT_EQ(decl.type.base, BaseType::INT);
}

/**
 * @brief Test parsing a 2D array
 */
TEST(ParserTest, ParseArray2D)
{
   std::string st = R"(
        VAR_GLOBAL
            matrix : ARRAY[0..1, 0..2] OF INT;
        END_VAR
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.globals.size(), 1);
   ASSERT_EQ(tu.globals[0].decls.size(), 1);
   const auto& decl = tu.globals[0].decls[0];
   EXPECT_EQ(decl.name, "matrix");
   ASSERT_EQ(decl.type.arrayDims.size(), 2);
   EXPECT_EQ(decl.type.base, BaseType::INT);
}

// ============================================================================
//  Control Flow Tests
// ============================================================================

/**
 * @brief Test parsing IF statement
 */
TEST(ParserTest, ParseIfStatement)
{
   std::string st = R"(
        PROGRAM Test
            VAR x, y : INT; END_VAR
            IF x > 0 THEN
                y := 1;
            ELSIF x = 0 THEN
                y := 0;
            ELSE
                y := -1;
            END_IF
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Test");
   // Body should contain at least one statement (the IF)
   EXPECT_GT(tu.pous[0].body.size(), 0);
}

/**
 * @brief Test parsing FOR loop
 */
TEST(ParserTest, ParseForLoop)
{
   std::string st = R"(
        PROGRAM Test
            VAR i, sum : INT; END_VAR
            FOR i := 0 TO 10 BY 2 DO
                sum := sum + i;
            END_FOR
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Test");
   EXPECT_GT(tu.pous[0].body.size(), 0);
}

/**
 * @brief Test parsing WHILE loop
 */
TEST(ParserTest, ParseWhileLoop)
{
   std::string st = R"(
        PROGRAM Test
            VAR x : INT; END_VAR
            WHILE x < 10 DO
                x := x + 1;
            END_WHILE
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Test");
   EXPECT_GT(tu.pous[0].body.size(), 0);
}

/**
 * @brief Test parsing CASE statement with ranges
 */
TEST(ParserTest, ParseCaseStatement)
{
   std::string st = R"(
        PROGRAM Test
            VAR value, result : INT; END_VAR
            CASE value OF
                1: result := 10;
                2, 3: result := 20;
                10..20: result := 30;
                ELSE result := 0;
            END_CASE
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.pous.size(), 1);
   EXPECT_EQ(tu.pous[0].name, "Test");
   EXPECT_GT(tu.pous[0].body.size(), 0);
}

// ============================================================================
//  Interface Tests
// ============================================================================

/**
 * @brief Test parsing an INTERFACE
 */
TEST(ParserTest, ParseInterface)
{
   std::string st = R"(
        INTERFACE I_Device
            METHOD Init : BOOL
                VAR_INPUT name : STRING; END_VAR
            END_METHOD
            METHOD Process : BOOL
            END_METHOD
            METHOD GetStatus : INT
            END_METHOD
        END_INTERFACE
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   ASSERT_EQ(tu.interfaces.size(), 1);
   EXPECT_EQ(tu.interfaces[0].name, "I_Device");
   ASSERT_EQ(tu.interfaces[0].methods.size(), 3);
   EXPECT_EQ(tu.interfaces[0].methods[0].name, "Init");
   EXPECT_EQ(tu.interfaces[0].methods[1].name, "Process");
   EXPECT_EQ(tu.interfaces[0].methods[2].name, "GetStatus");
}

// ============================================================================
//  Edge Cases and Complex Scenarios
// ============================================================================

/**
 * @brief Test parsing an empty translation unit
 */
TEST(ParserTest, ParseEmpty)
{
   std::string st = "";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   EXPECT_EQ(tu.globals.size(), 0);
   EXPECT_EQ(tu.pous.size(), 0);
   EXPECT_EQ(tu.enums.size(), 0);
   EXPECT_EQ(tu.structs.size(), 0);
}

/**
 * @brief Test parsing a complete project with all POU types
 */
TEST(ParserTest, ParseCompleteProject)
{
   std::string st = R"(
        VAR_GLOBAL
            global_x : INT := 10;
        END_VAR

        TYPE Color : (Red, Green, Blue) END_TYPE
        
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        FUNCTION Add : INT
            VAR_INPUT a, b : INT; END_VAR
            Add := a + b;
        END_FUNCTION

        FUNCTION_BLOCK Counter
            VAR_INPUT enable : BOOL; END_VAR
            VAR_OUTPUT count : INT; END_VAR
            VAR internal : INT; END_VAR
            IF enable THEN
                count := count + 1;
            END_IF
        END_FUNCTION_BLOCK

        PROGRAM Main
            VAR
                result : INT;
                c : Counter;
            END_VAR
            result := Add(1, 2);
            c();
        END_PROGRAM
    )";
   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   // Check all components
   EXPECT_EQ(tu.globals.size(), 1);
   EXPECT_EQ(tu.enums.size(), 1);
   EXPECT_EQ(tu.structs.size(), 1);
   EXPECT_EQ(tu.pous.size(), 3); // Add, Counter, Main
}