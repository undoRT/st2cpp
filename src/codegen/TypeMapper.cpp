/**
 * @file TypeMapper.cpp
 * @brief The bridge from Structured Text types to their C++ spelling
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/TypeMapper.h"
#include "semantic/TypeSystem.h"
#include <algorithm>
#include <unordered_set>

namespace st2cpp::codegen {

using namespace st2cpp::semantic;

/**
 * @brief Map IEC 61131-3 base type to C++ runtime type
 *
 * Converts standard PLC data types to their corresponding
 * C++ runtime library types (e.g., Bool, Int16, UInt32).
 *
 * @param b The base type enumerator
 * @return String representation of the C++ type
 */
std::string TypeMapper::mapBaseType(BaseType b) const
{
   switch (b) {
   case BaseType::BOOL:
      return "Bool";
   case BaseType::SINT:
      return "Int8";
   case BaseType::INT:
      return "Int16";
   case BaseType::DINT:
      return "Int32";
   case BaseType::LINT:
      return "Int64";
   case BaseType::USINT:
      return "UInt8";
   case BaseType::UINT:
      return "UInt16";
   case BaseType::UDINT:
      return "UInt32";
   case BaseType::ULINT:
      return "UInt64";
   case BaseType::REAL:
      return "Float";
   case BaseType::LREAL:
      return "Double";
   case BaseType::BYTE:
      return "UInt8";
   case BaseType::WORD:
      return "UInt16";
   case BaseType::DWORD:
      return "UInt32";
   case BaseType::LWORD:
      return "UInt64";
   case BaseType::STRING:
      return "String";
   case BaseType::WSTRING:
      return "WString";
   case BaseType::TIME:
      return "UInt32";
   case BaseType::DATE:
      return "UInt32";
   case BaseType::DT:
      return "UInt64";
   case BaseType::TOD:
      return "UInt64";
   case BaseType::VOID:
      return "void";
   case BaseType::NAMED:
      return ""; // Handled by caller
   }
   return "";
}

/**
 * @brief Generate STArray template type for array dimensions
 * 
 * Creates nested STArray templates for multidimensional arrays.
 * 
 * @param baseType The base element type (already normalized)
 * @param tr Type reference containing array dimensions
 * @return C++ type string like "STArray<Int16, 0, 10>" or 
 *         "STArray<STArray<Int16, 0, 4>, 1, 3>"
 */
std::string TypeMapper::getArrayType(const std::string& baseType, const TypeRef& tr) const
{
   if (tr.arrayDims.empty()) {
      return baseType;
   }

   // Start from the innermost dimension and build outward
   std::string innerType = baseType;

   // Process dimensions from last to first (innermost to outermost)
   for (auto it = tr.arrayDims.rbegin(); it != tr.arrayDims.rend(); ++it) {
      const auto& dim = *it;

      // Extract low and high bounds from AST literals
      std::string low, high;

      // Handle low bound
      if (auto* lit = std::get_if<LiteralExpr>(&dim.low->node)) {
         low = lit->value;
      } else if (auto* unary = std::get_if<UnaryExpr>(&dim.low->node)) {
         // Handle negative numbers: -2
         if (unary->op == "-") {
            if (auto* lit = std::get_if<LiteralExpr>(&unary->operand->node)) {
               low = "-" + lit->value;
            }
         }
      }

      // Handle high bound
      if (auto* lit = std::get_if<LiteralExpr>(&dim.high->node)) {
         high = lit->value;
      } else if (auto* unary = std::get_if<UnaryExpr>(&dim.high->node)) {
         if (unary->op == "-") {
            if (auto* lit = std::get_if<LiteralExpr>(&unary->operand->node)) {
               high = "-" + lit->value;
            }
         }
      }

      // Wrap the current inner type with another STArray
      innerType = "STArray<" + innerType + ", " + low + ", " + high + ">";
   }

   return innerType;
}

/**
 * @brief Map a TypeRef to its complete C++ type string
 *
 * Handles all type modifiers: named types, base types,
 * pointers, references, and array dimensions.
 *
 * @param tr Type reference to map
 * @return Complete C++ type string
 */
