/**
 * @file ParserStmt.cpp
 * @brief Statement and expression parsing helpers
 *
 * This compilation unit contains the implementation of statement-level parsing
 * (IF, FOR, WHILE, CASE, expressions, etc.) and expression parsing using
 * Pratt/precedence climbing.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "parser/Parser.h"
#include <sstream>

// ============================================================================
//  Statement List Parsing
// ============================================================================

/**
 * @brief Parse a list of statements until a stop token is encountered
 * @param stopAt Set of token types that terminate the list
 * @return Vector of statement AST nodes
 */
std::vector<std::shared_ptr<Stmt>> Parser::parseStatementList(std::initializer_list<TokenType> stopAt)
{
   std::vector<std::shared_ptr<Stmt>> stmts;
   while (!atEnd()) {
      for (auto s : stopAt) {
         if (check(s)) {
            return stmts;
         }
      }
      auto stmt = parseStatement();
      if (stmt) {
         stmts.push_back(stmt);
      }
   }
   return stmts;
}

// ============================================================================
//  Single Statement Parsing
// ============================================================================

/**
 * @brief Parse a single statement
 *
 * Handles assignment, function calls, return, exit, and control flow statements.
 *
 * @return Statement AST node (or nullptr for empty)
 */
std::shared_ptr<Stmt> Parser::parseStatement()
{
   uint32_t ln = peek().line;

   // Empty statement
   if (match(TokenType::SEMICOLON)) {
      return std::make_shared<Stmt>(EmptyStmt{}, ln);
   }

   if (check(TokenType::KW_IF)) {
      return parseIfStmt();
   }
   if (check(TokenType::KW_FOR)) {
      return parseForStmt();
   }
   if (check(TokenType::KW_WHILE)) {
      return parseWhileStmt();
   }
   if (check(TokenType::KW_REPEAT)) {
      return parseRepeatStmt();
   }
   if (check(TokenType::KW_CASE)) {
      return parseCaseStmt();
   }

   if (match(TokenType::KW_RETURN)) {
      match(TokenType::SEMICOLON);
      return std::make_shared<Stmt>(ReturnStmt{}, ln);
   }
   if (match(TokenType::KW_EXIT)) {
      match(TokenType::SEMICOLON);
      return std::make_shared<Stmt>(ExitStmt{}, ln);
   }

   // Assignment or function call expression statement
   auto lhs = parseExpr();

   if (match(TokenType::OP_ASSIGN)) {
      auto rhs = parseExpr();
      match(TokenType::SEMICOLON);
      return std::make_shared<Stmt>(AssignStmt{lhs, rhs}, ln);
   }
   // Expression statement (FB call)
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(ExprStmt{lhs}, ln);
}

// ============================================================================
//  Control Flow Statements
// ============================================================================

/**
 * @brief Parse an IF-THEN-ELSIF-ELSE-END_IF statement
 * @return Statement AST node (IfStmt)
 */
std::shared_ptr<Stmt> Parser::parseIfStmt()
{
   uint32_t ln = peek().line;
   expect(TokenType::KW_IF, "Expected IF");
   IfStmt stmt;

   auto cond = parseExpr();
   expect(TokenType::KW_THEN, "Expected THEN");
   auto body = parseStatementList({TokenType::KW_ELSIF, TokenType::KW_ELSE, TokenType::KW_END_IF});
   stmt.branches.push_back({cond, body});

   while (match(TokenType::KW_ELSIF)) {
      auto ec = parseExpr();
      expect(TokenType::KW_THEN, "Expected THEN after ELSIF");
      auto eb = parseStatementList({TokenType::KW_ELSIF, TokenType::KW_ELSE, TokenType::KW_END_IF});
      stmt.branches.push_back({ec, eb});
   }
   if (match(TokenType::KW_ELSE)) {
      auto eb = parseStatementList({TokenType::KW_END_IF});
      stmt.branches.push_back({nullptr, eb});
   }
   expect(TokenType::KW_END_IF, "Expected END_IF");
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(std::move(stmt), ln);
}

/**
 * @brief Parse a FOR loop statement
 * @return Statement AST node (ForStmt)
 */
