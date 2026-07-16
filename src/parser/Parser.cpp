/**
 * @file Parser.cpp
 * @brief Parser implementation for Structured Text
 *
 * Implements the parsing routines declared in `Parser.h`. Uses a simple
 * recursive-descent / precedence climbing approach to build the AST.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "parser/Parser.h"
#include <algorithm>
#include <sstream>

std::vector<Interface> Parser::s_parsedInterfaces;

// ============================================================================
//  Constructor
// ============================================================================

/**
 * @brief Construct a parser with a token stream
 * @param tokens Vector of tokens from the lexer
 */
Parser::Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {}

// ============================================================================
//  Token Navigation Helpers
// ============================================================================

/**
 * @brief Peek ahead at a token without consuming it
 * @param offset Number of tokens to look ahead (0 = current)
 * @return Reference to the token at the specified offset
 */
const Token& Parser::peek(int offset) const
{
   size_t idx = m_pos + offset;
   if (idx >= m_tokens.size()) {
      return m_tokens.back(); // EOF
   }
   return m_tokens[idx];
}

/**
 * @brief Consume the current token and move to the next
 * @return Reference to the consumed token
 */
const Token& Parser::advance()
{
   if (!atEnd()) {
      ++m_pos;
   }
   return m_tokens[m_pos - 1];
}

/**
 * @brief Check if the current token is of a given type
 * @param t Token type to check
 * @return true if current token matches the type
 */
bool Parser::check(TokenType t) const
{
   return peek().type == t;
}

/**
 * @brief Check if end of file has been reached
 * @return true if at EOF
 */
bool Parser::atEnd() const
{
   return peek().type == TokenType::END_OF_FILE;
}

/**
  * @brief If current token matches, consume it and return true
  * @param t Token type to match
  * @return true if token was matched and consumed
  */
bool Parser::match(TokenType t)
{
   if (check(t)) {
      advance();
      return true;
   }
   return false;
}

/**
 * @brief Expect a specific token type, consume it, or throw error
 * @param t Expected token type
 * @param msg Error message if token not found
 * @return Reference to the consumed token
 */
const Token& Parser::expect(TokenType t, const std::string& msg)
{
   if (!check(t)) {
      throw error(msg);
   }
   return advance();
}

/**
 * @brief Create a parse error with current location information
 * @param msg Error description
 * @return ParseError exception
 */
ParseError Parser::error(const std::string& msg) const
{
   const auto& tok = peek();
   std::ostringstream oss;
   oss << "Parse error at line " << tok.line << ":" << tok.col << " ('" << tok.text << "'): " << msg;
   return ParseError(oss.str(), tok.line, tok.col);
}

// ============================================================================
//  Top Level Parsing
// ============================================================================

/**
 * @brief Parse a complete translation unit
 *
 * Parses global variable lists (GVLs), TYPE declarations (STRUCT/ENUM),
 * and Program Organization Units (functions, FBs, programs).
 *
 * @return TranslationUnit AST containing all parsed elements
 */
TranslationUnit Parser::parseTranslationUnit()
{
   TranslationUnit tu;
   while (!atEnd()) {
      // Interface check
      if (check(TokenType::KW_INTERFACE)) {
         tu.interfaces.push_back(parseInterface());
         continue;
      }
      // Check for GVL with attributes then
      else if (check(TokenType::KW_ATTRIBUTE) || check(TokenType::KW_VAR_GLOBAL)) {
         tu.globals.push_back(parseGlobalVarSection());
         continue;
      } else if (check(TokenType::KW_TYPE)) {
         advance(); // consume TYPE

         // Save current position to backtrack if needed
         size_t savedPos = m_pos;

         // Read type name
         if (!check(TokenType::IDENTIFIER)) {
            throw error("Expected type name after TYPE");
         }
         std::string typeName = peek().text;
         advance(); // consume type name

         // Look ahead to determine if it's a STRUCT or ENUM
         if (check(TokenType::COLON)) {
            advance(); // consume ':'
            if (check(TokenType::KW_STRUCT)) {
               // It is a struct - backtrack and parse as struct
               m_pos = savedPos;
               tu.structs.push_back(parseStructType());
            } else if (check(TokenType::LPAREN)) {
               // It is an ENUM - backtrack and parse as enum
               m_pos = savedPos;
               tu.enums.push_back(parseEnumType());
            } else {
               throw error("Expected STRUCT or '(' after TYPE name");
            }
         } else {
            throw error("Expected ':' after type name");
         }
      } else {
         tu.pous.push_back(parsePOU());
      }
   }
   return tu;
}

