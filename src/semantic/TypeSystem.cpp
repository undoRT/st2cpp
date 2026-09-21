/**
 * @file TypeSystem.cpp
 * @brief Type system implementation: TypeInfo factories, built-in registration, compatibility
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/TypeSystem.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "ast/AST.h"
#include <algorithm>
#include <cctype>

namespace st2cpp::semantic {

// ============================================================================
// TypeInfo Factory Methods
// ============================================================================

/**
 * @brief Construct a TypeInfo for the BOOL elementary type.
 * @return The new TypeInfo describing BOOL
 */
TypeInfo TypeInfo::makeBool()
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = BaseType::BOOL;
   t.name = "BOOL";
   t.isBitType = true;
   t.sizeInBytes = 1;
   t.alignment = 1;
   return t;
}

/**
 * @brief Construct a TypeInfo for a signed or unsigned integer elementary type.
 * @param bt The integer base type (SINT..ULINT)
 * @return The new TypeInfo with the appropriate name, width and alignment
 */
TypeInfo TypeInfo::makeInt(BaseType bt)
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = bt;
   t.isNumeric = true;

   switch (bt) {
   case BaseType::SINT:
      t.name = "SINT";
      t.sizeInBytes = 1;
      t.alignment = 1;
      t.isSigned = true;
      break;
   case BaseType::INT:
      t.name = "INT";
      t.sizeInBytes = 2;
      t.alignment = 2;
      t.isSigned = true;
      break;
   case BaseType::DINT:
      t.name = "DINT";
      t.sizeInBytes = 4;
      t.alignment = 4;
      t.isSigned = true;
      break;
   case BaseType::LINT:
      t.name = "LINT";
      t.sizeInBytes = 8;
      t.alignment = 8;
      t.isSigned = true;
      break;
   case BaseType::USINT:
      t.name = "USINT";
      t.sizeInBytes = 1;
      t.alignment = 1;
      t.isSigned = false;
      break;
   case BaseType::UINT:
      t.name = "UINT";
      t.sizeInBytes = 2;
      t.alignment = 2;
      t.isSigned = false;
      break;
   case BaseType::UDINT:
      t.name = "UDINT";
      t.sizeInBytes = 4;
      t.alignment = 4;
      t.isSigned = false;
      break;
   case BaseType::ULINT:
      t.name = "ULINT";
      t.sizeInBytes = 8;
      t.alignment = 8;
      t.isSigned = false;
      break;
   default:
      t.name = "INT";
      t.sizeInBytes = 2;
      t.alignment = 2;
      t.isSigned = true;
   }
   return t;
}

/**
 * @brief Construct a TypeInfo for a real elementary type.
 * @param bt The real base type (REAL or LREAL)
 * @return The new TypeInfo with the appropriate name, width and alignment
 */
TypeInfo TypeInfo::makeReal(BaseType bt)
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = bt;
   t.isNumeric = true;
   t.isReal = true;
   t.isSigned = true;

   switch (bt) {
   case BaseType::REAL:
      t.name = "REAL";
      t.sizeInBytes = 4;
      t.alignment = 4;
      break;
   case BaseType::LREAL:
      t.name = "LREAL";
      t.sizeInBytes = 8;
      t.alignment = 8;
      break;
   default:
      t.name = "REAL";
      t.sizeInBytes = 4;
      t.alignment = 4;
   }
   return t;
}

/**
 * @brief Construct a TypeInfo for a bit-string elementary type.
 * @param bt The bit-string base type (BYTE..LWORD)
 * @return The new TypeInfo describing the bit string
 */
TypeInfo TypeInfo::makeBitString(BaseType bt)
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = bt;
   t.isBitString = true;
   t.isNumeric = true; // Bit strings behave as unsigned integers
   t.isSigned = false;

   switch (bt) {
   case BaseType::BYTE:
      t.name = "BYTE";
      t.sizeInBytes = 1;
      t.alignment = 1;
      break;
   case BaseType::WORD:
      t.name = "WORD";
      t.sizeInBytes = 2;
      t.alignment = 2;
      break;
   case BaseType::DWORD:
      t.name = "DWORD";
      t.sizeInBytes = 4;
      t.alignment = 4;
      break;
   case BaseType::LWORD:
      t.name = "LWORD";
      t.sizeInBytes = 8;
      t.alignment = 8;
      break;
   default:
      t.name = "BYTE";
      t.sizeInBytes = 1;
      t.alignment = 1;
   }
   return t;
}