std::shared_ptr<Stmt> Parser::parseForStmt()
{
   uint32_t ln = peek().line;
   expect(TokenType::KW_FOR, "Expected FOR");
   ForStmt stmt;
   stmt.var = expect(TokenType::IDENTIFIER, "Expected loop variable").text;
   expect(TokenType::OP_ASSIGN, "Expected ':='");
   stmt.from = parseExpr();
   expect(TokenType::KW_TO, "Expected TO");
   stmt.to = parseExpr();
   if (match(TokenType::KW_BY)) {
      stmt.by = parseExpr();
   }
   expect(TokenType::KW_DO, "Expected DO");
   stmt.body = parseStatementList({TokenType::KW_END_FOR});
   expect(TokenType::KW_END_FOR, "Expected END_FOR");
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(std::move(stmt), ln);
}

/**
 * @brief Parse a WHILE loop statement
 * @return Statement AST node (WhileStmt)
 */
std::shared_ptr<Stmt> Parser::parseWhileStmt()
{
   uint32_t ln = peek().line;
   expect(TokenType::KW_WHILE, "Expected WHILE");
   WhileStmt stmt;
   stmt.condition = parseExpr();
   expect(TokenType::KW_DO, "Expected DO");
   stmt.body = parseStatementList({TokenType::KW_END_WHILE});
   expect(TokenType::KW_END_WHILE, "Expected END_WHILE");
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(std::move(stmt), ln);
}

/**
 * @brief Parse a REPEAT-UNTIL loop statement
 * @return Statement AST node (RepeatStmt)
 */
std::shared_ptr<Stmt> Parser::parseRepeatStmt()
{
   uint32_t ln = peek().line;
   expect(TokenType::KW_REPEAT, "Expected REPEAT");
   RepeatStmt stmt;
   stmt.body = parseStatementList({TokenType::KW_UNTIL});
   expect(TokenType::KW_UNTIL, "Expected UNTIL");
   stmt.condition = parseExpr();
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(std::move(stmt), ln);
}

/**
 * @brief Parse a CASE statement
 *
 * Handles multiple case branches with value lists and ranges.
 * Range cases are supported but noted in the AST for later handling.
 *
 * @return Statement AST node (CaseStmt)
 */
std::shared_ptr<Stmt> Parser::parseCaseStmt()
{
   uint32_t ln = peek().line;
   expect(TokenType::KW_CASE, "Expected CASE");
   CaseStmt stmt;
   stmt.selector = parseExpr();
   expect(TokenType::KW_OF, "Expected OF");

   while (!check(TokenType::KW_END_CASE) && !check(TokenType::KW_ELSE) && !atEnd()) {
      CaseBranch branch;
      // Parse value list / ranges
      do {
         CaseValue cv;
         cv.low = parseExpr();
         if (match(TokenType::OP_RANGE)) {
            cv.high = parseExpr();
         }
         branch.values.push_back(std::move(cv));
      } while (match(TokenType::COMMA));

      expect(TokenType::COLON, "Expected ':' after CASE values");

      // Parse statements for this branch
      while (!atEnd() && !check(TokenType::KW_END_CASE) && !check(TokenType::KW_ELSE) && !isCaseLabelAhead()) {
         auto stmtPtr = parseStatement();
         if (stmtPtr) {
            branch.body.push_back(stmtPtr);
         }
      }

      stmt.branches.push_back(std::move(branch));
   }
   if (match(TokenType::KW_ELSE)) {
      CaseBranch elseBranch;
      elseBranch.body = parseStatementList({TokenType::KW_END_CASE});
      stmt.branches.push_back(std::move(elseBranch));
   }
   expect(TokenType::KW_END_CASE, "Expected END_CASE");
   match(TokenType::SEMICOLON);
   return std::make_shared<Stmt>(std::move(stmt), ln);
}

/**
 * @brief Look ahead to determine if we're at a CASE label
 *
 * Checks if a colon (:) appears before any statement terminator,
 * indicating a new CASE label rather than a statement continuation.
 *
 * @return true if a colon is found before a semicolon or assignment
 */