/**
 * @brief Parse a Program Organization Unit (POU)
 *
 * Handles FUNCTION_BLOCK, FUNCTION, and PROGRAM declarations.
 * Parses the POU name, return type (for functions), variable sections,
 * methods (for FBs), and the statement body.
 *
 * @return POU AST node
 */
POU Parser::parsePOU()
{
   POU pou;
   pou.line = peek().line;

   if (match(TokenType::KW_FUNCTION_BLOCK)) {
      pou.kind = POUKind::FUNCTION_BLOCK;
   } else if (match(TokenType::KW_FUNCTION)) {
      pou.kind = POUKind::FUNCTION;
   } else if (match(TokenType::KW_PROGRAM)) {
      pou.kind = POUKind::PROGRAM;
   } else {
      throw error("Expected FUNCTION_BLOCK, FUNCTION, or PROGRAM");
   }

   // ABSTRACT FUNCTION_BLOCK
   if (match(TokenType::KW_ABSTRACT)) {
      pou.isAbstract = true;
   }

   // FINAL after FUNCTION_BLOCK
   if (match(TokenType::KW_FINAL)) {
      pou.isFinal = true;
   }

   pou.name = expect(TokenType::IDENTIFIER, "Expected POU name").text;

   // Parse EXTENDS
   if (match(TokenType::KW_EXTENDS)) {
      pou.extends = expect(TokenType::IDENTIFIER, "Expected base POU name").text;
   }

   // Parse IMPLEMENTS (Interface)
   if (match(TokenType::KW_IMPLEMENTS)) {
      do {
         pou.implements.push_back(expect(TokenType::IDENTIFIER, "Expected interface name").text);
      } while (match(TokenType::COMMA));
   }

   // Return type for FUNCTION: FUNCTION Name : TYPE
   if (pou.kind == POUKind::FUNCTION && match(TokenType::COLON)) {
      pou.returnType = parseTypeRef();
   }

   // Parse variable sections
   while (isVarSectionStart()) {
      VarKind kind;
      if (match(TokenType::KW_VAR)) {
         kind = VarKind::VAR;
      } else if (match(TokenType::KW_VAR_INPUT)) {
         kind = VarKind::INPUT;
      } else if (match(TokenType::KW_VAR_OUTPUT)) {
         kind = VarKind::OUTPUT;
      } else if (match(TokenType::KW_VAR_IN_OUT)) {
         kind = VarKind::IN_OUT;
      } else if (match(TokenType::KW_VAR_EXTERNAL)) {
         kind = VarKind::EXTERNAL;
      } else if (match(TokenType::KW_VAR_GLOBAL)) {
         kind = VarKind::GLOBAL;
      } else if (match(TokenType::KW_VAR_TEMP)) {
         kind = VarKind::TEMP;
      } else {
         break;
      }
      pou.varSections.push_back(parseVarSection(kind));
   }

   // Parse methods (only for FUNCTION_BLOCK)
   if (pou.kind == POUKind::FUNCTION_BLOCK) {
      while (check(TokenType::KW_METHOD)) {
         pou.methods.push_back(parseMethod());
      }
   }

   // Determine the END token for this POU
   TokenType endTok{};
   switch (pou.kind) {
   case POUKind::FUNCTION_BLOCK:
      endTok = TokenType::KW_END_FUNCTION_BLOCK;
      break;
   case POUKind::FUNCTION:
      endTok = TokenType::KW_END_FUNCTION;
      break;
   case POUKind::PROGRAM:
      endTok = TokenType::KW_END_PROGRAM;
      break;
   }

   pou.body = parseStatementList({endTok});
   expect(endTok, "Expected END keyword for POU");
   return pou;
}

