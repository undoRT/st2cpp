/**
 * @file AST.h
 * @brief Abstract Syntax Tree nodes for Structured Text
 *
 * This file defines all node types used to represent the
 * structure of a Structured Text program after parsing.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

// Forward declarations
struct TypeRef;
struct VarDecl;
struct VarSection;
struct Stmt;
struct Expr;
struct POU;

// Type references

enum class BaseType {
   BOOL,
   SINT,
   INT,
   DINT,
   LINT,
   USINT,
   UINT,
   UDINT,
   ULINT,
   REAL,
   LREAL,
   BYTE,
   WORD,
   DWORD,
   LWORD,
   STRING,
   WSTRING,
   TIME,
   DATE,
   DT,
   TOD,
   VOID,
   NAMED, // user-defined type or FB
};

/**
 * @struct ArrayDim
 * @brief Represents an array dimension with low and high bounds
 */
struct ArrayDim
{
   std::shared_ptr<Expr> low;
   std::shared_ptr<Expr> high;
};

/**
 * @struct TypeRef
 * @brief Reference to a data type (base type, array, pointer, etc.)
 */
struct TypeRef
{
   BaseType base = BaseType::NAMED; ///< Base type (if not NAMED)
   std::string name;                ///< Type name (if NAMED or user-defined)
   bool isPointer = false;          ///< Is this a POINTER TO type?
   bool isRefTo = false;            ///< Is this a REF_TO type?
   std::vector<ArrayDim> arrayDims; ///< Array dimensions (non-empty if array)
   std::optional<int> stringLen;    ///< Length for STRING[n]
};

/**
 * @struct StructMember
 * @brief Member declaration inside a `STRUCT` type
 */
struct StructMember
{
   std::string name;
   TypeRef type;
   std::shared_ptr<Expr> initialValue; // optional
};

/**
 * @struct StructType
 * @brief Represents a user-defined struct type
 */
struct StructType
{
   std::string name;
   std::vector<StructMember> members;
};

/**
 * @struct EnumEnumerator
 * @brief Single enumerator inside an `ENUM` type
 */
struct EnumEnumerator
{
   std::string name;
   std::shared_ptr<Expr> value; // optional
};

/**
 * @struct EnumType
 * @brief Represents an enumerated type
 */
struct EnumType
{
   std::string name;
   std::vector<EnumEnumerator> enumerators;
};

enum class VarKind {
   VAR,
   INPUT,
   OUTPUT,
   IN_OUT,
   EXTERNAL,
   GLOBAL,
   TEMP
};

/**
 * @struct MethodParameter
 * @brief Parameter declaration inside a method (INPUT, OUTPUT, IN_OUT)
 */
struct MethodParameter
{
   std::string name;
   TypeRef type;
   VarKind kind; // INPUT, OUTPUT, IN_OUT, VAR, TEMP
   std::shared_ptr<Expr> initialValue;
};

/**
 * @struct Method
 * @brief Method declaration inside a FUNCTION_BLOCK
 */
struct Method
{
   std::string name;
   TypeRef returnType; // BaseType::VOID if no return
   std::vector<MethodParameter> parameters;
   std::vector<VarDecl> localVars; // VAR and VAR_TEMP
   std::vector<std::shared_ptr<Stmt>> body;
   uint32_t line = 0;
   bool isOverride = false;
   bool isFinal = false;
   bool isAbstract = false;
};

/**
 * @struct VarDecl
 * @brief Declaration of a variable inside a VAR section
 */
struct VarDecl
{
   std::string name;
   TypeRef type;
   std::shared_ptr<Expr> initialValue; // optional
   bool isConstant = false;
   bool isRetain = false;
   std::string atAddress; // AT %... address string
   uint32_t line = 0;
};

/**
 * @struct Attribute
 * @brief Represents an attribute attached to a POU or variable (e.g. {attribute 'qualified_only'})
 */
struct Attribute
{
   std::string name;
   std::string value; // optional, eg. 'qualified_only'
};

/**
 * @struct VarSection
 * @brief A section of variable declarations (VAR_INPUT, VAR_OUTPUT, etc.)
 */
struct VarSection
{
   VarKind kind;
   std::vector<Attribute> attributes;
   std::vector<VarDecl> decls;
};

struct LiteralExpr
{
   std::string value;
   std::string suffix; /* type prefix e.g. INT# */
};

struct IdentExpr
{
   std::string name;
};

struct BoolLitExpr
{
   bool value;
};

struct UnaryExpr
{
   std::string op;
   std::shared_ptr<Expr> operand;
};

struct BinaryExpr
{
   std::string op;
   std::shared_ptr<Expr> left;
   std::shared_ptr<Expr> right;
};