bool Parser::isCaseLabelAhead()
{
   int i = 0;
   while (true) {
      TokenType t = peek(i).type;
      if (t == TokenType::END_OF_FILE) {
         break;
      }
      // If we find a colon before a statement end, it's a CASE label
      if (t == TokenType::COLON) {
         return true;
      }
      // If we encounter a statement end or assignment, it's not a label
      if (t == TokenType::SEMICOLON || t == TokenType::OP_ASSIGN) {
         return false;
      }
      // Control structures that would terminate the branch
      if (t == TokenType::KW_END_CASE || t == TokenType::KW_ELSE) {
         return false;
      }
      i++;
   }
   return false;
}

// ============================================================================
//  Expression Parsing (Pratt / Precedence Climbing)
// ============================================================================

/**
 * @brief Get binary operator precedence level
 *
 * Higher precedence = tighter binding.
 *
 * @param t Token type of the operator
 * @return Precedence level, or -1 if not a binary operator
 */
int Parser::getBinaryPrec(TokenType t)
{
   switch (t) {
   case TokenType::KW_OR:
      return 1;
   case TokenType::KW_XOR:
      return 2;
   case TokenType::KW_AND:
      return 3;
   case TokenType::OP_EQ:
   case TokenType::OP_NEQ:
   case TokenType::OP_LT:
   case TokenType::OP_LE:
   case TokenType::OP_GT:
   case TokenType::OP_GE:
      return 4;
   case TokenType::OP_PLUS:
   case TokenType::OP_MINUS:
      return 5;
   case TokenType::OP_MUL:
   case TokenType::OP_DIV:
   case TokenType::KW_MOD:
      return 6;
   case TokenType::OP_POWER:
      return 7;
   default:
      return -1;
   }
}

/**
 * @brief Parse an expression with Pratt precedence climbing
 * @param minPrec Minimum precedence to accept (default 0)
 * @return Expression AST node
 */
std::shared_ptr<Expr> Parser::parseExpr(int minPrec)
{
   auto left = parseUnary();
   while (true) {
      int prec = getBinaryPrec(peek().type);
      if (prec < minPrec) {
         break;
      }
      std::string op = peek().text;
      uint32_t ln = peek().line;
      advance();
      auto right = parseExpr(prec + 1);
      left = std::make_shared<Expr>(BinaryExpr{op, left, right}, ln);
   }
   return left;
}

/**
 * @brief Parse a unary expression (NOT, -, +)
 * @return Expression AST node
 */
std::shared_ptr<Expr> Parser::parseUnary()
{
   uint32_t ln = peek().line;
   if (check(TokenType::KW_NOT)) {
      advance();
      return std::make_shared<Expr>(UnaryExpr{"NOT", parseUnary()}, ln);
   }
   if (check(TokenType::OP_MINUS)) {
      advance();
      return std::make_shared<Expr>(UnaryExpr{"-", parseUnary()}, ln);
   }
   if (check(TokenType::OP_PLUS)) {
      advance();
      return parseUnary();
   }
   return parsePrimary();
}

/**
 * @brief Parse a primary expression
 *
 * Handles literals, ADR(), SIZEOF(), parenthesized expressions,
 * and identifiers (with postfix processing).
 *
 * @return Expression AST node
 */
std::shared_ptr<Expr> Parser::parsePrimary()
{
   uint32_t ln = peek().line;

   // TRUE / FALSE
   if (match(TokenType::KW_TRUE)) {
      return std::make_shared<Expr>(BoolLitExpr{true}, ln);
   }
   if (match(TokenType::KW_FALSE)) {
      return std::make_shared<Expr>(BoolLitExpr{false}, ln);
   }

   // ADR(expr)
   if (match(TokenType::KW_ADR)) {
      expect(TokenType::LPAREN, "Expected '(' after ADR");
      auto op = parseExpr();
      expect(TokenType::RPAREN, "Expected ')'");
      return std::make_shared<Expr>(AdrExpr{op}, ln);
   }

   // SIZEOF(type)
   if (match(TokenType::KW_SIZEOF)) {
      expect(TokenType::LPAREN, "Expected '(' after SIZEOF");
      auto ty = parseTypeRef();
      expect(TokenType::RPAREN, "Expected ')'");
      return std::make_shared<Expr>(SizeofExpr{ty}, ln);
   }

   // Literals
   if (check(TokenType::INT_LITERAL) || check(TokenType::REAL_LITERAL)) {
      auto tok = advance();
      return std::make_shared<Expr>(LiteralExpr{tok.text, ""}, ln);
   }
   if (check(TokenType::STRING_LITERAL)) {
      auto tok = advance();
      return std::make_shared<Expr>(LiteralExpr{tok.text, ""}, ln);
   }
   if (check(TokenType::TIME_LITERAL)) {
      auto tok = advance();
      return std::make_shared<Expr>(LiteralExpr{tok.text, "TIME"}, ln);
   }

   // Parenthesized expression
   if (match(TokenType::LPAREN)) {
      auto e = parseExpr();
      expect(TokenType::RPAREN, "Expected ')'");
      return e;
   }

   // Identifier - consume it and let parsePostfix handle postfix operators
   if (check(TokenType::IDENTIFIER) || isTypeKeyword(peek().type)) {
      std::string name = peek().text;
      advance();
      auto expr = std::make_shared<Expr>(IdentExpr{name}, ln);
      return parsePostfix(expr);
   }

   throw error("Expected expression");
}