/**
 * @brief Parse a STRUCT type declaration
 *
 * Syntax:
 *   TYPE name :
 *   STRUCT
 *     member1, member2 : TYPE [:= initial];
 *   END_STRUCT
 *   END_TYPE
 *
 * @return StructType AST node
 */
StructType Parser::parseStructType()
{
   StructType st;
   st.name = expect(TokenType::IDENTIFIER, "Expected type name after TYPE").text;

   expect(TokenType::COLON, "Expected ':' after type name");
   expect(TokenType::KW_STRUCT, "Expected STRUCT");

   while (!check(TokenType::KW_END_STRUCT) && !atEnd()) {
      std::vector<std::string> names;
      names.push_back(expect(TokenType::IDENTIFIER, "Expected member name").text);
      while (match(TokenType::COMMA)) {
         names.push_back(expect(TokenType::IDENTIFIER, "Expected member name").text);
      }

      expect(TokenType::COLON, "Expected ':' after member name(s)");
      TypeRef ty = parseTypeRef();

      std::shared_ptr<Expr> init;
      if (match(TokenType::OP_ASSIGN)) {
         init = parseExpr();
      }

      expect(TokenType::SEMICOLON, "Expected ';' after member declaration");

      for (const auto& name : names) {
         st.members.push_back({name, ty, init});
      }
   }

   expect(TokenType::KW_END_STRUCT, "Expected END_STRUCT");
   match(TokenType::SEMICOLON); // Optional semicolon

   expect(TokenType::KW_END_TYPE, "Expected END_TYPE");
   match(TokenType::SEMICOLON); // Optional semicolon

   return st;
}

/**
 * @brief Parse an ENUM type declaration
 *
 * Syntax:
 *   TYPE name :
 *     ( enumerator1 [:= value], enumerator2, ... )
 *   END_TYPE
 *
 * @return EnumType AST node
 */
EnumType Parser::parseEnumType()
{
   EnumType et;
   et.name = expect(TokenType::IDENTIFIER, "Expected type name after TYPE").text;

   expect(TokenType::COLON, "Expected ':' after type name");
   expect(TokenType::LPAREN, "Expected '(' before enumerator list");

   if (!check(TokenType::RPAREN)) {
      do {
         EnumEnumerator enumerator;
         enumerator.name = expect(TokenType::IDENTIFIER, "Expected enumerator name").text;
         if (match(TokenType::OP_ASSIGN)) {
            enumerator.value = parseExpr();
         }
         et.enumerators.push_back(enumerator);
      } while (match(TokenType::COMMA));
   }

   expect(TokenType::RPAREN, "Expected ')' after enumerator list");
   match(TokenType::SEMICOLON); // Optional semicolon
   expect(TokenType::KW_END_TYPE, "Expected END_TYPE");
   match(TokenType::SEMICOLON); // Optional semicolon

   return et;
}

/**
 * @brief Parse a METHOD declaration inside a FUNCTION_BLOCK
 *
 * Syntax:
 *   METHOD <name> [ : <return_type> ]
 *     VAR_INPUT ... END_VAR
 *     VAR_OUTPUT ... END_VAR
 *     VAR ... END_VAR
 *     VAR_TEMP ... END_VAR
 *     <statements>
 *   END_METHOD
 *
 * @return Method AST node
 */
