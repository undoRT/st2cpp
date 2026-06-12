/**
 * @file Lexer.cpp
 * @brief Lexical analyzer implementation for Structured Text
 *
 * Implements tokenization routines declared in `Lexer.h`. Handles identifiers,
 * literals, comments and all ST-specific token forms.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "lexer/Lexer.h"
#include <cctype>
#include <stdexcept>
#include <algorithm>

// ============================================================================
//  Keyword Table
// ============================================================================

const std::unordered_map<std::string, TokenType> Lexer::s_keywords = {
   {"FUNCTION_BLOCK", TokenType::KW_FUNCTION_BLOCK},
   {"END_FUNCTION_BLOCK", TokenType::KW_END_FUNCTION_BLOCK},
   {"FUNCTION", TokenType::KW_FUNCTION},
   {"END_FUNCTION", TokenType::KW_END_FUNCTION},
   {"PROGRAM", TokenType::KW_PROGRAM},
   {"END_PROGRAM", TokenType::KW_END_PROGRAM},
   {"ATTRIBUTE", TokenType::KW_ATTRIBUTE},
   {"END_ATTRIBUTE", TokenType::KW_END_ATTRIBUTE},

   {"VAR", TokenType::KW_VAR},
   {"END_VAR", TokenType::KW_END_VAR},
   {"VAR_INPUT", TokenType::KW_VAR_INPUT},
   {"VAR_OUTPUT", TokenType::KW_VAR_OUTPUT},
   {"VAR_IN_OUT", TokenType::KW_VAR_IN_OUT},
   {"VAR_EXTERNAL", TokenType::KW_VAR_EXTERNAL},
   {"VAR_GLOBAL", TokenType::KW_VAR_GLOBAL},
   {"VAR_TEMP", TokenType::KW_VAR_TEMP},
   {"CONSTANT", TokenType::KW_CONSTANT},
   {"RETAIN", TokenType::KW_RETAIN},
   {"METHOD", TokenType::KW_METHOD},
   {"END_METHOD", TokenType::KW_END_METHOD},
   {"PROPERTY", TokenType::KW_PROPERTY},
   {"END_PROPERTY", TokenType::KW_END_PROPERTY},

   {"INTERFACE", TokenType::KW_INTERFACE},
   {"END_INTERFACE", TokenType::KW_END_INTERFACE},
   {"EXTENDS", TokenType::KW_EXTENDS},
   {"IMPLEMENTS", TokenType::KW_IMPLEMENTS},
   {"ABSTRACT", TokenType::KW_ABSTRACT},
   {"FINAL", TokenType::KW_FINAL},
   {"OVERRIDE", TokenType::KW_OVERRIDE},
   {"SUPER", TokenType::KW_SUPER},

   {"IF", TokenType::KW_IF},
   {"THEN", TokenType::KW_THEN},
   {"ELSIF", TokenType::KW_ELSIF},
   {"ELSE", TokenType::KW_ELSE},
   {"END_IF", TokenType::KW_END_IF},
   {"FOR", TokenType::KW_FOR},
   {"TO", TokenType::KW_TO},
   {"BY", TokenType::KW_BY},
   {"DO", TokenType::KW_DO},
   {"END_FOR", TokenType::KW_END_FOR},
   {"WHILE", TokenType::KW_WHILE},
   {"END_WHILE", TokenType::KW_END_WHILE},
   {"REPEAT", TokenType::KW_REPEAT},
   {"UNTIL", TokenType::KW_UNTIL},
   {"END_REPEAT", TokenType::KW_END_REPEAT},
   {"CASE", TokenType::KW_CASE},
   {"OF", TokenType::KW_OF},
   {"END_CASE", TokenType::KW_END_CASE},
   {"EXIT", TokenType::KW_EXIT},
   {"RETURN", TokenType::KW_RETURN},

   {"BOOL", TokenType::KW_BOOL},
   {"SINT", TokenType::KW_SINT},
   {"INT", TokenType::KW_INT},
   {"DINT", TokenType::KW_DINT},
   {"LINT", TokenType::KW_LINT},
   {"USINT", TokenType::KW_USINT},
   {"UINT", TokenType::KW_UINT},
   {"UDINT", TokenType::KW_UDINT},
   {"ULINT", TokenType::KW_ULINT},
   {"REAL", TokenType::KW_REAL},
   {"LREAL", TokenType::KW_LREAL},
   {"BYTE", TokenType::KW_BYTE},
   {"WORD", TokenType::KW_WORD},
   {"DWORD", TokenType::KW_DWORD},
   {"LWORD", TokenType::KW_LWORD},
   {"STRING", TokenType::KW_STRING},
   {"WSTRING", TokenType::KW_WSTRING},
   {"TIME", TokenType::KW_TIME},
   {"DATE", TokenType::KW_DATE},
   {"DT", TokenType::KW_DT},
   {"TOD", TokenType::KW_TOD},
   {"ARRAY", TokenType::KW_ARRAY},
   {"TYPE", TokenType::KW_TYPE},
   {"END_TYPE", TokenType::KW_END_TYPE},
   {"STRUCT", TokenType::KW_STRUCT},
   {"END_STRUCT", TokenType::KW_END_STRUCT},
   {"ENUM", TokenType::KW_ENUM},
   {"POINTER", TokenType::KW_POINTER},
   {"REF_TO", TokenType::KW_REF_TO},

   {"ADR", TokenType::KW_ADR},
   {"SIZEOF", TokenType::KW_SIZEOF},
   {"NOT", TokenType::KW_NOT},
   {"AND", TokenType::KW_AND},
   {"OR", TokenType::KW_OR},
   {"XOR", TokenType::KW_XOR},
   {"MOD", TokenType::KW_MOD},
   {"TRUE", TokenType::KW_TRUE},
   {"FALSE", TokenType::KW_FALSE},
};

// ============================================================================
//  Constructor and Helpers
// ============================================================================

/**
 * @brief Construct a new Lexer object
 * @param source Source code string
 * @param filename Optional filename for error messages
 */