// ============================================================================
//  Postfix Operators
// ============================================================================

/**
 * @brief Parse postfix operators (., [, ^, ())
 *
 * Handles member access, array indexing, pointer dereference, and
 * function/method calls. This is called repeatedly to chain postfix
 * operations (e.g., a.b.c[3].d()).
 *
 * @param base The base expression to attach postfix operators to
 * @return Expression AST node with postfix operators applied
 */
std::shared_ptr<Expr> Parser::parsePostfix(std::shared_ptr<Expr> base)
{
   uint32_t ln = peek().line;
   while (true) {
      if (match(TokenType::DOT)) {
         std::string member = expect(TokenType::IDENTIFIER, "Expected member name").text;
         base = std::make_shared<Expr>(MemberExpr{base, member}, ln);
      } else if (match(TokenType::LBRACKET)) {
         std::vector<std::shared_ptr<Expr>> indices;
         indices.push_back(parseExpr());
         while (match(TokenType::COMMA)) {
            indices.push_back(parseExpr());
         }
         expect(TokenType::RBRACKET, "Expected ']'");
         base = std::make_shared<Expr>(IndexExpr{base, std::move(indices)}, ln);
      } else if (match(TokenType::OP_DEREF)) {
         base = std::make_shared<Expr>(DerefExpr{base}, ln);
      } else if (match(TokenType::LPAREN)) {
         CallExpr call;
         call.callee = base;

         if (!check(TokenType::RPAREN)) {
            call.args.push_back(parseCallArg());
            while (match(TokenType::COMMA)) {
               call.args.push_back(parseCallArg());
            }
         }

         expect(TokenType::RPAREN, "Expected ')' after arguments");
         base = std::make_shared<Expr>(std::move(call), ln);
      } else {
         break;
      }
   }
   return base;
}

/**
 * @brief Parse a single function/method call argument
 *
 * Handles:
 * - Positional arguments: expr
 * - Named input arguments: name := expr
 * - Named output bindings: name => expr (for FB calls)
 *
 * @return CallExpr::Arg structure with name, value, and flags
 */
CallExpr::Arg Parser::parseCallArg()
{
   CallExpr::Arg arg;
   uint32_t ln = peek().line;
   uint32_t cl = peek().col;

   // Check for named output binding: IDENT => expr
   if (check(TokenType::IDENTIFIER) && peek(1).type == TokenType::OP_ASSIGN_REF) {
      arg.name = advance().text;
      advance(); // consume '=>'
      arg.value = parseExpr();
      arg.named = true;
      arg.isOutput = true;
      arg.line = ln;
      arg.col = cl;
   }
   // Check for named input argument: IDENT := expr
   else if (check(TokenType::IDENTIFIER) && peek(1).type == TokenType::OP_ASSIGN) {
      arg.name = advance().text;
      advance(); // consume ':='
      arg.value = parseExpr();
      arg.named = true;
      arg.isOutput = false;
      arg.line = ln;
      arg.col = cl;
   } else {
      arg.value = parseExpr();
      arg.named = false;
      arg.isOutput = false;
      arg.line = ln;
      arg.col = cl;
   }

   return arg;
}