/**
 * @file Lexer.h
 * @brief Lexical analyzer for Structured Text
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "Token.h"
#include <vector>
#include <unordered_map>

class Lexer
{
public:
   // ============================================================================
   //  Constructor
   // ============================================================================

   explicit Lexer(std::string source, std::string filename = "<input>");

   // ============================================================================
   //  Main Tokenization
   // ============================================================================

   std::vector<Token> tokenize();

private:
   std::string m_src;
   std::string m_file;
   size_t m_pos = 0;
   uint32_t m_line = 1;
   uint32_t m_col = 1;

   static const std::unordered_map<std::string, TokenType> s_keywords;

   // ============================================================================
   //  Helpers
   // ============================================================================

   char peek(int offset = 0) const;
   char advance();
   bool match(char expected);

   // ============================================================================
   //  Whitespace and Comment Skipping
   // ============================================================================

   void skipWhitespaceAndComments();

   // ============================================================================
   //  Token Reading Functions
   // ============================================================================

   Token readIdentifierOrKeyword();
   Token readNumber();
   Token readString();
   Token readTimeLiteral();
   Token makeToken(TokenType t, const std::string& txt) const;
};