struct MemberExpr
{
   std::shared_ptr<Expr> object;
   std::string member;
};

struct IndexExpr
{
   std::shared_ptr<Expr> array;
   std::vector<std::shared_ptr<Expr>> indices;
};

struct DerefExpr
{
   std::shared_ptr<Expr> pointer;
};

struct CallExpr
{
   std::shared_ptr<Expr> callee;
   // Named params (fb calls): a := val  OR positional
   struct Arg
   {
      std::string name;
      std::shared_ptr<Expr> value;
      bool named;
      bool isOutput = false;
      uint32_t line = 0;
      uint32_t col = 0;
   };
   std::vector<Arg> args;
};

struct SuperCallExpr
{
   std::string methodName;
   std::vector<CallExpr::Arg> args;
};

struct Interface
{
   std::string name;
   std::vector<Method> methods;
   uint32_t line;
};

struct AdrExpr
{
   std::shared_ptr<Expr> operand;
};

struct SizeofExpr
{
   TypeRef type;
};

struct CastExpr
{
   TypeRef targetType;
   std::shared_ptr<Expr> operand;
};

struct ArrayInitExpr
{
   std::vector<std::shared_ptr<Expr>> elements;
};

using ExprVariant = std::variant<LiteralExpr,
                                 BoolLitExpr,
                                 IdentExpr,
                                 UnaryExpr,
                                 BinaryExpr,
                                 MemberExpr,
                                 IndexExpr,
                                 DerefExpr,
                                 CallExpr,
                                 SuperCallExpr,
                                 AdrExpr,
                                 SizeofExpr,
                                 CastExpr,
                                 ArrayInitExpr>;

struct Expr
{
   ExprVariant node;
   uint32_t line = 0;

   template<typename T>
   explicit Expr(T&& v, uint32_t ln = 0) : node(std::forward<T>(v)), line(ln)
   {}
};

struct AssignStmt
{
   std::shared_ptr<Expr> lhs;
   std::shared_ptr<Expr> rhs;
};
struct ExprStmt
{
   std::shared_ptr<Expr> expr;
}; // FB call as statement
struct ReturnStmt
{};
struct ExitStmt
{};
struct EmptyStmt
{};

struct IfBranch
{
   std::shared_ptr<Expr> condition; // nullptr → ELSE branch
   std::vector<std::shared_ptr<Stmt>> body;
};
struct IfStmt
{
   std::vector<IfBranch> branches;
};

struct ForStmt
{
   std::string var;
   std::shared_ptr<Expr> from;
   std::shared_ptr<Expr> to;
   std::shared_ptr<Expr> by; // optional (nullptr → 1)
   std::vector<std::shared_ptr<Stmt>> body;
};

struct WhileStmt
{
   std::shared_ptr<Expr> condition;
   std::vector<std::shared_ptr<Stmt>> body;
};

struct RepeatStmt
{
   std::vector<std::shared_ptr<Stmt>> body;
   std::shared_ptr<Expr> condition;
};

struct CaseValue
{
   // single value or range
   std::shared_ptr<Expr> low;
   std::shared_ptr<Expr> high; // nullptr → not a range
};
struct CaseBranch
{
   std::vector<CaseValue> values; // empty → ELSE
   std::vector<std::shared_ptr<Stmt>> body;
};
struct CaseStmt
{
   std::shared_ptr<Expr> selector;
   std::vector<CaseBranch> branches;
};

using StmtVariant = std::variant<AssignStmt, ExprStmt, ReturnStmt, ExitStmt, EmptyStmt, IfStmt, ForStmt, WhileStmt, RepeatStmt, CaseStmt>;

struct Stmt
{
   StmtVariant node;
   uint32_t line = 0;

   template<typename T>
   explicit Stmt(T&& v, uint32_t ln = 0) : node(std::forward<T>(v)), line(ln)
   {}
};

enum class POUKind {
   FUNCTION_BLOCK,
   FUNCTION,
   PROGRAM
};

struct POU
{
   POUKind kind;
   std::string name;
   TypeRef returnType; // only for FUNCTION
   std::vector<VarSection> varSections;
   std::vector<std::shared_ptr<Stmt>> body;
   std::vector<Method> methods;
   uint32_t line = 0;
   std::string extends;
   std::vector<std::string> implements;
   bool isAbstract = false;
   bool isFinal = false;
};

// Translation unit
struct TranslationUnit
{
   std::vector<POU> pous;
   std::vector<StructType> structs;
   std::vector<EnumType> enums;
   std::vector<VarSection> globals;
   std::vector<Interface> interfaces;
};