/**
 * @brief Construct a TypeInfo for a time/date elementary type.
 * @param bt The time base type (TIME, DATE, TOD or DT)
 * @return The new TypeInfo describing the time type
 */
TypeInfo TypeInfo::makeTime(BaseType bt)
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = bt;
   t.isTime = true;
   t.sizeInBytes = 4;
   t.alignment = 4;

   switch (bt) {
   case BaseType::TIME:
      t.name = "TIME";
      break;
   case BaseType::DATE:
      t.name = "DATE";
      break;
   case BaseType::TOD:
      t.name = "TOD";
      t.sizeInBytes = 8;
      t.alignment = 8;
      break;
   case BaseType::DT:
      t.name = "DT";
      t.sizeInBytes = 8;
      t.alignment = 8;
      break;
   default:
      t.name = "TIME";
   }
   return t;
}

/**
 * @brief Construct a TypeInfo for a string elementary type.
 * @details The produced type has a dynamic (zero) size.
 * @param bt The string base type (STRING or WSTRING)
 * @return The new TypeInfo describing the string type
 */
TypeInfo TypeInfo::makeString(BaseType bt)
{
   TypeInfo t;
   t.kind = TypeKind::Elementary;
   t.baseType = bt;
   t.isString = true;

   switch (bt) {
   case BaseType::STRING:
      t.name = "STRING";
      break;
   case BaseType::WSTRING:
      t.name = "WSTRING";
      break;
   default:
      t.name = "STRING";
   }
   t.sizeInBytes = 0; // Dynamic size
   t.alignment = 1;
   return t;
}

/**
 * @brief Construct a TypeInfo for the VOID type.
 * @return The new TypeInfo describing VOID
 */
TypeInfo TypeInfo::makeVoid()
{
   TypeInfo t;
   t.kind = TypeKind::Void;
   t.baseType = BaseType::VOID;
   t.name = "VOID";
   t.sizeInBytes = 0;
   t.alignment = 1;
   return t;
}

/**
 * @brief Construct a placeholder TypeInfo for unresolved types.
 * @return The new TypeInfo describing an unknown type
 */
TypeInfo TypeInfo::makeUnknown()
{
   TypeInfo t;
   t.kind = TypeKind::Unknown;
   t.baseType = BaseType::VOID;
   t.name = "<unknown>";
   t.sizeInBytes = 0;
   t.alignment = 1;
   return t;
}

// ============================================================================
// Compatibility Checks (Baseline from CodeGenerator behavior)
// ============================================================================

/**
 * @brief Classify this type into a conversion family.
 * @details Unknown types classify as Unknown, non-elementary types as Other,
 * and elementary types by their bit/numeric/time/string flags.
 * @return The conversion group this type belongs to
 */
ConversionGroup TypeInfo::conversionGroup() const
{
   if (kind == TypeKind::Unknown) {
      return ConversionGroup::Unknown;
   }
   if (kind != TypeKind::Elementary) {
      return ConversionGroup::Other;
   }
   if (isBitType) {
      return ConversionGroup::Boolean;
   }
   if (isBitString) {
      return ConversionGroup::BitString;
   }
   if (isReal) {
      return ConversionGroup::Real;
   }
   if (isNumeric) {
      return ConversionGroup::Integer;
   }
   if (isTime) {
      return ConversionGroup::Time;
   }
   if (isString) {
      return ConversionGroup::String;
   }
   return ConversionGroup::Other;
}

/**
 * @brief Check whether this type is compatible with another type.
 * @details Types sharing the same nonzero TypeId are compatible; Unknown is
 * compatible with anything during resolution and Void only with Void. Different
 * kinds allow numeric and bit-string widening, matching the CodeGenerator baseline.
 * @param other The other type to compare against
 * @param ctx The compatibility context of the check
 * @return true when the types are compatible
 */