Method Parser::parseMethod()
{
   Method method;
   method.line = peek().line;

   expect(TokenType::KW_METHOD, "Expected METHOD");

   // PRIVATE, PROTECTED, PUBLIC
   if (match(TokenType::KW_PRIVATE)) {
      method.visibility = MethodVisibility::PRIVATE;
   } else if (match(TokenType::KW_PROTECTED)) {
      method.visibility = MethodVisibility::PROTECTED;
   } else if (match(TokenType::KW_PUBLIC)) {
      method.visibility = MethodVisibility::PUBLIC;
   } else {
      // Default: PUBLIC
      method.visibility = MethodVisibility::PUBLIC;
   }

   // ABSTRACT METHOD
   if (match(TokenType::KW_ABSTRACT)) {
      method.isAbstract = true;
   }

   // FINAL METHOD
   if (match(TokenType::KW_FINAL)) {
      method.isFinal = true;
   }

   // OVERRIDE
   if (match(TokenType::KW_OVERRIDE)) {
      method.isOverride = true;
   }

   method.name = expect(TokenType::IDENTIFIER, "Expected method name").text;

   // Optional return type
   if (match(TokenType::COLON)) {
      method.returnType = parseTypeRef();
   } else {
      method.returnType.base = BaseType::VOID;
   }

   // Parse variable sections and statements until END_METHOD
   while (!check(TokenType::KW_END_METHOD) && !atEnd()) {
      if (check(TokenType::KW_VAR_INPUT)) {
         advance();
         auto section = parseMethodVarSection(VarKind::INPUT);
         for (const auto& decl : section.decls) {
            MethodParameter param;
            param.name = decl.name;
            param.type = decl.type;
            param.kind = VarKind::INPUT;
            param.initialValue = decl.initialValue;
            method.parameters.push_back(param);
         }
      } else if (check(TokenType::KW_VAR_OUTPUT)) {
         advance();
         auto section = parseMethodVarSection(VarKind::OUTPUT);
         for (const auto& decl : section.decls) {
            MethodParameter param;
            param.name = decl.name;
            param.type = decl.type;
            param.kind = VarKind::OUTPUT;
            param.initialValue = decl.initialValue;
            method.parameters.push_back(param);
         }
      } else if (check(TokenType::KW_VAR_IN_OUT)) {
         advance();
         auto section = parseMethodVarSection(VarKind::IN_OUT);
         for (const auto& decl : section.decls) {
            MethodParameter param;
            param.name = decl.name;
            param.type = decl.type;
            param.kind = VarKind::IN_OUT;
            param.initialValue = decl.initialValue;
            method.parameters.push_back(param);
         }
      } else if (check(TokenType::KW_VAR) || check(TokenType::KW_VAR_TEMP)) {
         VarKind kind = check(TokenType::KW_VAR) ? VarKind::VAR : VarKind::TEMP;
         advance();
         auto section = parseMethodVarSection(kind);
         for (const auto& decl : section.decls) {
            VarDecl local;
            local.name = decl.name;
            local.type = decl.type;
            local.initialValue = decl.initialValue;
            method.localVars.push_back(local);
         }
      } else {
         // Parse statement
         auto stmt = parseStatement();
         if (stmt) {
            method.body.push_back(stmt);
         }
      }
   }

   expect(TokenType::KW_END_METHOD, "Expected END_METHOD");
   match(TokenType::SEMICOLON);

   return method;
}

/**
 * @brief Parse interface type for function blocks
 * 
 * @return Interface struct found
 */
Interface Parser::parseInterface()
{
   Interface iface;
   iface.line = peek().line;

   expect(TokenType::KW_INTERFACE, "Expected INTERFACE");
   iface.name = expect(TokenType::IDENTIFIER, "Expected interface name").text;

   // Parse methods (without implementations)
   while (!check(TokenType::KW_END_INTERFACE) && !atEnd()) {
      if (check(TokenType::KW_METHOD)) {
         Method method = parseMethod();
         // For interfaces, methods are abstract by default
         method.isAbstract = true;
         iface.methods.push_back(method);
      } else {
         throw error("Expected METHOD in INTERFACE");
      }
   }

   expect(TokenType::KW_END_INTERFACE, "Expected END_INTERFACE");
   match(TokenType::SEMICOLON);
   s_parsedInterfaces.push_back(iface);
   return iface;
}