std::string TypeMapper::mapType(const TypeRef& tr) const
{
   std::string base;
   if (tr.base == BaseType::NAMED) {
      base = normalizeType(tr.name);
      // Semantic: resolve a project/symbol-table type to its C++ spelling.
      // External structs/enums/FBs resolve through their descriptor binding
      // (e.g. examplelib::Channel); local and primitive names are unchanged.
      if (const auto* st = m_semantic.semanticSymTab()) {
         const st2cpp::semantic::TypeId tid = st->getTypeIdByName(tr.name);
         if (tid != 0) {
            const st2cpp::semantic::TypeInfo* t = st->getType(tid);
            // Named alias (TYPE Name : <type>; END_TYPE): the name resolves to
            // a canonical type carrying a DIFFERENT name. Encode the canonical
            // C++ type instead of the alias spelling, so aliases of arrays,
            // pointers and scalars all emit the correct C++ declaration.
            if (t != nullptr && st->normalizeKey(t->name) != st->normalizeKey(tr.name)) {
               return mapTypeId(tid);
            }
            base = m_semantic.semanticTypeCppName(tid, base);
         }
      }
      // Legacy alias resolution (no SemanticInfo attached): chase the alias
      // chain in m_aliasTypes when the loop above could not help.
      const std::string aliased = resolveAliasLegacy(base);
      if (!aliased.empty()) {
         base = aliased;
      }
   } else {
      base = mapBaseType(tr.base);
   }

   if (tr.isPointer) {
      return base + "*";
   }
   if (tr.isRefTo) {
      return base + "&";
   }
   if (!tr.arrayDims.empty()) {
      return getArrayType(base, tr);
   }
   return base;
}

/**
 * @brief Get only the base type name without pointers or references
 *
 * Strips away '*', '&', and array decorations to return just
 * the underlying type name.
 *
 * @param tr Type reference to analyze
 * @return Base type name as string
 */
std::string TypeMapper::getBaseTypeName(const TypeRef& tr) const
{
   if (tr.base == BaseType::NAMED) {
      return tr.name;
   }
   return mapBaseType(tr.base);
}

/**
 * @param calleeName
 */
std::string TypeMapper::getBaseFBName(const std::string& calleeName) const
{
   // If it contains "[" it is an arratay access: extract the name before the parenthesis
   size_t bracketPos = calleeName.find('[');
   if (bracketPos != std::string::npos) {
      return calleeName.substr(0, bracketPos);
   }
   return calleeName;
}

/**
 * @brief Check if a type reference represents void
 * @param tr Type reference to check
 * @return true if the type is void
 */
bool TypeMapper::isVoidType(const TypeRef& tr) const
{
   if (tr.base == BaseType::VOID) {
      return true;
   }
   if (tr.base == BaseType::NAMED && !tr.name.empty()) {
      std::string typeName = normalizeType(tr.name);
      std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
      if (typeName == "void") {
         return true;
      }
   }
   return false;
}

/**
 * @brief Map a semantic TypeId to its complete C++ type string
 *
 * Central semantic type mapper: resolves the C++ spelling from the canonical
 * TypeInfo instead of re-deriving it from the syntactic TypeRef.
 *
 * @param typeId Canonical type id from the semantic analysis
 * @return Complete C++ type string (empty if the id is not decodable)
 */