bool TypeInfo::isCompatibleWith(const TypeInfo* other, CompatContext ctx) const
{
   if (!other) {
      return false;
   }
   if (this == other) {
      return true;
   }
   // If both have valid (non-zero) IDs and they're the same, they're compatible
   if (id != 0 && id == other->id) {
      return true;
   }

   // Unknown compatible with anything (during resolution)
   if (kind == TypeKind::Unknown || other->kind == TypeKind::Unknown) {
      return true;
   }

   // Void only compatible with Void
   if (kind == TypeKind::Void || other->kind == TypeKind::Void) {
      return kind == TypeKind::Void && other->kind == TypeKind::Void;
   }

   // Different kinds generally not compatible (except numeric widening)
   if (kind != other->kind) {
      // Numeric widening: smaller -> larger integer/real
      if (isNumeric && other->isNumeric) {
         // Allow widening (e.g., INT -> DINT, REAL -> LREAL)
         // But not narrowing (handled as warning in C++)
         return true; // Baseline: allow, C++ handles it
      }
      // Bit string widening
      if (isBitString && other->isBitString) {
         return true;
      }
      return false;
   }

   // Same kind: check specifics
   switch (kind) {
   case TypeKind::Elementary:
      // Same base type = compatible
      // Also allow numeric widening (e.g., INT -> DINT, REAL -> LREAL)
      if (baseType == other->baseType) {
         return true;
      }
      if (isNumeric && other->isNumeric) {
         return true; // Numeric widening
      }
      if (isBitString && other->isBitString) {
         return true; // Bit string widening
      }
      return false;

   case TypeKind::Array:
      // Arrays compatible if same element type AND same dimensions
      return elementTypeId == other->elementTypeId && dimensions == other->dimensions;

   case TypeKind::Struct:
   case TypeKind::Enum:
   case TypeKind::FunctionBlock:
   case TypeKind::Interface:
      // Nominal typing: same symbol = compatible
      return symbolId == other->symbolId;

   case TypeKind::Pointer:
   case TypeKind::Reference:
      // Same pointed-to type
      return pointedTypeId == other->pointedTypeId;

   default:
      return false;
   }
}

/**
 * @brief Check whether a value of another type can be assigned to this type.
 * @details Uses the permissive CodeGenerator baseline: elementary numeric
 * families are cross-assignable (the emitted C++ static_cast handles
 * widening/narrowing), while non-elementary types require a nominal/exact match.
 * @param from The source type being assigned
 * @return true when the assignment is allowed
 */
bool TypeInfo::isAssignableFrom(const TypeInfo* from) const
{
   if (!from) {
      return false;
   }
   if (this == from) {
      return true;
   }
   // If both have valid (non-zero) IDs and they're the same, they're assignable
   if (id != 0 && id == from->id) {
      return true;
   }

   // Unknown assignable to/from anything (during resolution)
   if (kind == TypeKind::Unknown || from->kind == TypeKind::Unknown) {
      return true;
   }

   // Void only from Void
   if (kind == TypeKind::Void || from->kind == TypeKind::Void) {
      return kind == TypeKind::Void && from->kind == TypeKind::Void;
   }

   // Pointer/Reference: same pointed-to type
   if (kind == TypeKind::Pointer || kind == TypeKind::Reference) {
      if (from->kind == kind && pointedTypeId == from->pointedTypeId) {
         return true;
      }
      return false;
   }

   // Exact match for non-elementary
   if (kind != TypeKind::Elementary) {
      return isCompatibleWith(from, CompatContext::Assignment);
   }

   // Elementary: C++ usual arithmetic conversions (baseline from CodeGenerator).
   // BOOL only from BOOL
   if (isBitType) {
      return from->isBitType;
   }

   // Numeric family: C++ handles widening/narrowing (emitted static_cast).
   // Integer <-> Real <-> BitString cross-conversions are delegated to C++;
   // NOTE: IEC 61131-3 defines no implicit BitString <-> numeric conversion;
   // that conformance is enforced at Strict level via isImplicitlyConvertibleFrom.
   ConversionGroup g = conversionGroup();
   ConversionGroup fg = from->conversionGroup();
   if ((g == ConversionGroup::Integer || g == ConversionGroup::Real || g == ConversionGroup::BitString)
       && (fg == ConversionGroup::Integer || fg == ConversionGroup::Real || fg == ConversionGroup::BitString)) {
      return true;
   }

   // String/Time: exact match
   if (isString || isTime) {
      return baseType == from->baseType;
   }

   return false;
}

/**
 * @brief Check whether another type converts implicitly (value-preserving) to this type.
 * @details Enforces the IEC 61131-3 implicit-conversion rules used by expression
 * contexts and Strict mode: same-sign integer widening, same-rank-or-wider bit
 * strings, and integer-to-same-or-larger-real widenings only.
 * @param from The source type being converted
 * @return true when the conversion is implicit and value-preserving
 */