/**
 * @brief Parse a variable section inside a METHOD
 *
 * Similar to parseVarSection but without AT address support.
 *
 * @param kind The kind of variable section
 * @return VarSection AST node
 */
VarSection Parser::parseMethodVarSection(VarKind kind)
{
   VarSection sec;
   sec.kind = kind;

   match(TokenType::KW_CONSTANT);
   match(TokenType::KW_RETAIN);

   while (!check(TokenType::KW_END_VAR) && !atEnd()) {
      std::vector<std::string> names;
      names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      while (match(TokenType::COMMA)) {
         names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      }

      expect(TokenType::COLON, "Expected ':' after variable name(s)");
      TypeRef ty = parseTypeRef();

      std::shared_ptr<Expr> init;
      if (match(TokenType::OP_ASSIGN)) {
         init = parseExpr();
      }
      expect(TokenType::SEMICOLON, "Expected ';' after variable declaration");

      for (auto& nm : names) {
         VarDecl d;
         d.name = nm;
         d.type = ty;
         d.initialValue = init;
         sec.decls.push_back(std::move(d));
      }
   }

   expect(TokenType::KW_END_VAR, "Expected END_VAR");
   return sec;
}

/**
 * @brief Parse a struct initialization body: (member := value, ...)
 * @return StructInitExpr AST node
 */
StructInitExpr Parser::parseStructInitBody()
{
   StructInitExpr init;
   while (!check(TokenType::RPAREN) && !atEnd()) {
      if (!check(TokenType::IDENTIFIER)) {
         throw error("Expected member name in struct initialization");
      }
      StructInitExpr::MemberInit member;
      member.member = advance().text;
      expect(TokenType::OP_ASSIGN, "Expected ':=' after member name");
      member.value = parseExpr(); // può essere un'altra struct init o un'espressione normale
      init.members.push_back(std::move(member));
      if (!check(TokenType::RPAREN)) {
         expect(TokenType::COMMA, "Expected ',' between struct initializers");
      }
   }
   expect(TokenType::RPAREN, "Expected ')' after struct initialization");
   return init;
}

// ============================================================================
//  Variable Section Parsing
// ============================================================================

/**
 * @brief Check if the current position starts a variable section
 * @return true if looking at VAR, VAR_INPUT, VAR_OUTPUT, etc.
 */
bool Parser::isVarSectionStart() const
{
   TokenType t = peek().type;
   return t == TokenType::KW_VAR || t == TokenType::KW_VAR_INPUT || t == TokenType::KW_VAR_OUTPUT || t == TokenType::KW_VAR_IN_OUT
          || t == TokenType::KW_VAR_EXTERNAL || t == TokenType::KW_VAR_GLOBAL || t == TokenType::KW_VAR_TEMP;
}

/**
 * @brief Parse a standard variable section
 *
 * Syntax:
 *   VAR [CONSTANT] [RETAIN]
 *     name {, name} : type [:= initial] ;
 *   END_VAR
 *
 * @param kind The kind of variable section
 * @return VarSection AST node
 */