std::string TypeMapper::mapTypeId(st2cpp::semantic::TypeId typeId) const
{
   const auto* st = m_semantic.semanticSymTab();
   if (!st) {
      return "";
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   if (!t) {
      return "";
   }

   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
      return mapBaseType(t->baseType);
   case st2cpp::semantic::TypeKind::Array: {
      std::string elem = mapTypeId(t->elementTypeId);
      std::string inner = elem.empty() ? normalizeType(t->name) : elem;
      for (auto it = t->dimensions.rbegin(); it != t->dimensions.rend(); ++it) {
         inner = "STArray<" + inner + ", " + std::to_string(it->low) + ", " + std::to_string(it->high) + ">";
      }
      return inner;
   }
   case st2cpp::semantic::TypeKind::Pointer: {
      std::string inner = mapTypeId(t->pointedTypeId);
      if (inner.empty() && t->pointedTypeId != 0) {
         const st2cpp::semantic::TypeInfo* pointed = st->getType(t->pointedTypeId);
         inner = pointed ? normalizeType(pointed->name) : "";
      }
      return inner.empty() ? normalizeType(t->name) : (inner + "*");
   }
   case st2cpp::semantic::TypeKind::Reference: {
      std::string inner = mapTypeId(t->pointedTypeId);
      if (inner.empty() && t->pointedTypeId != 0) {
         const st2cpp::semantic::TypeInfo* pointed = st->getType(t->pointedTypeId);
         inner = pointed ? normalizeType(pointed->name) : "";
      }
      return inner + "&";
   }
   case st2cpp::semantic::TypeKind::Struct:
   case st2cpp::semantic::TypeKind::Enum:
   case st2cpp::semantic::TypeKind::FunctionBlock: {
      // External-library types resolve to their descriptor C++ binding
      // (e.g. examplelib::State); local types keep the ST spelling.
      return m_semantic.semanticTypeCppName(typeId, normalizeType(t->name));
   }
   case st2cpp::semantic::TypeKind::Interface:
      return normalizeType(t->name);
   case st2cpp::semantic::TypeKind::Void:
      return "void";
   case st2cpp::semantic::TypeKind::Unknown:
   default:
      if (t->baseType != BaseType::VOID && (t->isNumeric || t->isBitType)) {
         return mapBaseType(t->baseType);
      }
      return normalizeType(t->name);
   }
}

/**
 * @brief Register named type aliases so mapType can resolve them.
 * @details This is the legacy (no-SemanticInfo) path: each alias is recorded
 * with its underlying TypeRef. Aliases that map to another alias are chased
 * lazily by resolveAliasLegacy on a per-use basis, so declaration order and
 * in-order chains both work from the caller's perspective.
 * @param aliases The TYPE aliases declared in the translation unit
 */
void TypeMapper::registerTypeAliases(const std::vector<TypeAlias>& aliases)
{
   m_aliasTypes.clear();
   for (const auto& alias : aliases) {
      m_aliasTypes[normalizeType(alias.name)] = alias.type;
   }
}

/**
 * @brief Resolve an alias name to its underlying C++ type (legacy mapType path).
 * @details Follows alias-of-alias chains through m_aliasTypes with a cycle
 * guard; a chain target that is itself an alias keeps the lookup alive, and
 * anything else is handed to mapType. Returns "" when the name is not a known
 * alias, so callers can fall back to the plain name.
 * @param name The (normalized) alias name to resolve
 * @return The resolved C++ type, or "" when not an alias / cyclic / broken
 */
std::string TypeMapper::resolveAliasLegacy(const std::string& name) const
{
   std::unordered_set<std::string> guard;
   std::string cur = name;

   for (int depth = 0; depth < 64; ++depth) {
      auto it = m_aliasTypes.find(cur);
      if (it == m_aliasTypes.end()) {
         return "";
      }
      if (!guard.insert(cur).second) {
         return ""; // cycle
      }
      const TypeRef& underlying = it->second;
      if (underlying.base == BaseType::NAMED && underlying.arrayDims.empty() && !underlying.isPointer && !underlying.isRefTo) {
         // Alias of another name → chase the chain.
         const std::string next = normalizeType(underlying.name);
         if (next == cur || m_aliasTypes.find(next) == m_aliasTypes.end()) {
            return mapType(underlying); // target is a real type, not an alias
         }
         cur = next;
         continue;
      }
      return mapType(underlying);
   }
   return "";
}

/**
 * @brief Decide logical vs bitwise operators using the decorated resolvedTypeId
 * @param typeId Resolved type of an operand
 * @return true only when the type is the canonical BOOL
 */