bool TypeInfo::isImplicitlyConvertibleFrom(const TypeInfo* from) const
{
   // Value-preserving implicit conversions (IEC 61131-3 style).
   // Used by expression contexts and Strict mode; isAssignableFrom remains the
   // permissive baseline that delegates remaining conversions to C++.
   if (!from) {
      return false;
   }
   if (this == from) {
      return true;
   }
   if (id != 0 && id == from->id) {
      return true;
   }

   // Unknown assignable to/from anything (during resolution)
   if (kind == TypeKind::Unknown || from->kind == TypeKind::Unknown) {
      return true;
   }

   // Void only from Void
   if (kind == TypeKind::Void || from->kind == TypeKind::Void) {
      return kind == TypeKind::Void && from->kind == TypeKind::Void;
   }

   // Non-elementary: nominal/exact (struct, enum, FB, array, pointer, reference)
   if (kind != TypeKind::Elementary) {
      return isCompatibleWith(from, CompatContext::Assignment);
   }

   if (from->kind != TypeKind::Elementary) {
      return false;
   }

   // BOOL only from BOOL
   if (isBitType) {
      return from->isBitType;
   }

   ConversionGroup g = conversionGroup();
   ConversionGroup fg = from->conversionGroup();

   // Time/String: exact match only
   if (g == ConversionGroup::Time || g == ConversionGroup::String) {
      return baseType == from->baseType;
   }

   // Integer -> same-sign Integer of equal or wider rank
   if (g == ConversionGroup::Integer) {
      if (fg != ConversionGroup::Integer) {
         return false;
      }
      if (isSigned != from->isSigned) {
         return false;
      }
      return sizeInBytes >= from->sizeInBytes;
   }

   // BitString -> same-rank-or-wider BitString
   if (g == ConversionGroup::BitString) {
      if (fg != ConversionGroup::BitString) {
         return false;
      }
      return sizeInBytes >= from->sizeInBytes;
   }

   // Real target: from Integer or narrower Real (widenings only)
   if (g == ConversionGroup::Real) {
      if (fg == ConversionGroup::Real) {
         return sizeInBytes >= from->sizeInBytes;
      }
      return fg == ConversionGroup::Integer;
   }

   return false;
}

// ============================================================================
// TypeChecker Implementation
// ============================================================================

