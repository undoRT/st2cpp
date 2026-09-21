/**
 * @file TypeSystem.h
 * @brief Type system for semantic analysis: TypeInfo, TypeKind, compatibility
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "ast/AST.h"
#include "semantic/Diagnostics.h"
#include <string>
#include <vector>
#include <cstdint>

namespace st2cpp::semantic {

// Forward declarations
class SymbolTable;

/**
 * @brief Category of a type in the type system
 */
enum class TypeKind {
    Elementary,    // BOOL, INT, REAL, TIME, STRING, etc.
    Array,         // ARRAY[lo..hi] OF T
    Struct,        // STRUCT
    Enum,          // ENUM
    Pointer,       // POINTER TO T
    Reference,     // REF_TO T
    FunctionBlock, // FUNCTION_BLOCK instance type
    Interface,     // INTERFACE
    Void,          // FUNCTION without return
    Unknown        // During resolution
};

/**
 * @brief Context for compatibility checks
 */
enum class CompatContext {
    Assignment,    // LHS := RHS
    CallArgument,  // Argument to function/FB/method call
    Return,        // RETURN value
    BinaryOp,      // Binary operator operands
    UnaryOp,       // Unary operator operand
    ArrayIndex,    // Array indexing expression
    Comparison,    // Comparison operator operands
    StructInit     // Struct initialization member
};

/**
 * @brief IEC 61131-3 conversion groups
 * 
 * Conversions are implicitly allowed within a group (subject to width/sign)
 * and, in IEC 61131-3, between Integer and Real. Bit strings form a distinct
 * group: implicit BitString <-> numeric conversions are NOT defined by the
 * standard and require an explicit conversion function.
 */
enum class ConversionGroup {
    Unknown,    // During resolution
    Boolean,    // BOOL
    Integer,    // SINT, INT, DINT, LINT, USINT, UINT, UDINT, ULINT
    Real,       // REAL, LREAL
    BitString,  // BYTE, WORD, DWORD, LWORD
    Time,       // TIME, DATE, TOD, DT
    String,     // STRING, WSTRING
    Other       // arrays, structs, enums, FB, interfaces, pointers, references, void
};

/**
 * @brief Array dimension information (semantic, not syntactic)
 */
struct ArrayDimInfo {
    int low = 0;
    int high = 0;
    bool isConstant = false;  // true if bounds are compile-time constants
    