bool TypeMapper::isSemanticBoolType(st2cpp::semantic::TypeId typeId) const
{
   const auto* st = m_semantic.semanticSymTab();
   if (!st) {
      return false;
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   return t != nullptr && t->isBitType;
}

/**
 * @brief Check if a resolved type is an ENUM (member access must use '::' not '.')
 */
bool TypeMapper::isSemanticEnumType(st2cpp::semantic::TypeId typeId) const
{
   const auto* st = m_semantic.semanticSymTab();
   if (!st) {
      return false;
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   return t != nullptr && t->kind == st2cpp::semantic::TypeKind::Enum;
}

/**
 * @brief Emit an explicit static_cast for RHS of an assignment/RETURN when the
 *        semantic types of lhs and rhs differ.
 *
 * The semantic analysis already validated the assignment; the runtime types
 * are C++ primitives, so the cast only makes the intended conversion explicit
 * (IEC 61131-3 allows widening between numerics). No cast is emitted when the
 * mode is disabled, types match, or the target is not numeric elementary.
 *
 * @param lhs     LHS expression (decorated)
 * @param rhs     RHS expression (decorated)
 * @param rhsCode Already generated C++ text of the RHS
 * @return RHS text, possibly wrapped in static_cast
 */
std::string TypeMapper::applySemanticAssignmentCast(const Expr& lhs, const Expr& rhs, const std::string& rhsCode) const
{
   if (!m_semantic.semanticAvailable()) {
      return rhsCode;
   }
   st2cpp::semantic::TypeId lhsId = lhs.resolvedTypeId;
   st2cpp::semantic::TypeId rhsId = rhs.resolvedTypeId;
   if (lhsId == 0 || rhsId == 0 || lhsId == rhsId) {
      return rhsCode;
   }
   const auto* st = m_semantic.semanticSymTab();
   if (!st) {
      return rhsCode;
   }
   const st2cpp::semantic::TypeInfo* lhsType = st->getType(lhsId);
   if (!lhsType || lhsType->kind != st2cpp::semantic::TypeKind::Elementary || !lhsType->isNumeric) {
      return rhsCode;
   }
   std::string target = mapTypeId(lhsId);
   if (target.empty()) {
      return rhsCode;
   }
   return "static_cast<" + target + ">(" + rhsCode + ")";
}

/**
 * @brief Normalize a string based on case sensitivity setting
 *
 * If case-sensitive mode is enabled, returns the string unchanged.
 * Otherwise, converts the entire string to uppercase.
 * Used as the base normalization function for all identifiers.
 *
 * @param str String to normalize
 * @return Normalized string
 */
std::string TypeMapper::normalize(const std::string& str) const
{
   if (m_identifiers.caseSensitive) {
      return str;
   }
   std::string result = str;
   std::transform(result.begin(), result.end(), result.begin(), ::toupper);
   return result;
}

/**
 * @brief Normalize a type name
 *
 * Special handling for type names: base runtime types (Bool, Int16, etc.)
 * preserve their case, while user-defined types (structs, enums) are
 * normalized according to the case sensitivity setting.
 *
 * @param str Type name to normalize
 * @return Normalized type name
 */
std::string TypeMapper::normalizeType(const std::string& str) const
{
   // The runtime aliases are spelled in mixed case (Int16, Float, ...) and the
   // generated C++ already refers to them by that spelling, so the identifier
   // policy must not fold them; only user-defined names follow the policy.
   static const std::unordered_set<std::string> baseTypes
      = {"Bool", "Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Double", "String", "WString", "Void"};

   if (baseTypes.find(str) != baseTypes.end()) {
      return str;
   }

   return m_identifiers.type(str);
}

/**
 * @brief Normalize an identifier (variable, member, function name)
 *
 * Applies standard normalization to identifiers based on the
 * case sensitivity setting. Most PLCs are case-insensitive,
 * so identifiers are typically converted to uppercase.
 *
 * @param str Identifier to normalize
 * @return Normalized identifier
 */
std::string TypeMapper::normalizeIdent(const std::string& str) const
{
   return m_identifiers.ident(str);
}

/**
 * @brief Get the size of a type in bytes
 *
 * @param tr Type reference
 * @return int Size in bytes
 */
int TypeMapper::getTypeSizeInBytes(const TypeRef& tr) const
{
   // If it's an array, calculate total size
   if (!tr.arrayDims.empty()) {
      TypeRef elemType = tr;
      elemType.arrayDims.clear();
      int elemSize = getTypeSizeInBytes(elemType);
      int totalSize = 1;
      for (const auto& dim : tr.arrayDims) {
         // Calculate dimension size: high - low + 1
         int low = 0, high = 0;
         if (auto* lit = std::get_if<LiteralExpr>(&dim.low->node)) {
            low = std::stoi(lit->value);
         } else if (auto* unary = std::get_if<UnaryExpr>(&dim.low->node)) {
            auto* lit = std::get_if<LiteralExpr>(&unary->operand->node);
            if (unary->op == "-" && lit) {
               low = -std::stoi(lit->value);
            }
         }
         if (auto* lit = std::get_if<LiteralExpr>(&dim.high->node)) {
            high = std::stoi(lit->value);
         } else if (auto* unary = std::get_if<UnaryExpr>(&dim.high->node)) {
            auto* lit = std::get_if<LiteralExpr>(&unary->operand->node);
            if (unary->op == "-" && lit) {
               high = -std::stoi(lit->value);
            }
         }
         totalSize *= (high - low + 1);
      }
      return elemSize * totalSize;
   }

   // Base types
   switch (tr.base) {
   case BaseType::BOOL:
      return 1;
   case BaseType::SINT:
      return 1;
   case BaseType::INT:
      return 2;
   case BaseType::DINT:
      return 4;
   case BaseType::LINT:
      return 8;
   case BaseType::USINT:
      return 1;
   case BaseType::UINT:
      return 2;
   case BaseType::UDINT:
      return 4;
   case BaseType::ULINT:
      return 8;
   case BaseType::REAL:
      return 4;
   case BaseType::LREAL:
      return 8;
   case BaseType::BYTE:
      return 1;
   case BaseType::WORD:
      return 2;
   case BaseType::DWORD:
      return 4;
   case BaseType::LWORD:
      return 8;
   case BaseType::TIME:
      return 4;
   case BaseType::DATE:
      return 4;
   case BaseType::DT:
      return 8;
   case BaseType::TOD:
      return 8;
   case BaseType::STRING:
      return tr.stringLen.value_or(80);
   case BaseType::WSTRING:
      return tr.stringLen.value_or(80) * 2;
   case BaseType::NAMED: {
      // User-defined type - default to 1 (should be overridden)
      return 1;
   }
   case BaseType::VOID:
      return 0;
   default:
      return 1;
   }
}

/**
 * @brief Get the alignment requirement of a type in bytes
 *
 * @param tr Type reference
 * @return int Alignment in bytes
 */
int TypeMapper::getTypeAlignment(const TypeRef& tr) const
{
   // For arrays, alignment is that of the element
   if (!tr.arrayDims.empty()) {
      TypeRef elemType = tr;
      elemType.arrayDims.clear();
      return getTypeAlignment(elemType);
   }

   switch (tr.base) {
   case BaseType::BOOL:
      return 1;
   case BaseType::SINT:
      return 1;
   case BaseType::USINT:
      return 1;
   case BaseType::BYTE:
      return 1;
   case BaseType::INT:
      return 2;
   case BaseType::UINT:
      return 2;
   case BaseType::WORD:
      return 2;
   case BaseType::DINT:
      return 4;
   case BaseType::UDINT:
      return 4;
   case BaseType::DWORD:
      return 4;
   case BaseType::REAL:
      return 4;
   case BaseType::LINT:
      return 8;
   case BaseType::ULINT:
      return 8;
   case BaseType::LWORD:
      return 8;
   case BaseType::LREAL:
      return 8;
   case BaseType::TIME:
      return 4;
   case BaseType::DATE:
      return 4;
   case BaseType::DT:
      return 8;
   case BaseType::TOD:
      return 8;
   case BaseType::STRING:
      return 1;
   case BaseType::WSTRING:
      return 2;
   case BaseType::NAMED:
      return 1; // Default for user-defined types
   case BaseType::VOID:
      return 0;
   default:
      return 1;
   }
}

} // namespace st2cpp::codegen