namespace TypeChecker {

/**
 * @brief Validate an assignment for compatible types, reporting a diagnostic on failure.
 * @param lhs The target (left-hand side) type
 * @param rhs The source (right-hand side) type
 * @param loc The source location of the assignment
 * @param diag The diagnostics collector for errors
 * @return true when the assignment is valid
 */
bool checkAssignment(const TypeInfo* lhs, const TypeInfo* rhs, const SourceLocation& loc, Diagnostics& diag)
{
   if (!lhs || !rhs) {
      return false;
   }

   if (!lhs->isAssignableFrom(rhs)) {
      diag.addError(DiagnosticCode::InvalidAssignment, "cannot assign " + rhs->name + " to " + lhs->name, loc);
      return false;
   }
   return true;
}

/**
 * @brief Validate and type a binary expression for a given operator.
 * @details Comparison operators yield BOOL; logical/bitwise AND-OR-XOR map to
 * BOOL or a common integral type; MOD requires integer operands only; arithmetic
 * operators require numeric operands. Unsupported combinations are reported as
 * diagnostics and yield an invalid TypeId.
 * @param op The operator token (e.g., "+", "AND", "MOD")
 * @param left The type of the left operand
 * @param right The type of the right operand
 * @param loc The source location of the expression
 * @param diag The diagnostics collector for errors
 * @param symTab The symbol table used to look up result types
 * @return The TypeId of the expression result, or 0 when invalid
 */
TypeId checkBinaryOp(const std::string& op,
                     const TypeInfo* left,
                     const TypeInfo* right,
                     const SourceLocation& loc,
                     Diagnostics& diag,
                     const SymbolTable& symTab)
{
   if (!left || !right) {
      return 0;
   }

   // Comparison operators -> BOOL
   if (op == "=" || op == "==" || op == "<>" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
      if (!left->isCompatibleWith(right, CompatContext::Comparison)) {
         diag.addError(DiagnosticCode::InvalidBinaryOperands, "cannot compare " + left->name + " with " + right->name, loc);
         return 0;
      }
      return symTab.getTypeIdByName("BOOL");
   }

   // Logical/bitwise operators (AND, OR, XOR)
   // BOOL -> logical; integer and bit-string operands -> bitwise.
   // REAL is rejected: the C++ target emits '&'/'|'/'^' which do not compile
   // on floats (IEC 61131-3: bitwise operators are not defined for reals).
   if (op == "AND" || op == "OR" || op == "XOR") {
      bool leftBool = left->isBitType;
      bool rightBool = right->isBitType;

      if (leftBool && rightBool) {
         return symTab.getTypeIdByName("BOOL");
      }
      // Integer-like operands (including bit strings): bitwise is well-defined
      bool leftIntegral = left->isNumeric && !left->isReal;
      bool rightIntegral = right->isNumeric && !right->isReal;
      if (leftIntegral && rightIntegral) {
         return getArithmeticCommonType(left, right, symTab);
      }
      diag.addError(DiagnosticCode::InvalidBinaryOperands,
                    "operator '" + op + "' not supported between " + left->name + " and " + right->name,
                    loc);
      return 0;
   }

   // MOD: integer operands only (C++ '%' does not compile on floats)
   if (op == "MOD") {
      bool leftInt = left->isNumeric && !left->isReal;
      bool rightInt = right->isNumeric && !right->isReal;
      if (leftInt && rightInt) {
         return getArithmeticCommonType(left, right, symTab);
      }
      diag.addError(DiagnosticCode::InvalidBinaryOperands,
                    "operator 'MOD' requires integer operands, got " + left->name + " and " + right->name,
                    loc);
      return 0;
   }

   // Arithmetic operators
   if (op == "+" || op == "-" || op == "*" || op == "/") {
      if (!left->isNumeric || !right->isNumeric) {
         diag.addError(DiagnosticCode::InvalidBinaryOperands, "arithmetic operator '" + op + "' requires numeric operands", loc);
         return 0;
      }
      return getArithmeticCommonType(left, right, symTab);
   }

   // Power operator
   if (op == "**") {
      if (!left->isNumeric || !right->isNumeric) {
         diag.addError(DiagnosticCode::InvalidBinaryOperands, "power operator requires numeric operands", loc);
         return 0;
      }
      return getArithmeticCommonType(left, right, symTab);
   }

   return 0;
}

/**
 * @brief Validate and type a unary expression for a given operator.
 * @details NOT requires a BOOL operand and yields BOOL; unary "+"/"-" require a
 * numeric operand and preserve its type. Unsupported combinations are reported
 * as diagnostics and yield an invalid TypeId.
 * @param op The operator token ("NOT", "+" or "-")
 * @param operand The type of the operand
 * @param loc The source location of the expression
 * @param diag The diagnostics collector for errors
 * @param symTab The symbol table used to look up result types
 * @return The TypeId of the expression result, or 0 when invalid
 */
TypeId checkUnaryOp(const std::string& op, const TypeInfo* operand, const SourceLocation& loc, Diagnostics& diag, const SymbolTable& symTab)
{
   if (!operand) {
      return 0;
   }

   if (op == "NOT") {
      if (!operand->isBitType) {
         diag.addError(DiagnosticCode::InvalidUnaryOperand, "NOT requires BOOL operand, got " + operand->name, loc);
         return 0;
      }
      return symTab.getTypeIdByName("BOOL");
   }

   if (op == "-" || op == "+") {
      if (!operand->isNumeric) {
         diag.addError(DiagnosticCode::InvalidUnaryOperand, "unary '" + op + "' requires numeric operand, got " + operand->name, loc);
         return 0;
      }
      return operand->id;
   }

   return 0;
}

/**
 * @brief Validate call arguments against the parameter symbols of a callee.
 * @details Each argument is checked for assignability to its corresponding
 * parameter, and a mismatched argument count yields a wrong-argument-count
 * diagnostic.
 * @param paramSymbols The SymbolIds of the callee parameters
 * @param args The call arguments to validate
 * @param loc The source location of the call
 * @param diag The diagnostics collector for errors
 * @param symTab The symbol table used to resolve parameter and argument types
 * @return true when all arguments are valid
 */
bool checkCallArguments(const std::vector<SymbolId>& paramSymbols,
                        const std::vector<CallExpr::Arg>& args,
                        const SourceLocation& loc,
                        Diagnostics& diag,
                        SymbolTable& symTab,
                        bool allowPartial)
{
   // Excess arguments are always an error for every callee kind.
   if (args.size() > paramSymbols.size()) {
      diag.addError(DiagnosticCode::WrongArgumentCount,
                    "expected " + std::to_string(paramSymbols.size()) + " arguments, got " + std::to_string(args.size()), loc);
      return false;
   }

   // Pair provided arguments to parameters. Positional arguments fill the
   // leading parameters by declaration order; named ones pair by name.
   std::vector<bool> provided(paramSymbols.size(), false);
   size_t positional = 0;
   for (const auto& arg : args) {
      if (arg.named) {
         for (size_t pi = 0; pi < paramSymbols.size(); ++pi) {
            const Symbol* p = symTab.get(paramSymbols[pi]);
            if (p && p->name == arg.name) {
               provided[pi] = true;
               break;
            }
         }
      } else if (positional < paramSymbols.size()) {
         provided[positional] = true;
         positional++;
      }
   }

   // FUNCTION/BUILTIN calls must supply every parameter. IEC 61131-3 allows a
   // caller to omit parameters that carry an initial value (default) or that
   // are VAR_OUTPUT / VAR_IN_OUT; every plain VAR_INPUT without a default must
   // be provided. FUNCTION_BLOCK instance calls may omit anything (unconnected
   // inputs keep their state, outputs stay unassigned).
   if (!allowPartial) {
      std::string missingNames;
      for (size_t pi = 0; pi < paramSymbols.size(); ++pi) {
         if (provided[pi]) {
            continue;
         }
         const Symbol* p = symTab.get(paramSymbols[pi]);
         if (!p) {
            continue;
         }
         const bool mayOmit = p->hasDefaultValue || p->paramDir == ParamDir::Output || p->paramDir == ParamDir::InOut;
         if (!mayOmit) {
            if (!missingNames.empty()) {
               missingNames += ", ";
            }
            missingNames += "'" + p->name + "'";
         }
      }
      if (!missingNames.empty()) {
         diag.addError(DiagnosticCode::WrongArgumentCount,
                       "expected " + std::to_string(paramSymbols.size()) + " arguments, got " + std::to_string(args.size())
                          + " (missing required parameter" + (missingNames.find(", ") != std::string::npos ? "s" : "") + ": "
                          + missingNames + ")",
                       loc);
         return false;
      }
   }

   bool allOk = true;
   positional = 0;
   for (size_t i = 0; i < args.size(); i++) {
      // Named arguments pair with their parameter by name; positional ones pair
      // by declaration order.
      const Symbol* paramSym = nullptr;
      if (args[i].named) {
         for (SymbolId pid : paramSymbols) {
            const Symbol* p = symTab.get(pid);
            if (p && p->name == args[i].name) {
               paramSym = p;
               break;
            }
         }
      } else if (positional < paramSymbols.size()) {
         paramSym = symTab.get(paramSymbols[positional]);
         positional++;
      }
      if (!paramSym) {
         continue;
      }
      const TypeInfo* paramType = symTab.getType(paramSym->typeId);
      if (!paramType) {
         continue;
      }

      TypeId argTypeId = args[i].value ? args[i].value->resolvedTypeId : 0;
      const TypeInfo* argType = symTab.getType(argTypeId);
      if (!argType) {
         continue;
      }

      if (!paramType->isAssignableFrom(argType)) {
         diag.addError(DiagnosticCode::InvalidArgumentType, "cannot convert " + argType->name + " to " + paramType->name, loc);
         allOk = false;
      }
   }
   return allOk;
}

/**
 * @brief Resolve the TypeId of an expression from its decorated variants.
 * @details Identifiers resolve through their symbol's typeId, while literals and
 * already-decorated expressions resolve through their resolvedTypeId. The scope
 * and diagnostics parameters are currently unused.
 * @param expr The expression whose type is requested
 * @param scopeId The scope to resolve against (unused)
 * @param symTab The symbol table used for lookups
 * @param diag The diagnostics collector (unused)
 * @return The resolved TypeId of the expression
 */
TypeId resolveExpressionType(const Expr& expr, SymbolId scopeId, SymbolTable& symTab, Diagnostics& diag)
{
   (void) scopeId;
   (void) diag;
   if (auto p = std::get_if<IdentExpr>(&expr.node)) {
      if (expr.symbolId == 0) {
         return 0;
      }
      Symbol* sym = symTab.get(expr.symbolId);
      return sym ? sym->typeId : 0;
   }
   if (auto p = std::get_if<LiteralExpr>(&expr.node)) {
      return expr.resolvedTypeId;
   }
   if (auto p = std::get_if<BoolLitExpr>(&expr.node)) {
      return symTab.getTypeIdByName("BOOL");
   }
   if (auto p = std::get_if<BinaryExpr>(&expr.node)) {
      return expr.resolvedTypeId;
   }
   if (auto p = std::get_if<UnaryExpr>(&expr.node)) {
      return expr.resolvedTypeId;
   }
   if (auto p = std::get_if<CallExpr>(&expr.node)) {
      return expr.resolvedTypeId;
   }
   return expr.resolvedTypeId;
}

/**
 * @brief Check whether a type is a boolean type.
 * @param typeId The TypeId of the type to test
 * @param symTab The symbol table used to fetch the TypeInfo
 * @return true when the type is a BOOL type
 */
bool isBoolType(TypeId typeId, const SymbolTable& symTab)
{
   const TypeInfo* t = symTab.getType(typeId);
   return t && t->isBitType;
}

/**
 * @brief Compute the common type of two arithmetic operands.
 * @details Mirrors the C++ usual arithmetic conversions in simplified form:
 * a real operand promotes to LREAL/REAL, otherwise the wider integer wins.
 * @param left The type of the left operand
 * @param right The type of the right operand
 * @param symTab The symbol table used to look up result types
 * @return The TypeId of the common type, or 0 when an operand is missing
 */
TypeId getArithmeticCommonType(const TypeInfo* left, const TypeInfo* right, const SymbolTable& symTab)
{
   // C++ usual arithmetic conversions simplified
   if (!left || !right) {
      return 0;
   }

   // If either is LREAL/REAL -> LREAL
   if (left->baseType == BaseType::LREAL || right->baseType == BaseType::LREAL) {
      return symTab.getTypeIdByName("LREAL");
   }
   if (left->baseType == BaseType::REAL || right->baseType == BaseType::REAL) {
      return symTab.getTypeIdByName("REAL");
   }

   // Integer: wider type wins
   size_t leftSize = left->sizeInBytes;
   size_t rightSize = right->sizeInBytes;
   if (leftSize >= rightSize) {
      return left->id;
   }
   return right->id;
}

/**
 * @brief Resolve a member access on a struct or function block instance.
 * @details Struct members are looked up in the type symbol's member list; FB
 * locals, methods and parameters are resolved through the FB scope, walking the
 * base-class chain, and interface methods through implemented interfaces.
 * Failures are reported as diagnostics.
 * @param objectType The type of the object being accessed
 * @param member The member name being accessed
 * @param symTab The symbol table used for lookups
 * @param diag The diagnostics collector for errors
 * @param loc The source location of the access
 * @return The MemberResult with the resolved symbol and type
 */
MemberResult checkMemberAccess(
   const TypeInfo& objectType, const std::string& member, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc)
{
   MemberResult result;

   // ENUM: 'TypeName.Value' qualified access resolves one of the enumerators.
   // Enumerators are typed with their enum type and hang off the enum symbol.
   if (objectType.kind == TypeKind::Enum) {
      Symbol* enumSym = symTab.get(objectType.symbolId);
      if (!enumSym) {
         diag.addError(DiagnosticCode::InvalidStructMember, "cannot resolve enum type for member access", loc);
         return result;
      }
      for (SymbolId eId : enumSym->enumerators) {
         Symbol* eSym = symTab.get(eId);
         if (eSym && SymbolTable::normalizeKey(eSym->name) == SymbolTable::normalizeKey(member)) {
            result.symbolId = eId;
            result.typeId = eSym->typeId;
            return result;
         }
      }
      diag.addError(DiagnosticCode::MissingStructMember, "enumerator '" + member + "' not found in " + objectType.name, loc);
      return result;
   }

   if (objectType.kind != TypeKind::Struct && objectType.kind != TypeKind::FunctionBlock) {
      diag.addError(DiagnosticCode::InvalidStructMember, "member access requires struct or function block, got " + objectType.name, loc);
      return result;
   }

   if (objectType.symbolId == 0) {
      diag.addError(DiagnosticCode::InvalidStructMember, "cannot resolve struct type for member access", loc);
      return result;
   }

   Symbol* sym = symTab.get(objectType.symbolId);
   if (!sym) {
      diag.addError(DiagnosticCode::InvalidStructMember, "struct symbol not found", loc);
      return result;
   }

   for (SymbolId memberId : sym->members) {
      Symbol* memberSym = symTab.get(memberId);
      if (memberSym && SymbolTable::normalizeKey(memberSym->name) == SymbolTable::normalizeKey(member)) {
         result.symbolId = memberId;
         result.typeId = memberSym->typeId;
         return result;
      }
   }

   // FUNCTION_BLOCK instances: methods (and local params/vars) live in the FB
   // scope rather than in sym->members. Walk the inheritance chain (base
   // classes expose their members to derived instances) so inherited methods
   // and variables resolve correctly.
   if (objectType.kind == TypeKind::FunctionBlock) {
      Symbol* cur = sym;
      while (cur != nullptr) {
         if (cur->scopeId != 0) {
            const Scope* fbs = symTab.getScope(cur->scopeId);
            if (fbs) {
               for (const auto& [fname, fsymId] : fbs->symbols) {
                  Symbol* fsym = symTab.get(fsymId);
                  if (!fsym || SymbolTable::normalizeKey(fsym->name) != SymbolTable::normalizeKey(member)) {
                     continue;
                  }
                  if (fsym->kind != SymbolKind::Method && fsym->kind != SymbolKind::Variable && fsym->kind != SymbolKind::Parameter
                      && fsym->kind != SymbolKind::StructMember) {
                     continue;
                  }
                  result.symbolId = fsymId;
                  result.typeId = (fsym->kind == SymbolKind::Method) ? fsym->returnTypeId : fsym->typeId;
                  return result;
               }
            }
         }
         if (cur->baseClassId == 0) {
            break;
         }
         cur = symTab.get(cur->baseClassId);
      }
   }

   // FUNCTION_BLOCK instances: methods declared by implemented interfaces.
   // Interfaces keep their methods in sym->members (abstract method symbols).
   if (objectType.kind == TypeKind::FunctionBlock && sym->scopeId != 0) {
      for (SymbolId ifaceId : sym->implementedInterfaces) {
         Symbol* ifaceSym = symTab.get(ifaceId);
         if (!ifaceSym) {
            continue;
         }
         for (SymbolId memberId : ifaceSym->members) {
            Symbol* memberSym = symTab.get(memberId);
            if (memberSym && SymbolTable::normalizeKey(memberSym->name) == SymbolTable::normalizeKey(member)) {
               result.symbolId = memberId;
               result.typeId = (memberSym->kind == SymbolKind::Method) ? memberSym->returnTypeId : memberSym->typeId;
               return result;
            }
         }
      }
   }

   diag.addError(DiagnosticCode::MissingStructMember, "member '" + member + "' not found in " + objectType.name, loc);
   return result;
}

/**
 * @brief Validate an array index access.
 * @details Requires an array object and an integer index type; non-array or
 * non-integer operands are reported as diagnostics.
 * @param arrayType The type of the array being indexed
 * @param indexType The type of the index expression
 * @param symTab The symbol table used for lookups (unused)
 * @param diag The diagnostics collector for errors
 * @param loc The source location of the access
 * @return The IndexResult with the element TypeId
 */
IndexResult checkIndexAccess(
   const TypeInfo& arrayType, const TypeInfo& indexType, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc)
{
   IndexResult result;

   if (arrayType.kind != TypeKind::Array) {
      diag.addError(DiagnosticCode::InvalidArrayIndex, "index access requires array type, got " + arrayType.name, loc);
      return result;
   }

   if (!indexType.isNumeric || indexType.isReal) {
      diag.addError(DiagnosticCode::InvalidArrayIndex, "array index must be an integer type, got " + indexType.name, loc);
      return result;
   }

   result.elementTypeId = arrayType.elementTypeId;
   result.valid = true;
   return result;
}

/**
 * @brief Validate a pointer or reference dereference.
 * @param ptrType The type of the pointer or reference being dereferenced
 * @param symTab The symbol table used for lookups (unused)
 * @param diag The diagnostics collector for errors
 * @param loc The source location of the dereference
 * @return The PointerResult with the pointed-to TypeId
 */
PointerResult checkPointerDereference(const TypeInfo& ptrType, SymbolTable& symTab, Diagnostics& diag, const SourceLocation& loc)
{
   PointerResult result;

   if (ptrType.kind != TypeKind::Pointer && ptrType.kind != TypeKind::Reference) {
      diag.addError(DiagnosticCode::InvalidPointerDereference, "dereference requires pointer or reference type, got " + ptrType.name, loc);
      return result;
   }

   result.pointedTypeId = ptrType.pointedTypeId;
   result.valid = true;
   return result;
}

} // namespace TypeChecker

} // namespace st2cpp::semantic