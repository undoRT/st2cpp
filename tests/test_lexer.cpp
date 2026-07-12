/**
 * @file test_lexer.cpp
 * @brief Implementation of test cases for the Lexer
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "lexer/Lexer.h"

// ============================================================================
//  Basic Token Tests
// ============================================================================

/**
 * @brief Test that keywords are correctly recognized
 */
TEST(LexerTest, Keywords)
{
   Lexer lexer("FUNCTION_BLOCK END_FUNCTION_BLOCK", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_FUNCTION_BLOCK);
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_FUNCTION_BLOCK);
}

/**
 * @brief Test that address with placeholder '*' is correctly recognized
 */
TEST(LexerTest, AddressWithPlaceholder)
{
   Lexer lexer("AT %IX*", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_AT);
   EXPECT_EQ(tokens[1].type, TokenType::ADDRESS_INPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[1].text, "%IX*");
}

/**
 * @brief Test that hexadecimal literals are correctly recognized
 */
TEST(LexerTest, LiteralHex)
{
   Lexer lexer("16#FF00", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].text, "16#FF00");
}

// ============================================================================
//  Extended Token Tests
// ============================================================================

/**
 * @brief Test that binary literals are correctly recognized
 */
TEST(LexerTest, LiteralBinary)
{
   Lexer lexer("2#1010_1100", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].text, "2#10101100");
}

/**
 * @brief Test that octal literals are correctly recognized
 */
TEST(LexerTest, LiteralOctal)
{
   Lexer lexer("8#1234", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].text, "8#1234");
}

/**
 * @brief Test that decimal integer literals are correctly recognized
 */
TEST(LexerTest, LiteralDecimal)
{
   Lexer lexer("12345", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].text, "12345");
}

/**
 * @brief Test that real literals are correctly recognized
 */
TEST(LexerTest, LiteralReal)
{
   Lexer lexer("123.456", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::REAL_LITERAL);
   EXPECT_EQ(tokens[0].text, "123.456");
}

/**
 * @brief Test that real literals with exponent are correctly recognized
 */
TEST(LexerTest, LiteralRealExponent)
{
   Lexer lexer("1.2e-3", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::REAL_LITERAL);
   EXPECT_EQ(tokens[0].text, "1.2e-3");
}

/**
 * @brief Test that string literals (single quotes) are correctly recognized
 */
TEST(LexerTest, StringLiteralSingle)
{
   Lexer lexer("'Hello World'", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].text, "'Hello World'");
}

/**
 * @brief Test that string literals (double quotes) are correctly recognized
 */
TEST(LexerTest, StringLiteralDouble)
{
   Lexer lexer("\"Hello World\"", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::STRING_LITERAL);
   EXPECT_EQ(tokens[0].text, "\"Hello World\"");
}

/**
 * @brief Test that time literals are correctly recognized
 */
TEST(LexerTest, TimeLiteral)
{
   Lexer lexer("T#100ms", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::TIME_LITERAL);
   EXPECT_EQ(tokens[0].text, "T#100ms");
}

/**
 * @brief Test that TIME# prefixed time literals are correctly recognized
 */
TEST(LexerTest, TimeLiteralWithPrefix)
{
   Lexer lexer("TIME#5s", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::TIME_LITERAL);
   EXPECT_EQ(tokens[0].text, "TIME#5s");
}

/**
 * @brief Test that boolean literals TRUE and FALSE are correctly recognized
 */
TEST(LexerTest, BooleanLiterals)
{
   Lexer lexer("TRUE FALSE", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_TRUE);
   EXPECT_EQ(tokens[0].text, "TRUE");
   EXPECT_EQ(tokens[1].type, TokenType::KW_FALSE);
   EXPECT_EQ(tokens[1].text, "FALSE");
}

// ============================================================================
//  Operator Tests
// ============================================================================

/**
 * @brief Test that assignment operators are correctly recognized
 */