Lexer::Lexer(std::string source, std::string filename) : m_src(std::move(source)), m_file(std::move(filename)) {}

/**
 * @brief Look ahead without consuming characters
 * @param offset Number of characters to look ahead
 * @return Character at the specified offset, or '\0' if past end
 */
char Lexer::peek(int offset) const
{
   size_t idx = m_pos + offset;
   return (idx < m_src.size()) ? m_src[idx] : '\0';
}

/**
 * @brief Consume one character and advance position
 * @return The consumed character
 */
char Lexer::advance()
{
   char c = m_src[m_pos++];
   if (c == '\n') {
      ++m_line;
      m_col = 1;
   } else {
      ++m_col;
   }
   return c;
}

/**
 * @brief Consume character if it matches the expected one
 * @param expected Character to match
 * @return true if matched and consumed
 */
bool Lexer::match(char expected)
{
   if (m_pos < m_src.size() && m_src[m_pos] == expected) {
      advance();
      return true;
   }
   return false;
}

/**
 * @brief Create a token at the current position
 * @param t Token type
 * @param txt Token text
 * @return Token object with current line/column
 */
Token Lexer::makeToken(TokenType t, const std::string& txt) const
{
   return Token(t, txt, m_line, m_col);
}

// ============================================================================
//  Whitespace and Comment Skipping
// ============================================================================

/**
 * @brief Skip whitespace and all comment types
 *
 * Handles:
 * - Whitespace (space, tab, newline)
 * - Line comments (// ...)
 * - Block comments ((* ... *))
 * - Curly brace comments ({ ... }) - TwinCAT style
 */
void Lexer::skipWhitespaceAndComments()
{
   while (m_pos < m_src.size()) {
      char c = peek();
      if (std::isspace(static_cast<unsigned char>(c))) {
         advance();
      }
      // Line comment: //
      else if (c == '/' && peek(1) == '/') {
         while (m_pos < m_src.size() && peek() != '\n') {
            advance();
         }
      }
      // Block comment: (* ... *)
      else if (c == '(' && peek(1) == '*') {
         advance();
         advance(); // consume (*
         while (m_pos < m_src.size()) {
            if (peek() == '*' && peek(1) == ')') {
               advance();
               advance();
               break;
            }
            advance();
         }
      }
      // Block comment: { ... }
      else if (c == '{') {
         while (m_pos < m_src.size() && peek() != '}') {
            advance();
         }
         if (m_pos < m_src.size()) {
            advance(); // consume }
         }
      } else {
         break;
      }
   }
}

