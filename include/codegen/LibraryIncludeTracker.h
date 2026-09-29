/**
 * @file LibraryIncludeTracker.h
 * @brief Decides which external library headers the generated C++ must include
 * @details A translation unit only pays for the libraries it actually mentions.
 * The tracker walks the AST and the symbol table once, records every library a
 * type, symbol or member resolves to, and turns that set into the include lines
 * to emit, deduplicated and in a stable order.
 *
 * The order is not incidental: it is the library load order, so two runs over
 * the same workspace always produce the same header. Two libraries exporting
 * the same symbol are resolved earlier, by the semantic bridge, and only the
 * winner is ever recorded here.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include "semantic/SymbolTable.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace st2cpp {
namespace codegen {

class SemanticBridge;

/**
 * @brief Collects the external libraries a translation unit depends on.
 */
class LibraryIncludeTracker {
public:
   /**
    * @param semantic Read-only access to the analysis that resolves a name to
    *        the library owning it
    */
   explicit LibraryIncludeTracker(const SemanticBridge& semantic) : m_semantic(semantic) {}

   /**
    * @brief Pre-scan the TU and compute the sorted, deduplicated include lines.
    * @param tu The translation unit about to be generated
    */
   void computeLibraryIncludeLines(const TranslationUnit& tu);

   /**
    * @brief The include block for the libraries the TU uses.
    * @return The lines to emit, or an empty string when it uses none
    */
   std::string libraryIncludeBlock() const;

   /**
    * @brief The include lines as computed by the last pre-scan.
    * @details Callers use it to decide whether the include block is worth
    * emitting at all, so the emptiness check and the block never disagree.
    */
   const std::vector<std::string>& includeLines() const { return m_libraryIncludeLines; }

   void collectUsedLibrariesFromExpr(const Expr& expr, std::unordered_set<std::string>& used) const;
   void collectUsedLibrariesFromStmt(const Stmt& stmt, std::unordered_set<std::string>& used) const;
   void collectUsedLibrariesFromTypeRef(const TypeRef& tr, std::unordered_set<std::string>& used) const;
   void recordUsedLibraryForSymbol(const st2cpp::semantic::Symbol& sym,
                                   std::unordered_set<std::string>& used) const;
   void recordUsedLibraryForTypeId(st2cpp::semantic::TypeId typeId,
                                   std::unordered_set<std::string>& used) const;

private:
   const SemanticBridge& m_semantic;
   /// The include lines, in emission order. Replaced wholesale by each pre-scan.
   std::vector<std::string> m_libraryIncludeLines;
};

} // namespace codegen
} // namespace st2cpp