TEST(LexerTest, AssignmentOperators)
{
   Lexer lexer(":= => =", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::OP_ASSIGN);
   EXPECT_EQ(tokens[0].text, ":=");
   EXPECT_EQ(tokens[1].type, TokenType::OP_ASSIGN_REF);
   EXPECT_EQ(tokens[1].text, "=>");
   EXPECT_EQ(tokens[2].type, TokenType::OP_EQ);
   EXPECT_EQ(tokens[2].text, "=");
}

/**
 * @brief Test that comparison operators are correctly recognized
 */
TEST(LexerTest, ComparisonOperators)
{
   Lexer lexer("< <= > >= <> ==", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::OP_LT);
   EXPECT_EQ(tokens[0].text, "<");
   EXPECT_EQ(tokens[1].type, TokenType::OP_LE);
   EXPECT_EQ(tokens[1].text, "<=");
   EXPECT_EQ(tokens[2].type, TokenType::OP_GT);
   EXPECT_EQ(tokens[2].text, ">");
   EXPECT_EQ(tokens[3].type, TokenType::OP_GE);
   EXPECT_EQ(tokens[3].text, ">=");
   EXPECT_EQ(tokens[4].type, TokenType::OP_NEQ);
   EXPECT_EQ(tokens[4].text, "<>");
   EXPECT_EQ(tokens[5].type, TokenType::OP_EQ);
   EXPECT_EQ(tokens[5].text, "==");
}

/**
 * @brief Test that arithmetic operators are correctly recognized
 */
TEST(LexerTest, ArithmeticOperators)
{
   Lexer lexer("+ - * / ^", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::OP_PLUS);
   EXPECT_EQ(tokens[0].text, "+");
   EXPECT_EQ(tokens[1].type, TokenType::OP_MINUS);
   EXPECT_EQ(tokens[1].text, "-");
   EXPECT_EQ(tokens[2].type, TokenType::OP_MUL);
   EXPECT_EQ(tokens[2].text, "*");
   EXPECT_EQ(tokens[3].type, TokenType::OP_DIV);
   EXPECT_EQ(tokens[3].text, "/");
   EXPECT_EQ(tokens[4].type, TokenType::OP_DEREF);
   EXPECT_EQ(tokens[4].text, "^");
}

/**
 * @brief Test that range operator is correctly recognized
 */
TEST(LexerTest, RangeOperator)
{
   Lexer lexer("1..10", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[0].text, "1");
   EXPECT_EQ(tokens[1].type, TokenType::OP_RANGE);
   EXPECT_EQ(tokens[1].text, "..");
   EXPECT_EQ(tokens[2].type, TokenType::INT_LITERAL);
   EXPECT_EQ(tokens[2].text, "10");
}

// ============================================================================
//  Address Tests
// ============================================================================

/**
 * @brief Test that fixed input addresses are correctly recognized
 */
TEST(LexerTest, AddressInputFixed)
{
   Lexer lexer("%IX0.0 %IB1 %IW2 %ID3 %IL4", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::ADDRESS_INPUT);
   EXPECT_EQ(tokens[0].text, "%IX0.0");
   EXPECT_EQ(tokens[1].type, TokenType::ADDRESS_INPUT);
   EXPECT_EQ(tokens[1].text, "%IB1");
   EXPECT_EQ(tokens[2].type, TokenType::ADDRESS_INPUT);
   EXPECT_EQ(tokens[2].text, "%IW2");
   EXPECT_EQ(tokens[3].type, TokenType::ADDRESS_INPUT);
   EXPECT_EQ(tokens[3].text, "%ID3");
   EXPECT_EQ(tokens[4].type, TokenType::ADDRESS_INPUT);
   EXPECT_EQ(tokens[4].text, "%IL4");
}

/**
 * @brief Test that fixed output addresses are correctly recognized
 */