// ============================================================================
//  Token Reading Functions
// ============================================================================

/**
 * @brief Read an identifier or keyword
 *
 * Reads alphanumeric characters and underscores, then checks if the
 * uppercase version matches a keyword in the lookup table.
 *
 * @return Token of type IDENTIFIER or a keyword type
 */
Token Lexer::readIdentifierOrKeyword()
{
   uint32_t startLine = m_line, startCol = m_col;
   std::string originalText;

   while (m_pos < m_src.size() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
      originalText += advance();
   }

   // Create uppercase version for keyword lookup
   std::string upperText = originalText;
   std::transform(upperText.begin(), upperText.end(), upperText.begin(), ::toupper);

   auto it = s_keywords.find(upperText);
   if (it != s_keywords.end()) {
      return Token(it->second, originalText, startLine, startCol);
   }

   return Token(TokenType::IDENTIFIER, originalText, startLine, startCol);
}

/**
 * @brief Read a numeric literal
 *
 * Handles:
 * - Decimal integers (123)
 * - Hex literals (16#FF)
 * - Real numbers (123.456, 1.2e-3)
 *
 * @return Token of type INT_LITERAL or REAL_LITERAL
 */
Token Lexer::readNumber()
{
   uint32_t startLine = m_line, startCol = m_col;
   std::string text;
   bool isReal = false;

   // Hex literal: 16#...
   if (peek() == '1' && peek(1) == '6' && peek(2) == '#') {
      text += advance();
      text += advance();
      text += advance();
      while (m_pos < m_src.size() && std::isxdigit(static_cast<unsigned char>(peek()))) {
         text += advance();
      }
      return Token(TokenType::INT_LITERAL, text, startLine, startCol);
   }

   // Integer part
   while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
      text += advance();
   }

   // Fractional part
   if (peek() == '.' && peek(1) != '.') {
      isReal = true;
      text += advance();
      while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
         text += advance();
      }
   }

   // Exponent
   if (peek() == 'E' || peek() == 'e') {
      isReal = true;
      text += advance();
      if (peek() == '+' || peek() == '-') {
         text += advance();
      }
      while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(peek()))) {
         text += advance();
      }
   }

   return Token(isReal ? TokenType::REAL_LITERAL : TokenType::INT_LITERAL, text, startLine, startCol);
}

/**
 * @brief Read a string literal
 *
 * Handles both single-quoted and double-quoted strings.
 * Escaped characters (starting with $) are preserved.
 *
 * @return Token of type STRING_LITERAL
 */
Token Lexer::readString()
{
   uint32_t startLine = m_line, startCol = m_col;
   char delim = advance(); // ' or "
   std::string text(1, delim);

   while (m_pos < m_src.size() && peek() != delim) {
      if (peek() == '$') {
         text += advance();
      }
      text += advance();
   }

   if (m_pos < m_src.size()) {
      text += advance(); // closing quote
   }

   return Token(TokenType::STRING_LITERAL, text, startLine, startCol);
}

/**
 * @brief Read a time literal (T#..., TIME#...)
 *
 * Time literals have the format: [T | TIME]#[duration]
 *
 * @return Token of type TIME_LITERAL
 */
Token Lexer::readTimeLiteral()
{
   uint32_t startLine = m_line, startCol = m_col;
   std::string text;

   while (m_pos < m_src.size() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '#' || peek() == '_' || peek() == '.')) {
      text += advance();
   }

   return Token(TokenType::TIME_LITERAL, text, startLine, startCol);
}

// ============================================================================
//  Main Tokenization
// ============================================================================

/**
 * @brief Tokenize the entire source code
 *
 * Main entry point for the lexer. Processes the source code character by
 * character, skipping whitespace and comments, and producing a stream of
 * tokens. Ends with an END_OF_FILE token.
 *
 * @return Vector of tokens
 * @throws std::runtime_error on lexing error
 */
