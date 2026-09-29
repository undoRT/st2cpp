/**
 * @file SemanticBridge.h
 * @brief Read-only queries over the semantic analysis, for the code generator
 * @details Everything the code generator needs to know from the semantic
 * analysis lives here: the symbol table, the C++ spelling of a type or an
 * enumerator, the instance a call targets, the signature an external function
 * block exposes, and the diagnostic raised when a descriptor's C++ binding is
 * incomplete.
 *
 * The bridge only reads. It never touches the generation state (the output
 * streams, the indent, the scope stack), which is what lets the code generator
 * stay a thin orchestrator and lets this component be reasoned about on its own.
 * Its one input is the IdentifierPolicy, so the spelling it produces always
 * agrees with the rest of the pipeline.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "codegen/CodegenTypes.h"
#include "codegen/IdentifierPolicy.h"
#include "ast/AST.h"
#include "library/LibraryDescriptor.h"
#include "semantic/SemanticInfo.h"
#include "semantic/SymbolTable.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace st2cpp::codegen {

/**
 * @brief Read-only access to the semantic analysis for the code generator.
 */
class SemanticBridge {
public:
   /**
    * @brief Bind the bridge to a semantic analysis result.
    * @param info The analysis to query; may be null, in which case every query
    *        degrades to the legacy syntactic inference
    * @param identifiers The identifier case policy to spell results with
    */
   SemanticBridge(st2cpp::semantic::SemanticInfo* info, IdentifierPolicy identifiers)
      : m_semanticInfo(info), m_identifiers(identifiers) {}

   /// Whether a semantic analysis carrying a symbol table is attached.
   bool semanticAvailable() const;

   /// The analyzed symbol table, or null when none is attached.
   const st2cpp::semantic::SymbolTable* semanticSymTab() const;

   /// The library descriptor owning an external symbol, when resolvable.
   const st2cpp::library::LibraryDescriptor* semanticDescriptorFor(const st2cpp::semantic::Symbol& sym) const;

   /// Report an external symbol whose descriptor C++ binding is incomplete.
   void reportBindingIncomplete(const std::string& entity, const std::string& what) const;

   /// C++ spelling of a semantic type, resolving external types to their binding.
   std::string semanticTypeCppName(st2cpp::semantic::TypeId typeId, const std::string& fallback) const;

   /// Qualified enum name owning an enumerator symbol.
   std::string semanticEnumNameForEnumerator(st2cpp::semantic::SymbolId enumeratorId) const;

   /// C++ spelling of an enumerator, using the descriptor's own spelling.
   std::string semanticEnumeratorCppName(st2cpp::semantic::TypeId enumTypeId, const std::string& stMemberName) const;

   /// The C++ callable a call expression targets (function, or owner::method).
   std::string semanticCallTargetName(const CallExpr& call) const;

   /// The C++ accessor a variable reference binds to, when it has one.
   std::string semanticVariableBinding(const st2cpp::semantic::Symbol& sym) const;

   /// External FB step info for an FB invocation statement.
   ExternalFbCallInfo semanticFbCallInfo(const CallExpr& call) const;

   /// The call interface of an external function block, recovered from symbols.
   std::optional<st2cpp::codegen::FunctionSignature> semanticSignatureForCall(const CallExpr& call) const;

   /// The function block symbol a declared POU resolves to.
   st2cpp::semantic::SymbolId semanticFbSymbolId(const POU& pou) const;

   /// The declared base of a function block, as a C++ type name.
   std::string semanticBaseForFb(const POU& pou) const;

   /**
    * @brief The identifier to emit for a reference the analyzer already resolved.
    * @details Under --caseSensitive a mis-cased reference that permissive mode
    * still resolved must be emitted with the declaration's own spelling, or the
    * generated C++ would name something no declaration introduced.
    */
   std::string declaredIdent(const std::string& written, st2cpp::semantic::SymbolId symbolId) const;

   /// The function block order the analysis computed, honouring inheritance.
   const std::vector<st2cpp::semantic::SymbolId>& fbTopoOrder() const;

   /// The attached analysis, or null when none is. Callers reach the library
   /// registry through it, which is where the descriptor of a used library lives.
   st2cpp::semantic::SemanticInfo* info() const { return m_semanticInfo; }

   /// The identifier case policy results are spelled with.
   const IdentifierPolicy& identifiers() const { return m_identifiers; }

private:
   // Not const-qualified on the pointee: reportBindingIncomplete() appends to
   // the diagnostics, which is the one deliberate write the bridge performs.
   st2cpp::semantic::SemanticInfo* m_semanticInfo;
   IdentifierPolicy m_identifiers;
};

} // namespace st2cpp::codegen