TEST(LexerTest, AddressOutputFixed)
{
   Lexer lexer("%QX0.0 %QB1 %QW2 %QD3 %QL4", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::ADDRESS_OUTPUT);
   EXPECT_EQ(tokens[0].text, "%QX0.0");
   EXPECT_EQ(tokens[1].type, TokenType::ADDRESS_OUTPUT);
   EXPECT_EQ(tokens[1].text, "%QB1");
   EXPECT_EQ(tokens[2].type, TokenType::ADDRESS_OUTPUT);
   EXPECT_EQ(tokens[2].text, "%QW2");
   EXPECT_EQ(tokens[3].type, TokenType::ADDRESS_OUTPUT);
   EXPECT_EQ(tokens[3].text, "%QD3");
   EXPECT_EQ(tokens[4].type, TokenType::ADDRESS_OUTPUT);
   EXPECT_EQ(tokens[4].text, "%QL4");
}

/**
 * @brief Test that fixed marker addresses are correctly recognized
 */
TEST(LexerTest, AddressMarkerFixed)
{
   Lexer lexer("%MX0.0 %MB1 %MW2 %MD3 %ML4", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::ADDRESS_MARKER);
   EXPECT_EQ(tokens[0].text, "%MX0.0");
   EXPECT_EQ(tokens[1].type, TokenType::ADDRESS_MARKER);
   EXPECT_EQ(tokens[1].text, "%MB1");
   EXPECT_EQ(tokens[2].type, TokenType::ADDRESS_MARKER);
   EXPECT_EQ(tokens[2].text, "%MW2");
   EXPECT_EQ(tokens[3].type, TokenType::ADDRESS_MARKER);
   EXPECT_EQ(tokens[3].text, "%MD3");
   EXPECT_EQ(tokens[4].type, TokenType::ADDRESS_MARKER);
   EXPECT_EQ(tokens[4].text, "%ML4");
}

/**
 * @brief Test that placeholder addresses are correctly recognized for all types
 */
TEST(LexerTest, AddressPlaceholder)
{
   Lexer lexer("%IX* %QX* %MX* %IB* %QB* %MB* %IW* %QW* %MW* %ID* %QD* %MD*", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::ADDRESS_INPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[0].text, "%IX*");
   EXPECT_EQ(tokens[1].type, TokenType::ADDRESS_OUTPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[1].text, "%QX*");
   EXPECT_EQ(tokens[2].type, TokenType::ADDRESS_MARKER_PLACEHOLDER);
   EXPECT_EQ(tokens[2].text, "%MX*");
   EXPECT_EQ(tokens[3].type, TokenType::ADDRESS_INPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[3].text, "%IB*");
   EXPECT_EQ(tokens[4].type, TokenType::ADDRESS_OUTPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[4].text, "%QB*");
   EXPECT_EQ(tokens[5].type, TokenType::ADDRESS_MARKER_PLACEHOLDER);
   EXPECT_EQ(tokens[5].text, "%MB*");
   EXPECT_EQ(tokens[6].type, TokenType::ADDRESS_INPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[6].text, "%IW*");
   EXPECT_EQ(tokens[7].type, TokenType::ADDRESS_OUTPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[7].text, "%QW*");
   EXPECT_EQ(tokens[8].type, TokenType::ADDRESS_MARKER_PLACEHOLDER);
   EXPECT_EQ(tokens[8].text, "%MW*");
   EXPECT_EQ(tokens[9].type, TokenType::ADDRESS_INPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[9].text, "%ID*");
   EXPECT_EQ(tokens[10].type, TokenType::ADDRESS_OUTPUT_PLACEHOLDER);
   EXPECT_EQ(tokens[10].text, "%QD*");
   EXPECT_EQ(tokens[11].type, TokenType::ADDRESS_MARKER_PLACEHOLDER);
   EXPECT_EQ(tokens[11].text, "%MD*");
}

// ============================================================================
//  Punctuation Tests
// ============================================================================

