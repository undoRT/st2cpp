/**
 * @file TypeMapper.h
 * @brief The bridge from Structured Text types to their C++ spelling
 * @details Everything the generator knows about a type is decided here: the C++
 * name of an elementary type, the qualified name of a user-defined one, the way
 * an array or a pointer decorates it, and the size and alignment a plain old
 * value takes.
 *
 * A named type may hide behind an alias, so the mapper keeps the alias table
 * and chases it to the canonical type. The identifier case policy is part of
 * the input, not of the state, so a mapper and the components that use it can
 * never spell the same type two different ways.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include "codegen/IdentifierPolicy.h"
#include "codegen/SemanticBridge.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace st2cpp::codegen {

/**
 * @brief The Structured Text to C++ type mapping.
 */
class TypeMapper
{
public:
   /**
    * @param semantic    Read-only access to the analysis, for the qualified
    *                    name of a type an external library exports
    * @param aliases     The alias table (TYPE Name : <type>; END_TYPE)
    * @param identifiers The identifier case policy names are spelled with
    */
   TypeMapper(const SemanticBridge& semantic, std::unordered_map<std::string, TypeRef>& aliases, IdentifierPolicy identifiers)
      : m_semantic(semantic), m_aliasTypes(aliases), m_identifiers(identifiers)
   {}

   // ===== elementary and named types =====
   std::string mapBaseType(BaseType base) const;
   std::string mapType(const TypeRef& tr) const;
   std::string mapTypeId(st2cpp::semantic::TypeId typeId) const;
   std::string getBaseTypeName(const TypeRef& tr) const;
   std::string getBaseFBName(const std::string& calleeName) const;
   bool isVoidType(const TypeRef& tr) const;

   // ===== arrays, pointers, references =====
   std::string getArrayType(const std::string& base, const TypeRef& tr) const;

   // ===== aliases =====
   void registerTypeAliases(const std::vector<TypeAlias>& aliases);
   std::string resolveAliasLegacy(const std::string& typeName) const;

   // ===== layout =====
   int getTypeSizeInBytes(const TypeRef& tr) const;
   int getTypeAlignment(const TypeRef& tr) const;

   // ===== semantic queries used for assignment casts =====
   bool isSemanticBoolType(st2cpp::semantic::TypeId typeId) const;
   bool isSemanticEnumType(st2cpp::semantic::TypeId typeId) const;
   std::string applySemanticAssignmentCast(const Expr& lhs, const Expr& rhs, const std::string& rhsCode) const;

   // ===== identifier spelling, kept for API compatibility with the forwarders =====
   std::string normalize(const std::string& str) const;
   std::string normalizeIdent(const std::string& str) const;
   std::string normalizeType(const std::string& str) const;

   /// The identifier case policy the mapper was built with.
   const IdentifierPolicy& identifiers() const { return m_identifiers; }

private:
   const SemanticBridge& m_semantic;
   std::unordered_map<std::string, TypeRef>& m_aliasTypes;
   IdentifierPolicy m_identifiers;
};

} // namespace st2cpp::codegen