VarSection Parser::parseVarSection(VarKind kind)
{
   VarSection sec;
   sec.kind = kind;

   // Consume optional CONSTANT / RETAIN modifier
   match(TokenType::KW_CONSTANT);
   match(TokenType::KW_RETAIN);

   while (!check(TokenType::KW_END_VAR) && !atEnd()) {
      // Parse one or more names
      std::vector<std::string> names;
      names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      while (match(TokenType::COMMA)) {
         names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      }

      // AT Address
      std::string atAddr;
      if (match(TokenType::KW_AT)) {
         // Read address (eg %IX0.0, %QW2, %MW10)
         if (check(TokenType::ADDRESS_INPUT) || check(TokenType::ADDRESS_OUTPUT) || check(TokenType::ADDRESS_MARKER)
             || check(TokenType::ADDRESS_TEMP) || check(TokenType::ADDRESS_DIRECT)) {
            atAddr = advance().text;
         } else {
            // Read token by token until ":"
            while (!check(TokenType::COLON) && !atEnd()) {
               atAddr += peek().text;
               advance();
            }
         }
      }

      expect(TokenType::COLON, "Expected ':' after variable name(s)");
      TypeRef ty = parseTypeRef();

      std::shared_ptr<Expr> init;
      if (match(TokenType::OP_ASSIGN)) {
         if (check(TokenType::LBRACKET)) {
            init = parseArrayInitializer();
         } else {
            init = parseExpr();
         }
      }
      expect(TokenType::SEMICOLON, "Expected ';' after variable declaration");

      for (auto& nm : names) {
         VarDecl d;
         d.name = nm;
         d.type = ty;
         d.initialValue = init;
         d.atAddress = atAddr;
         sec.decls.push_back(std::move(d));
      }
   }
   expect(TokenType::KW_END_VAR, "Expected END_VAR");
   return sec;
}

/**
 * @brief Parse a GLOBAL variable section (GVL)
 *
 * Supports optional attributes before VAR_GLOBAL.
 *
 * @return VarSection AST node with kind = GLOBAL
 */
VarSection Parser::parseGlobalVarSection()
{
   VarSection sec;
   sec.kind = VarKind::GLOBAL;

   // Parse attributes
   sec.attributes = parseAttributes();

   expect(TokenType::KW_VAR_GLOBAL, "Expected VAR_GLOBAL");

   // Optional modifiers
   match(TokenType::KW_CONSTANT);
   match(TokenType::KW_RETAIN);

   while (!check(TokenType::KW_END_VAR) && !atEnd()) {
      // Parse variable names
      std::vector<std::string> names;
      names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      while (match(TokenType::COMMA)) {
         names.push_back(expect(TokenType::IDENTIFIER, "Expected variable name").text);
      }

      // AT Address
      std::string atAddr;
      if (match(TokenType::KW_AT)) {
         // Address can be a token of type ADDRESS_*
         if (check(TokenType::ADDRESS_INPUT) || check(TokenType::ADDRESS_OUTPUT) || check(TokenType::ADDRESS_MARKER)
             || check(TokenType::ADDRESS_TEMP) || check(TokenType::ADDRESS_DIRECT)) {
            atAddr = advance().text;
         } else {
            // Fallback: consume token until ":"
            while (!check(TokenType::COLON) && !atEnd()) {
               atAddr += peek().text;
               advance();
            }
         }
      }

      expect(TokenType::COLON, "Expected ':' after variable name(s)");
      TypeRef ty = parseTypeRef();

      std::shared_ptr<Expr> init;
      if (match(TokenType::OP_ASSIGN)) {
         if (check(TokenType::LBRACKET)) {
            init = parseArrayInitializer();
         } else {
            init = parseExpr();
         }
      }
      expect(TokenType::SEMICOLON, "Expected ';' after variable declaration");

      for (auto& nm : names) {
         VarDecl d;
         d.name = nm;
         d.type = ty;
         d.initialValue = init;
         d.atAddress = atAddr;
         sec.decls.push_back(std::move(d));
      }
   }

   expect(TokenType::KW_END_VAR, "Expected END_VAR");
   return sec;
}

/**
 * @brief Parse an ATTRIBUTE (e.g., {attribute 'qualified_only'})
 * @return Attribute AST node
 */