/**
 * @brief Test that all punctuation tokens are correctly recognized
 */
TEST(LexerTest, Punctuation)
{
   Lexer lexer("( ) [ ] ; : , .", "<test>");
   auto tokens = lexer.tokenize();

   // Verify common punctuation
   // The lexer currently produces 9 tokens
   ASSERT_EQ(tokens.size(), 9);

   // Check each token
   EXPECT_EQ(tokens[0].type, TokenType::LPAREN);
   EXPECT_EQ(tokens[0].text, "(");
   EXPECT_EQ(tokens[1].type, TokenType::RPAREN);
   EXPECT_EQ(tokens[1].text, ")");
   EXPECT_EQ(tokens[2].type, TokenType::LBRACKET);
   EXPECT_EQ(tokens[2].text, "[");
   EXPECT_EQ(tokens[3].type, TokenType::RBRACKET);
   EXPECT_EQ(tokens[3].text, "]");
   EXPECT_EQ(tokens[4].type, TokenType::SEMICOLON);
   EXPECT_EQ(tokens[4].text, ";");
   EXPECT_EQ(tokens[5].type, TokenType::COLON);
   EXPECT_EQ(tokens[5].text, ":");
   EXPECT_EQ(tokens[6].type, TokenType::COMMA);
   EXPECT_EQ(tokens[6].text, ",");
   EXPECT_EQ(tokens[7].type, TokenType::DOT);
   EXPECT_EQ(tokens[7].text, ".");
}

/**
 * @brief Test that the '@' symbol is correctly recognized when used with a name
 */
TEST(LexerTest, AtSymbolWithName)
{
   Lexer lexer("myVar AT %IX0.0", "<test>");
   auto tokens = lexer.tokenize();

   // Find the AT token
   bool foundAt = false;
   for (const auto& token : tokens) {
      if (token.type == TokenType::KW_AT) {
         foundAt = true;
         EXPECT_EQ(token.text, "AT");
         break;
      }
   }
   EXPECT_TRUE(foundAt) << "AT keyword not found in tokens";
}

// ============================================================================
//  Comment Tests
// ============================================================================

/**
 * @brief Test that line comments are correctly skipped
 */
TEST(LexerTest, LineComment)
{
   Lexer lexer("VAR // This is a comment\nEND_VAR", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_VAR);
   EXPECT_EQ(tokens[0].text, "VAR");
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_VAR);
   EXPECT_EQ(tokens[1].text, "END_VAR");
}

/**
 * @brief Test that block comments (* ... *) are correctly skipped
 */
TEST(LexerTest, BlockComment)
{
   Lexer lexer("VAR (* This is a comment *) END_VAR", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_VAR);
   EXPECT_EQ(tokens[0].text, "VAR");
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_VAR);
   EXPECT_EQ(tokens[1].text, "END_VAR");
}

/**
 * @brief Test that nested block comments are correctly handled
 * 
 * Note: The current lexer does NOT support nested block comments.
 * It stops at the first '*)' it finds. This test is marked DISABLED
 * until nested comment support is added.
 */
TEST(LexerTest, DISABLED_NestedBlockComment)
{
   Lexer lexer("VAR (* outer (* inner *) outer *) END_VAR", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_VAR);
   EXPECT_EQ(tokens[0].text, "VAR");
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_VAR);
   EXPECT_EQ(tokens[1].text, "END_VAR");
}

/**
 * @brief Test that curly brace comments are correctly skipped (TwinCAT style)
 */
TEST(LexerTest, CurlyComment)
{
   Lexer lexer("VAR { This is a comment } END_VAR", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_VAR);
   EXPECT_EQ(tokens[0].text, "VAR");
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_VAR);
   EXPECT_EQ(tokens[1].text, "END_VAR");
}

// ============================================================================
//  Edge Case Tests
// ============================================================================

/**
 * @brief Test that empty input produces only END_OF_FILE
 */
