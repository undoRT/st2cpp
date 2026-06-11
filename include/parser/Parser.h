/**
 * @file Parser.h
 * @brief Recursive-descent parser for Structured Text (ST) tokens
 *
 * The `Parser` class consumes a stream of `Token` objects produced by the
 * `Lexer` and builds an in-memory `TranslationUnit` AST representation defined
 * in `ast/AST.h`.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 Salvatore Bamundo
 */

#pragma once
#include "lexer/Token.h"
#include "ast/AST.h"
#include <stdexcept>

// ============================================================================
//  Parse Error Exception
// ============================================================================

/**
 * @brief Exception thrown for parsing errors
 *
 * Contains line and column information for error reporting.
 */
class ParseError : public std::runtime_error
{
public:
   uint32_t line, col;
   ParseError(const std::string& msg, uint32_t ln, uint32_t c) : std::runtime_error(msg), line(ln), col(c) {}
};

// ============================================================================
//  Parser Class
// ============================================================================

/**
 * @brief Recursive-descent parser for Structured Text
 *
 * Consumes tokens from the lexer and builds an Abstract Syntax Tree (AST)
 * representing the complete translation unit.
 */
class Parser
{
public:
   explicit Parser(std::vector<Token> tokens);

   TranslationUnit parseTranslationUnit();

private:
   // ========================================================================
   //  Member Variables
   // ========================================================================

   std::vector<Token> m_tokens; ///< Token stream to parse
   size_t m_pos = 0;            ///< Current parsing position

   // ========================================================================
   //  Token Navigation Helpers
   // ========================================================================

   const Token& peek(int offset = 0) const;
   const Token& advance();
   bool check(TokenType t) const;
   bool atEnd() const;
   bool match(TokenType t);

   const Token& expect(TokenType t, const std::string& msg);
   ParseError error(const std::string& msg) const;

   // ========================================================================
   //  Top Level Parsing
   // ========================================================================

   POU parsePOU();
   StructType parseStructType();
   EnumType parseEnumType();
   Method parseMethod();
   VarSection parseMethodVarSection(VarKind kind);

   // ========================================================================
   //  Variable Section Parsing
   // ========================================================================

   VarSection parseVarSection(VarKind kind);
   VarSection parseGlobalVarSection();
   Attribute parseAttribute();
   std::vector<Attribute> parseAttributes();

   // ========================================================================
   //  Type Reference Parsing
   // ========================================================================

   TypeRef parseTypeRef();
   TypeRef parseBaseTypeRef();
   std::shared_ptr<Expr> parseArrayInitializer();

   // ========================================================================
   //  Statement Parsing
   // ========================================================================

   std::shared_ptr<Stmt> parseStatement();
   std::vector<std::shared_ptr<Stmt>> parseStatementList(std::initializer_list<TokenType> stopAt);
   std::shared_ptr<Stmt> parseIfStmt();
   std::shared_ptr<Stmt> parseForStmt();
   std::shared_ptr<Stmt> parseWhileStmt();
   std::shared_ptr<Stmt> parseRepeatStmt();
   std::shared_ptr<Stmt> parseCaseStmt();

   // ========================================================================
   //  Expression Parsing (Pratt / Precedence Climbing)
   // ========================================================================

   std::shared_ptr<Expr> parseExpr(int minPrec = 0);
   std::shared_ptr<Expr> parseUnary();
   std::shared_ptr<Expr> parsePrimary();
   std::shared_ptr<Expr> parsePostfix(std::shared_ptr<Expr> base);
   CallExpr::Arg parseCallArg();

   // ========================================================================
   //  Helper Functions
   // ========================================================================

   static int getBinaryPrec(TokenType t);
   static bool isTypeKeyword(TokenType t);
   bool isVarSectionStart() const;
   bool isCaseLabelAhead();
};