Attribute Parser::parseAttribute()
{
   Attribute attr;
   expect(TokenType::KW_ATTRIBUTE, "Expected '{' for attribute");

   // Read attribute name (identifier or string literal)
   if (check(TokenType::IDENTIFIER)) {
      attr.name = advance().text;
   } else if (check(TokenType::STRING_LITERAL)) {
      attr.name = advance().text;
   } else {
      throw error("Expected attribute name");
   }

   // Attribute value
   if (match(TokenType::OP_ASSIGN)) {
      if (check(TokenType::IDENTIFIER) || check(TokenType::STRING_LITERAL)) {
         attr.value = advance().text;
      } else {
         throw error("Expected attribute value");
      }
   }

   expect(TokenType::KW_END_ATTRIBUTE, "Expected '}' for attribute");
   return attr;
}

/**
 * @brief Parse a sequence of attributes
 * @return Vector of Attribute AST nodes
 */
std::vector<Attribute> Parser::parseAttributes()
{
   std::vector<Attribute> attrs;
   while (check(TokenType::KW_ATTRIBUTE)) {
      attrs.push_back(parseAttribute());
   }
   return attrs;
}

// ============================================================================
//  Type Reference Parsing
// ============================================================================

/**
 * @brief Check if a token type is a valid type keyword
 * @param t Token type to check
 * @return true if token represents a type
 */
bool Parser::isTypeKeyword(TokenType t)
{
   switch (t) {
   case TokenType::KW_BOOL:
   case TokenType::KW_SINT:
   case TokenType::KW_INT:
   case TokenType::KW_DINT:
   case TokenType::KW_LINT:
   case TokenType::KW_USINT:
   case TokenType::KW_UINT:
   case TokenType::KW_UDINT:
   case TokenType::KW_ULINT:
   case TokenType::KW_REAL:
   case TokenType::KW_LREAL:
   case TokenType::KW_BYTE:
   case TokenType::KW_WORD:
   case TokenType::KW_DWORD:
   case TokenType::KW_LWORD:
   case TokenType::KW_STRING:
   case TokenType::KW_WSTRING:
   case TokenType::KW_TIME:
   case TokenType::KW_DATE:
   case TokenType::KW_DT:
   case TokenType::KW_TOD:
   case TokenType::KW_ARRAY:
   case TokenType::KW_POINTER:
   case TokenType::KW_REF_TO:
   case TokenType::IDENTIFIER:
      return true;
   default:
      return false;
   }
}

/**
 * @brief Parse a complete type reference (including ARRAY, POINTER, REF_TO)
 *
 * Handles:
 * - Base types (BOOL, INT, etc.)
 * - Named types (IDENTIFIER)
 * - ARRAY [lo..hi] OF type
 * - POINTER TO type
 * - REF_TO type
 *
 * @return TypeRef AST node
 */
TypeRef Parser::parseTypeRef()
{
   // POINTER TO <type>
   if (match(TokenType::KW_POINTER)) {
      expect(TokenType::KW_TO, "Expected 'TO' after POINTER");
      TypeRef inner = parseTypeRef();
      inner.isPointer = true;
      return inner;
   }
   // REF_TO <type>
   if (match(TokenType::KW_REF_TO)) {
      TypeRef inner = parseTypeRef();
      inner.isRefTo = true;
      return inner;
   }
   // ARRAY [lo..hi, ...] OF <type>
   if (match(TokenType::KW_ARRAY)) {
      expect(TokenType::LBRACKET, "Expected '[' after ARRAY");
      TypeRef arr;
      arr.base = BaseType::NAMED; // Will be overwritten by element type
      do {
         ArrayDim dim;
         dim.low = parseExpr();
         expect(TokenType::OP_RANGE, "Expected '..' in array dimension");
         dim.high = parseExpr();
         arr.arrayDims.push_back(std::move(dim));
      } while (match(TokenType::COMMA));
      expect(TokenType::RBRACKET, "Expected ']'");
      expect(TokenType::KW_OF, "Expected 'OF' after array dimensions");
      TypeRef elem = parseTypeRef();
      elem.arrayDims = std::move(arr.arrayDims);
      return elem;
   }
   return parseBaseTypeRef();
}