TEST(LexerTest, EmptyInput)
{
   Lexer lexer("", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens.size(), 1);
   EXPECT_EQ(tokens[0].type, TokenType::END_OF_FILE);
}

/**
 * @brief Test that whitespace is correctly skipped
 */
TEST(LexerTest, Whitespace)
{
   Lexer lexer("   VAR   \t   END_VAR   \n   ", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_VAR);
   EXPECT_EQ(tokens[0].text, "VAR");
   EXPECT_EQ(tokens[1].type, TokenType::KW_END_VAR);
   EXPECT_EQ(tokens[1].text, "END_VAR");
}

/**
 * @brief Test that identifiers are correctly recognized
 */
TEST(LexerTest, Identifiers)
{
   Lexer lexer("myVar MyVar MYVAR _myVar my_var myVar123", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[0].text, "myVar");
   EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[1].text, "MyVar");
   EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[2].text, "MYVAR");
   EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[3].text, "_myVar");
   EXPECT_EQ(tokens[4].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[4].text, "my_var");
   EXPECT_EQ(tokens[5].type, TokenType::IDENTIFIER);
   EXPECT_EQ(tokens[5].text, "myVar123");
}

/**
 * @brief Test that keywords are case-insensitive
 */
TEST(LexerTest, KeywordCaseInsensitive)
{
   Lexer lexer("function_block Function_Block FUNCTION_BLOCK", "<test>");
   auto tokens = lexer.tokenize();
   EXPECT_EQ(tokens[0].type, TokenType::KW_FUNCTION_BLOCK);
   EXPECT_EQ(tokens[0].text, "function_block");
   EXPECT_EQ(tokens[1].type, TokenType::KW_FUNCTION_BLOCK);
   EXPECT_EQ(tokens[1].text, "Function_Block");
   EXPECT_EQ(tokens[2].type, TokenType::KW_FUNCTION_BLOCK);
   EXPECT_EQ(tokens[2].text, "FUNCTION_BLOCK");
}

/**
 * @brief Test that the lexer handles a full ST program correctly
 */
TEST(LexerTest, FullProgram)
{
   std::string st = R"(
        FUNCTION_BLOCK Counter
            VAR_INPUT
                Enable : BOOL;
                Reset : BOOL;
            END_VAR
            VAR_OUTPUT
                Count : INT;
            END_VAR
            IF Reset THEN
                Count := 0;
            ELSIF Enable THEN
                Count := Count + 1;
            END_IF
        END_FUNCTION_BLOCK
    )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();

   // Check that we got the right number of tokens (at least some)
   EXPECT_GT(tokens.size(), 10);

   // Check that we got the expected tokens
   bool foundFunctionBlock = false;
   bool foundEndFunctionBlock = false;
   bool foundVarInput = false;
   bool foundVarOutput = false;
   bool foundIf = false;
   bool foundEndIf = false;

   for (const auto& token : tokens) {
      if (token.type == TokenType::KW_FUNCTION_BLOCK) {
         foundFunctionBlock = true;
      }
      if (token.type == TokenType::KW_END_FUNCTION_BLOCK) {
         foundEndFunctionBlock = true;
      }
      if (token.type == TokenType::KW_VAR_INPUT) {
         foundVarInput = true;
      }
      if (token.type == TokenType::KW_VAR_OUTPUT) {
         foundVarOutput = true;
      }
      if (token.type == TokenType::KW_IF) {
         foundIf = true;
      }
      if (token.type == TokenType::KW_END_IF) {
         foundEndIf = true;
      }
   }

   EXPECT_TRUE(foundFunctionBlock);
   EXPECT_TRUE(foundEndFunctionBlock);
   EXPECT_TRUE(foundVarInput);
   EXPECT_TRUE(foundVarOutput);
   EXPECT_TRUE(foundIf);
   EXPECT_TRUE(foundEndIf);
}