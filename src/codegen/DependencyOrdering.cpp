/**
 * @file DependencyOrdering.cpp
 * @brief Orders function blocks and structs so a type is always defined before use
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/DependencyOrdering.h"
#include "semantic/TypeSystem.h"
#include <queue>

namespace st2cpp::codegen {

using namespace st2cpp::semantic;

/**
 * @brief Build dependency graph for a specific list of structs
 * @param structs List of structs
 * @return Map of struct name -> set of struct names it depends on
 */
BuildStructDepType DependencyOrdering::buildStructDependenciesForStructs(const std::vector<StructType>& structs) const
{
   BuildStructDepType deps;

   // First, build a set of all struct names in this list for quick lookup
   std::unordered_set<std::string> structNames;
   for (const auto& st : structs) {
      structNames.insert(m_identifiers.type(st.name));
   }

   for (const auto& st : structs) {
      std::string structName = m_identifiers.type(st.name);
      deps[structName]; // Ensure entry exists

      for (const auto& member : st.members) {
         if (member.type.base == BaseType::NAMED) {
            std::string memberType = m_identifiers.type(member.type.name);
            // Only consider dependencies that are structs in our list
            if (structNames.find(memberType) != structNames.end() && memberType != structName) {
               deps[structName].insert(memberType);
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Map the semantic fbTopoOrder (SymbolIds) to codegen FB names
 * 
 * Only names of FB symbols that exist in the current m_fbMap are kept; names
 * are normalized exactly like the legacy path (normalizeType). The semantic
 * order already accounts for inheritance and composition dependencies.
 */
std::vector<std::string> DependencyOrdering::orderedFbNamesFromSemantic() const
{
   std::vector<std::string> names;
   if (!m_semantic.semanticAvailable() || m_semantic.info()->empty()) {
      return names;
   }
   const st2cpp::semantic::SymbolTable* st = m_semantic.semanticSymTab();
   if (!st) {
      return names;
   }
   for (st2cpp::semantic::SymbolId id : m_semantic.fbTopoOrder()) {
      const st2cpp::semantic::Symbol* sym = st->get(id);
      if (sym && sym->kind == st2cpp::semantic::SymbolKind::FunctionBlock) {
         std::string n = m_identifiers.type(sym->name);
         if (m_fbMap.count(n)) {
            names.push_back(n);
         }
      }
   }
   return names;
}

/**
 * @brief Build dependency graph between function blocks
 * @param tu Translation unit
 * @return Map of FB name -> set of FB names it depends on
 */
std::unordered_map<std::string, std::unordered_set<std::string>> DependencyOrdering::buildFBDependencies(const TranslationUnit& tu) const
{
   std::unordered_map<std::string, std::unordered_set<std::string>> deps;

   for (const auto& pou : tu.pous) {
      if (pou.kind != POUKind::FUNCTION_BLOCK) {
         continue;
      }

      std::string fbName = m_identifiers.type(pou.name);
      deps[fbName]; // Ensure entry exists

      // Check all variable sections for FB-typed members
      for (const auto& sec : pou.varSections) {
         for (const auto& decl : sec.decls) {
            if (decl.type.base == BaseType::NAMED) {
               std::string typeName = m_identifiers.type(decl.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  deps[fbName].insert(typeName);
               }
            }
         }
      }

      // Also check method parameters
      for (const auto& method : pou.methods) {
         for (const auto& param : method.parameters) {
            if (param.type.base == BaseType::NAMED) {
               std::string typeName = m_identifiers.type(param.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  deps[fbName].insert(typeName);
               }
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Topological sort of function blocks (Kahn's algorithm)
 * @param dependencies Dependency map
 * @return Ordered list of FB names (no cycles)
 * @throws std::runtime_error if circular dependency detected
 */
std::vector<std::string> DependencyOrdering::topologicalSort(
   const std::unordered_map<std::string, std::unordered_set<std::string>>& dependencies) const
{
   std::unordered_map<std::string, int> inDegree;
   std::unordered_map<std::string, std::unordered_set<std::string>> adjList;

   // Initialize
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = 0;
      adjList[node] = deps;
   }

   // Calculate in-degree (number of nodes that depend on this node)
   // Wait - we need reverse: which nodes depend on this node?
   // Actually, for Kahn's algorithm, we need edges: u -> v means u depends on v
   // So we build adjacency from v (dependency) to u (dependent)

   std::unordered_map<std::string, std::unordered_set<std::string>> reverseAdj;
   for (const auto& [node, deps] : dependencies) {
      for (const auto& dep : deps) {
         reverseAdj[dep].insert(node);
      }
   }

   // Calculate in-degree (number of dependencies this node has)
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = deps.size();
   }

   // Queue for nodes with zero in-degree
   std::queue<std::string> q;
   for (const auto& [node, degree] : inDegree) {
      if (degree == 0) {
         q.push(node);
      }
   }

   std::vector<std::string> result;
   while (!q.empty()) {
      std::string node = q.front();
      q.pop();
      result.push_back(node);

      // Decrease in-degree of nodes that depend on this node
      for (const auto& dependent : reverseAdj[node]) {
         inDegree[dependent]--;
         if (inDegree[dependent] == 0) {
            q.push(dependent);
         }
      }
   }

   // Check for cycles
   if (result.size() != dependencies.size()) {
      throw std::runtime_error("Circular dependency detected among function blocks!");
   }

   return result;
}

/**
 * @brief Check if a struct type contains any FB (directly or indirectly)
 * @param structName Name of the struct to check
 * @param tu Translation unit for lookup
 * @return true if the struct contains any FB (directly or nested)
 */
bool DependencyOrdering::structContainsFB(const std::string& structName, const TranslationUnit& tu) const
{
   std::unordered_set<std::string> visited;
   return structContainsFB(structName, tu, visited);
}

bool DependencyOrdering::structContainsFB(const std::string& structName,
                                          const TranslationUnit& tu,
                                          std::unordered_set<std::string>& visited) const
{
   if (!visited.insert(structName).second) {
      return false;
   }

   // Find the struct definition
   const StructType* targetStruct = nullptr;
   for (const auto& st : tu.structs) {
      if (m_identifiers.type(st.name) == structName) {
         targetStruct = &st;
         break;
      }
   }

   if (!targetStruct) {
      return false;
   }

   // Check each member
   for (const auto& member : targetStruct->members) {
      if (member.type.base == BaseType::NAMED) {
         std::string memberType = m_identifiers.type(member.type.name);

         // Direct FB
         if (m_isFB.find(memberType) != m_isFB.end()) {
            return true;
         }

         // Recursive check: is this member a struct that contains FB?
         if (structContainsFB(memberType, tu, visited)) {
            return true;
         }
      }
   }

   return false;
}

/**
 * @brief Build dependency graph between structs
 * @param tu Translation unit  
 * @return Map of struct name -> set of struct names it depends on
 */
BuildStructDepType DependencyOrdering::buildStructDependencies(const TranslationUnit& tu) const
{
   BuildStructDepType deps;

   for (const auto& st : tu.structs) {
      std::string structName = m_identifiers.type(st.name);
      deps[structName]; // Ensure entry exists

      for (const auto& member : st.members) {
         if (member.type.base == BaseType::NAMED) {
            std::string memberType = m_identifiers.type(member.type.name);
            // Check if member type is another struct
            bool isStruct = false;
            for (const auto& otherStruct : tu.structs) {
               if (m_identifiers.type(otherStruct.name) == memberType) {
                  isStruct = true;
                  break;
               }
            }
            if (isStruct && memberType != structName) {
               deps[structName].insert(memberType);
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Topological sort of structs (Kahn's algorithm)
 * @param dependencies Dependency map (struct -> set of structs it depends on)
 * @return Ordered list of struct names (no cycles)
 * @throws std::runtime_error if circular dependency detected
 */
std::vector<std::string> DependencyOrdering::topologicalSortStructs(const BuildStructDepType& dependencies) const
{
   std::unordered_map<std::string, int> inDegree;
   BuildStructDepType reverseAdj;

   // Initialize
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = deps.size();
      for (const auto& dep : deps) {
         reverseAdj[dep].insert(node);
      }
   }

   // Queue for nodes with zero in-degree
   std::queue<std::string> q;
   for (const auto& [node, degree] : inDegree) {
      if (degree == 0) {
         q.push(node);
      }
   }

   std::vector<std::string> result;
   while (!q.empty()) {
      std::string node = q.front();
      q.pop();
      result.push_back(node);

      for (const auto& dependent : reverseAdj[node]) {
         inDegree[dependent]--;
         if (inDegree[dependent] == 0) {
            q.push(dependent);
         }
      }
   }

   // Check for cycles
   if (result.size() != dependencies.size()) {
      throw std::runtime_error("Circular dependency detected among structs!");
   }

   return result;
}

/**
 * @brief Ordina i membri di una StructInitExpr secondo l'ordine di dichiarazione della struct
 * @param members Lista di membri (membro -> valore)
 * @param structName Nome della struct (normalizzato)
 * @return Lista di membri ordinata
 */
std::vector<StructInitExpr::MemberInit> DependencyOrdering::orderStructMembers(const std::vector<StructInitExpr::MemberInit>& members,
                                                                               const std::string& structName) const
{
   auto it = m_structMembers.find(structName);
   if (it == m_structMembers.end()) {
      // Struct non trovata, mantenere l'ordine originale
      return members;
   }
   const auto& orderedMemberNames = it->second;

   // Crea una mappa per accesso rapido
   std::unordered_map<std::string, StructInitExpr::MemberInit> memberMap;
   for (const auto& m : members) {
      memberMap[m_identifiers.ident(m.member)] = m;
   }

   std::vector<StructInitExpr::MemberInit> result;
   for (const auto& name : orderedMemberNames) {
      auto found = memberMap.find(name);
      if (found != memberMap.end()) {
         result.push_back(found->second);
      }
   }
   return result;
}

} // namespace st2cpp::codegen