/**
 * @brief Parse a base type reference
 *
 * Handles basic types, STRING with length, and named identifiers.
 *
 * @return TypeRef AST node
 */
TypeRef Parser::parseBaseTypeRef()
{
   TypeRef tr;
   auto t = peek().type;

   auto mapBase = [&](BaseType b) {
      tr.base = b;
      tr.name = advance().text;
   };

   switch (t) {
   case TokenType::KW_BOOL:
      mapBase(BaseType::BOOL);
      break;
   case TokenType::KW_SINT:
      mapBase(BaseType::SINT);
      break;
   case TokenType::KW_INT:
      mapBase(BaseType::INT);
      break;
   case TokenType::KW_DINT:
      mapBase(BaseType::DINT);
      break;
   case TokenType::KW_LINT:
      mapBase(BaseType::LINT);
      break;
   case TokenType::KW_USINT:
      mapBase(BaseType::USINT);
      break;
   case TokenType::KW_UINT:
      mapBase(BaseType::UINT);
      break;
   case TokenType::KW_UDINT:
      mapBase(BaseType::UDINT);
      break;
   case TokenType::KW_ULINT:
      mapBase(BaseType::ULINT);
      break;
   case TokenType::KW_REAL:
      mapBase(BaseType::REAL);
      break;
   case TokenType::KW_LREAL:
      mapBase(BaseType::LREAL);
      break;
   case TokenType::KW_BYTE:
      mapBase(BaseType::BYTE);
      break;
   case TokenType::KW_WORD:
      mapBase(BaseType::WORD);
      break;
   case TokenType::KW_DWORD:
      mapBase(BaseType::DWORD);
      break;
   case TokenType::KW_LWORD:
      mapBase(BaseType::LWORD);
      break;
   case TokenType::KW_TIME:
      mapBase(BaseType::TIME);
      break;
   case TokenType::KW_DATE:
      mapBase(BaseType::DATE);
      break;
   case TokenType::KW_DT:
      mapBase(BaseType::DT);
      break;
   case TokenType::KW_TOD:
      mapBase(BaseType::TOD);
      break;
   case TokenType::KW_STRING:
      tr.base = BaseType::STRING;
      tr.name = advance().text;
      if (match(TokenType::LBRACKET)) {
         auto lenTok = expect(TokenType::INT_LITERAL, "Expected string length");
         tr.stringLen = std::stoi(lenTok.text);
         expect(TokenType::RBRACKET, "Expected ']'");
      }
      break;
   case TokenType::KW_WSTRING:
      tr.base = BaseType::WSTRING;
      tr.name = advance().text;
      break;
   case TokenType::IDENTIFIER:
      tr.base = BaseType::NAMED;
      tr.name = advance().text;
      break;
   default:
      throw error("Expected type name");
   }
   return tr;
}

/**
 * @brief Parse an array initializer: [ expr, expr, ... ] or [[...], [...]]
 * @return Expression AST node (ArrayInitExpr)
 */
std::shared_ptr<Expr> Parser::parseArrayInitializer()
{
   uint32_t ln = peek().line;
   expect(TokenType::LBRACKET, "Expected '[' for array initializer");

   std::vector<std::shared_ptr<Expr>> elements;

   if (!check(TokenType::RBRACKET)) {
      do {
         // Check if this is a nested array initializer (for multi-dim arrays)
         if (check(TokenType::LBRACKET)) {
            // Recursively parse nested array initializer
            elements.push_back(parseArrayInitializer());
         } else {
            elements.push_back(parseExpr());
         }
      } while (match(TokenType::COMMA));
   }

   expect(TokenType::RBRACKET, "Expected ']' after array initializer");

   return std::make_shared<Expr>(ArrayInitExpr{elements}, ln);
}
