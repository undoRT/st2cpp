/**
 * @file Token.h
 * @brief Token definitions for the ST lexer
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <string>
#include <cstdint>

enum class TokenType {
   INT_LITERAL,
   REAL_LITERAL,
   BOOL_LITERAL,
   STRING_LITERAL,
   TIME_LITERAL,
   IDENTIFIER,

   KW_FUNCTION_BLOCK,
   KW_END_FUNCTION_BLOCK,
   KW_FUNCTION,
   KW_END_FUNCTION,
   KW_PROGRAM,
   KW_END_PROGRAM,
   KW_METHOD,
   KW_PRIVATE,
   KW_PROTECTED,
   KW_PUBLIC,
   KW_END_METHOD,
   KW_PROPERTY,
   KW_END_PROPERTY,

   KW_VAR,
   KW_END_VAR,
   KW_VAR_INPUT,
   KW_VAR_OUTPUT,
   KW_VAR_IN_OUT,
   KW_VAR_EXTERNAL,
   KW_VAR_GLOBAL,
   KW_VAR_TEMP,
   KW_CONSTANT,
   KW_RETAIN,

   KW_ATTRIBUTE,
   KW_END_ATTRIBUTE,

   KW_IF,
   KW_THEN,
   KW_ELSIF,
   KW_ELSE,
   KW_END_IF,
   KW_FOR,
   KW_TO,
   KW_BY,
   KW_DO,
   KW_END_FOR,
   KW_WHILE,
   KW_END_WHILE,
   KW_REPEAT,
   KW_UNTIL,
   KW_END_REPEAT,
   KW_CASE,
   KW_OF,
   KW_END_CASE,
   KW_EXIT,
   KW_RETURN,

   KW_BOOL,
   KW_SINT,
   KW_INT,
   KW_DINT,
   KW_LINT,
   KW_USINT,
   KW_UINT,
   KW_UDINT,
   KW_ULINT,
   KW_REAL,
   KW_LREAL,
   KW_BYTE,
   KW_WORD,
   KW_DWORD,
   KW_LWORD,
   KW_STRING,
   KW_WSTRING,
   KW_TIME,
   KW_DATE,
   KW_DT,
   KW_TOD,
   KW_ARRAY,
   KW_TYPE,
   KW_END_TYPE,
   KW_STRUCT,
   KW_END_STRUCT,
   KW_ENUM,
   KW_POINTER,
   KW_REF_TO,

   KW_INTERFACE,
   KW_END_INTERFACE,
   KW_EXTENDS,
   KW_IMPLEMENTS,
   KW_ABSTRACT,
   KW_FINAL,
   KW_OVERRIDE,
   KW_SUPER,

   KW_ADR,
   KW_SIZEOF,
   KW_NOT,
   KW_AND,
   KW_OR,
   KW_XOR,
   KW_MOD,
   KW_TRUE,
   KW_FALSE,
   KW_AT,

   OP_ASSIGN,
   OP_ASSIGN_REF,
   OP_DEREF,
   OP_RANGE,
   OP_EQ,
   OP_NEQ,
   OP_LT,
   OP_LE,
   OP_GT,
   OP_GE,
   OP_PLUS,
   OP_MINUS,
   OP_MUL,
   OP_DIV,
   OP_POWER,

   LPAREN,
   RPAREN,
   LBRACKET,
   RBRACKET,
   SEMICOLON,
   COLON,
   COMMA,
   DOT,
   AT,
   PERCENT,

   ADDRESS_INPUT,  // %I
   ADDRESS_OUTPUT, // %Q
   ADDRESS_MARKER, // %M
   ADDRESS_TEMP,   // %T
   ADDRESS_DIRECT, // %D

   ADDR_BIT,     // %IX, %QX, %MX
   ADDR_BYTE,    // %IB, %QB, %MB
   ADDR_WORD,    // %IW, %QW, %MW
   ADDR_DWORD,   // %ID, %QD, %MD
   ADDR_LWORD,   // %IL, %QL, %ML
   ADDR_POINTER, // %P

   // Address with placeholder '*' (e.g., %IX*, %QW*, %MD*)
   ADDRESS_INPUT_PLACEHOLDER,
   ADDRESS_OUTPUT_PLACEHOLDER,
   ADDRESS_MARKER_PLACEHOLDER,

   END_OF_FILE,
   UNKNOWN
};

struct Token
{
   TokenType type;
   std::string text;
   uint32_t line;
   uint32_t col;

   Token(TokenType t, std::string txt, uint32_t ln, uint32_t c) : type(t), text(std::move(txt)), line(ln), col(c) {}
};
