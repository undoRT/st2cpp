/**
 * @file DependencyOrdering.h
 * @brief Orders function blocks and structs so a type is always defined before use
 * @details C++ needs a complete type before a variable of it can be declared, so
 * a translation unit that declares `FB_A` in terms of `FB_B` must emit B first.
 * Both the function block graph and the struct graph need such an order, and both
 * are built the same way: collect the edges, then topologically sort.
 *
 * Where the semantic analysis already computed a valid order for the blocks, that
 * order wins: it understands inheritance and composition that a name-level scan
 * cannot see, and it is the whole point of running the analysis. The syntactic
 * sort is the fallback for a run without one.
 *
 * Nothing here writes to the collected maps; the component reads them and returns
 * an order, which makes the graph logic testable on its own.
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
#include <unordered_set>
#include <vector>

namespace st2cpp::codegen {

/// A type name mapped to the type names it needs declared first.
using BuildStructDepType = std::unordered_map<std::string, std::unordered_set<std::string>>;

/**
 * @brief The emission order of function blocks and structs.
 */
class DependencyOrdering {
public:
   /**
    * @param fbMap      The function blocks collected from the translation unit
    * @param isFB       Type name to "is a function block", as collected
    * @param structMembers Struct name to its ordered member names
    * @param semantic   Read-only access to the analysis
    * @param identifiers The identifier case policy names are spelled with
    */
   DependencyOrdering(const std::unordered_map<std::string, POU>& fbMap,
                      const std::unordered_map<std::string, bool>& isFB,
                      const std::unordered_map<std::string, std::vector<std::string>>& structMembers,
                      const SemanticBridge& semantic,
                      IdentifierPolicy identifiers)
      : m_fbMap(fbMap), m_isFB(isFB), m_structMembers(structMembers),
        m_semantic(semantic), m_identifiers(identifiers) {}

   /// The function block order the semantic analysis computed, when it has one.
   std::vector<std::string> orderedFbNamesFromSemantic() const;

   /// The dependencies between function blocks, from a syntactic scan.
   std::unordered_map<std::string, std::unordered_set<std::string>>
   buildFBDependencies(const TranslationUnit& tu) const;

   /// Topologically sort a dependency graph, cycles broken by first-seen order.
   std::vector<std::string> topologicalSort(
      const std::unordered_map<std::string, std::unordered_set<std::string>>& dependencies) const;

   /// Whether a struct is used, directly or through a member, as a function block.
   bool structContainsFB(const std::string& structName, const TranslationUnit& tu) const;

   /// Struct dependencies over a whole translation unit.
   BuildStructDepType buildStructDependencies(const TranslationUnit& tu) const;

   /// Struct dependencies over an explicit list of structs.
   BuildStructDepType buildStructDependenciesForStructs(const std::vector<StructType>& structs) const;

   /// Topologically sort the struct graph.
   std::vector<std::string> topologicalSortStructs(const BuildStructDepType& dependencies) const;

   /// Reorder a struct initializer to follow the declaration order of the struct.
   std::vector<StructInitExpr::MemberInit> orderStructMembers(
      const std::vector<StructInitExpr::MemberInit>& members, const std::string& typeName) const;

private:
   /// Recursive worker of structContainsFB, carrying the already-visited set.
   bool structContainsFB(const std::string& structName, const TranslationUnit& tu,
                         std::unordered_set<std::string>& visited) const;

   const std::unordered_map<std::string, POU>& m_fbMap;
   const std::unordered_map<std::string, bool>& m_isFB;
   const std::unordered_map<std::string, std::vector<std::string>>& m_structMembers;
   const SemanticBridge& m_semantic;
   IdentifierPolicy m_identifiers;
};

} // namespace st2cpp::codegen