    bool operator==(const ArrayDimInfo& other) const {
        return low == other.low && high == other.high && isConstant == other.isConstant;
    }
    bool operator!=(const ArrayDimInfo& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Canonical type information (semantic, not syntactic)
 * 
 * This is the resolved, canonical representation of a type.
 * TypeRef in AST is syntactic; TypeInfo is semantic.
 */
struct TypeInfo {
    TypeId id = 0;
    TypeKind kind = TypeKind::Unknown;
    std::string name;           // "INT", "MyStruct", "MyFB", etc.
    
    // --- Elementary types ---
    BaseType baseType = BaseType::VOID;
    bool isBitType = false;     // BOOL
    bool isNumeric = false;
    bool isSigned = false;
    bool isReal = false;
    bool isTime = false;
    bool isString = false;
    bool isBitString = false;   // BYTE, WORD, DWORD, LWORD
    
    // --- Array ---
    TypeId elementTypeId = 0;
    std::vector<ArrayDimInfo> dimensions;
    
    // --- Struct/Enum/FB/Interface ---
    SymbolId symbolId = 0;      // Link to declaring symbol
    
    // --- Pointer/Reference ---
    TypeId pointedTypeId = 0;
    
    // --- Derived properties (computed once) ---
    size_t sizeInBytes = 0;
    size_t alignment = 0;
    // Note: isBitType, isNumeric, isSigned, isReal, isTime, isString, isBitString
    // are already declared in Elementary types section above
    
    // --- Compatibility checks ---
    
    /**
     * @brief IEC 61131-3 conversion group this type belongs to
     */
    ConversionGroup conversionGroup() const;
    
    /**
     * @brief Check if this type is compatible with another in given context
     */
    bool isCompatibleWith(const TypeInfo* other, CompatContext ctx) const;
    
    /**
     * @brief Check if a value of type 'from' can be assigned to this type (LHS := RHS)
     * 
     * Permissive baseline (project decision): numeric conversions, including
     * between Integer/Real and BitString, are delegated to C++ (the target emits
     * an explicit static_cast when the elementary spellings differ). BOOL is
     * strict, Time/String require exact match, struct/enum/FB are nominal and
     * Pointer/Reference require the same pointed-to type. Strict IEC 61131-3
     * conformance is enforced separately via isImplicitlyConvertibleFrom.
     */
    bool isAssignableFrom(const TypeInfo* from) const;
    
    /**
     * @brief Check if 'from' is implicitly convertible to this type
     * 
     * Stricter than isAssignableFrom: value-preserving implicit conversions only
     * (IEC 61131-3 implicit conversion rules). Used for expression contexts and
     * by Strict mode (Fase 6). TRUE only for same-type, same-group widening that
     * cannot lose value or require the generated cast:
     *   - Integer -> Integer of equal or wider rank with same sign
     *   - Integer -> Real (widening), Real -> LREAL
     *   - BitString -> BitString of equal or wider rank
     *   - identical Time/String base types, exact struct/enum/FB, same pointed-to
     * Rejects narrowing (incl. Real -> Integer), sign-changing, cross-group and
     * BitString <-> numeric conversions.
     */
    bool isImplicitlyConvertibleFrom(const TypeInfo* from) const;
    
    // --- Factory methods for built-in types ---
    static TypeInfo makeBool();
    static TypeInfo makeInt(BaseType bt);
    static TypeInfo makeReal(BaseType bt);
    static TypeInfo makeBitString(BaseType bt);
    static TypeInfo makeTime(BaseType bt);
    static TypeInfo makeString(BaseType bt);
    static TypeInfo makeVoid();
    static TypeInfo makeUnknown();
};

// Forward declare SymbolTable for factory methods
class SymbolTable;

/**
 * @brief Type checker helper functions (stateless, operate on TypeInfo)
 * 
 * These are free functions in TypeSystem.cpp, not methods of a class.
 */
namespace TypeChecker {

// Assignment compatibility
bool checkAssignment(const TypeInfo* lhs, const TypeInfo* rhs, const SourceLocation& loc, Diagnostics& diag);

// Binary operator type checking
TypeId checkBinaryOp(const std::string& op, const TypeInfo* left, const TypeInfo* right, const SourceLocation& loc, Diagnostics& diag, const SymbolTable& symTab);

// Unary operator type checking
TypeId checkUnaryOp(const std::string& op, const TypeInfo* operand, const SourceLocation& loc, Diagnostics& diag, const SymbolTable& symTab);

// Call argument checking
bool checkCallArguments(const std::vector<SymbolId>& paramSymbols, const std::vector<CallExpr::Arg>& args, const SourceLocation& loc, Diagnostics& diag, SymbolTable& symTab, bool allowPartial = false);

// Expression type resolution (returns TypeId, 0 if error)
TypeId resolveExpressionType(const Expr& expr, SymbolId scopeId, SymbolTable& symTab, Diagnostics& diag);

// Helper: check if expression is BOOL-typed (for logical vs bitwise ops)
bool isBoolType(TypeId typeId, const SymbolTable& symTab);

// Helper: get common type for arithmetic (C++ usual arithmetic conversions)
TypeId getArithmeticCommonType(const TypeInfo* left, const TypeInfo* right, const SymbolTable& symTab);

// Member access checking
struct MemberResult {
    SymbolId symbolId = 0;
    TypeId typeId = 0;
};
MemberResult checkMemberAccess(const TypeInfo& objectType, const std::string& member, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc);

// Index access checking
struct IndexResult {
    TypeId elementTypeId = 0;
    bool valid = false;
};
IndexResult checkIndexAccess(const TypeInfo& arrayType, const TypeInfo& indexType, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc);

// Pointer/Reference checking
struct PointerResult {
    TypeId pointedTypeId = 0;
    bool valid = false;
};
PointerResult checkPointerDereference(const TypeInfo& ptrType, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc);

} // namespace TypeChecker

} // namespace st2cpp::semantic