std::vector<Token> Lexer::tokenize()
{
   std::vector<Token> tokens;

   while (true) {
      skipWhitespaceAndComments();

      if (m_pos >= m_src.size()) {
         tokens.emplace_back(TokenType::END_OF_FILE, "", m_line, m_col);
         break;
      }

      uint32_t tokLine = m_line, tokCol = m_col;
      char c = peek();

      // Identifier or keyword
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
         Token t = readIdentifierOrKeyword();

         // Detect T#, TIME# time literals
         if ((t.text == "T" || t.text == "t" || t.text == "TIME" || t.text == "time") && peek() == '#') {
            std::string text = t.text;
            text += advance(); // #
            while (m_pos < m_src.size() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '.')) {
               text += advance();
            }
            tokens.emplace_back(TokenType::TIME_LITERAL, text, tokLine, tokCol);
         } else {
            tokens.push_back(std::move(t));
         }
         continue;
      }

      // Number
      if (std::isdigit(static_cast<unsigned char>(c))) {
         tokens.push_back(readNumber());
         continue;
      }

      // String
      if (c == '\'' || c == '"') {
         tokens.push_back(readString());
         continue;
      }

      advance(); // consume the character

      switch (c) {
      case ':':
         if (match('=')) {
            tokens.emplace_back(TokenType::OP_ASSIGN, ":=", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::COLON, ":", tokLine, tokCol);
         }
         break;
      case '<':
         if (match('>')) {
            tokens.emplace_back(TokenType::OP_NEQ, "<>", tokLine, tokCol);
         } else if (match('=')) {
            tokens.emplace_back(TokenType::OP_LE, "<=", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::OP_LT, "<", tokLine, tokCol);
         }
         break;
      case '>':
         if (match('=')) {
            tokens.emplace_back(TokenType::OP_GE, ">=", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::OP_GT, ">", tokLine, tokCol);
         }
         break;
      case '=':
         if (match('>')) {
            tokens.emplace_back(TokenType::OP_ASSIGN_REF, "=>", tokLine, tokCol);
         } else if (match('=')) {
            tokens.emplace_back(TokenType::OP_EQ, "==", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::OP_EQ, "=", tokLine, tokCol);
         }
         break;
      case '+':
         tokens.emplace_back(TokenType::OP_PLUS, "+", tokLine, tokCol);
         break;
      case '-':
         tokens.emplace_back(TokenType::OP_MINUS, "-", tokLine, tokCol);
         break;
      case '*':
         if (match('*')) {
            tokens.emplace_back(TokenType::OP_POWER, "**", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::OP_MUL, "*", tokLine, tokCol);
         }
         break;
      case '/':
         tokens.emplace_back(TokenType::OP_DIV, "/", tokLine, tokCol);
         break;
      case '^':
         tokens.emplace_back(TokenType::OP_DEREF, "^", tokLine, tokCol);
         break;
      case '.':
         if (match('.')) {
            tokens.emplace_back(TokenType::OP_RANGE, "..", tokLine, tokCol);
         } else {
            tokens.emplace_back(TokenType::DOT, ".", tokLine, tokCol);
         }
         break;
      case '(':
         tokens.emplace_back(TokenType::LPAREN, "(", tokLine, tokCol);
         break;
      case ')':
         tokens.emplace_back(TokenType::RPAREN, ")", tokLine, tokCol);
         break;
      case '[':
         tokens.emplace_back(TokenType::LBRACKET, "[", tokLine, tokCol);
         break;
      case ']':
         tokens.emplace_back(TokenType::RBRACKET, "]", tokLine, tokCol);
         break;
      case ';':
         tokens.emplace_back(TokenType::SEMICOLON, ";", tokLine, tokCol);
         break;
      case ',':
         tokens.emplace_back(TokenType::COMMA, ",", tokLine, tokCol);
         break;
      case '{':
         tokens.emplace_back(TokenType::KW_ATTRIBUTE, "{", tokLine, tokCol);
         break;
      case '}':
         tokens.emplace_back(TokenType::KW_END_ATTRIBUTE, "}", tokLine, tokCol);
         break;
      case '@':
         tokens.emplace_back(TokenType::AT, "@", tokLine, tokCol);
         break;
      case '%':
         tokens.emplace_back(TokenType::PERCENT, "%", tokLine, tokCol);
         break;
      default:
         tokens.emplace_back(TokenType::UNKNOWN, std::string(1, c), tokLine, tokCol);
      }
   }

   return tokens;
}