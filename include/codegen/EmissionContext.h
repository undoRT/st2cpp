/**
 * @file EmissionContext.h
 * @brief Shared state of a code generation run
 * @details The emitter for declarations, the emitter for expressions and
 * statements, and the emitter that assembles the files all read and write the
 * same state: the two output streams, the indent, the scope stack, the maps
 * collected while walking the translation unit, and the collaborators built
 * from the semantic analysis. That state lives here so a single object owns it
 * and the emitters only borrow a reference to it.
 *
 * `EmissionContext` also carries the small queries the emitters make over its
 * collaborators (type spelling, semantic bindings, dependency ordering,
 * library includes) and the indent helpers, so an emitter never has to reach
 * through the context into a collaborator by hand.
 *
 * It holds no emitter logic: it never emits a line. That is what keeps the
 * three emitters free to be reasoned about separately.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include "codegen/CodegenTypes.h"
#include "codegen/DependencyOrdering.h"
#include "codegen/IdentifierPolicy.h"
#include "codegen/LibraryIncludeTracker.h"
#include "codegen/ProcessImage.h"
#include "codegen/ScopeManager.h"
#include "codegen/SemanticBridge.h"
#include "codegen/TypeMapper.h"
#include "parser/Parser.h"
#include "semantic/SemanticInfo.h"
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace st2cpp::codegen {

/**
 * @struct EmissionContext
 * @brief Everything a single generation run owns, shared by the emitters
 *
 * The fields are public on purpose: the emitters are internal collaborators
 * and spelling every field through an accessor would only add noise. Nothing
 * outside the code generator is expected to touch this struct.
 */
struct EmissionContext
{
   EmissionContext() { rebuildSemanticComponents(); }

   // ===== Configuration =====

   void setNamespace(const std::string& ns) { m_namespace = ns; }
   void setRuntimeHeader(const std::string& rt) { m_runtimeHeader = rt; }
   void setProcessImageConfig(const ProcessImageConfig& config) { m_piConfig = config; }
   void setSemanticInfo(st2cpp::semantic::SemanticInfo* info)
   {
      m_semanticInfo = info;
      rebuildSemanticComponents();
   }

   /**
    * @brief Turn the identifier case policy on or off.
    * @details Also refreshes the semantic bridge, which spells every identifier
    * it reports with this policy and must never disagree with the emitter.
    */
   void setCaseSensitive(bool caseSensitive)
   {
      m_caseSensitive = caseSensitive;
      rebuildSemanticComponents();
   }

   /**
    * @brief Re-create the components that hold the analysis by pointer.
    * @details The bridge, the include tracker, the dependency ordering and the
    * type mapper all keep the SemanticInfo rather than a copy, so they have to
    * be rebuilt whenever the analysis is attached, detached, or reached with a
    * different identifier policy. The mapper comes last because it borrows the
    * bridge, and the ordering before it because the mapper reads the same
    * collected maps.
    */
   void rebuildSemanticComponents()
   {
      m_semantic = st2cpp::codegen::SemanticBridge(m_semanticInfo,
                                                   st2cpp::codegen::IdentifierPolicy{m_caseSensitive});
      m_libraryIncludes = std::make_unique<st2cpp::codegen::LibraryIncludeTracker>(m_semantic);
      m_dependencyOrdering = std::make_unique<st2cpp::codegen::DependencyOrdering>(
         m_fbMap, m_isFB, m_structMembers, m_semantic,
         st2cpp::codegen::IdentifierPolicy{m_caseSensitive});
      m_typeMapper = std::make_unique<st2cpp::codegen::TypeMapper>(
         m_semantic, m_aliasTypes, st2cpp::codegen::IdentifierPolicy{m_caseSensitive});
   }

   // ===== Indent helpers =====

   std::string ind() const { return std::string(m_indent * 4, ' '); }
   void push() { ++m_indent; }
   void pop() { --m_indent; }

   // ===== Semantic bridge queries (forwarded to SemanticBridge) =====

   bool semanticAvailable() const { return m_semantic.semanticAvailable(); }
   const st2cpp::semantic::SymbolTable* semanticSymTab() const { return m_semantic.semanticSymTab(); }
   const st2cpp::library::LibraryDescriptor* semanticDescriptorFor(const st2cpp::semantic::Symbol& sym) const
   { return m_semantic.semanticDescriptorFor(sym); }
   void reportBindingIncomplete(const std::string& entity, const std::string& what) const
   { m_semantic.reportBindingIncomplete(entity, what); }
   std::string semanticTypeCppName(st2cpp::semantic::TypeId id, const std::string& fallback) const
   { return m_semantic.semanticTypeCppName(id, fallback); }
   std::string semanticEnumeratorCppName(st2cpp::semantic::TypeId id, const std::string& member) const
   { return m_semantic.semanticEnumeratorCppName(id, member); }
   std::string semanticEnumNameForEnumerator(st2cpp::semantic::SymbolId id) const
   { return m_semantic.semanticEnumNameForEnumerator(id); }
   std::string semanticCallTargetName(const CallExpr& call) const
   { return m_semantic.semanticCallTargetName(call); }
   std::string semanticVariableBinding(const st2cpp::semantic::Symbol& sym) const
   { return m_semantic.semanticVariableBinding(sym); }
   st2cpp::codegen::ExternalFbCallInfo semanticFbCallInfo(const CallExpr& call) const
   { return m_semantic.semanticFbCallInfo(call); }
   std::optional<FunctionSignature> semanticSignatureForCall(const CallExpr& call) const
   { return m_semantic.semanticSignatureForCall(call); }
   st2cpp::semantic::SymbolId semanticFbSymbolId(const POU& pou) const
   { return m_semantic.semanticFbSymbolId(pou); }
   std::string semanticBaseForFb(const POU& pou) const { return m_semantic.semanticBaseForFb(pou); }
   std::string declaredIdent(const std::string& written, st2cpp::semantic::SymbolId id) const
   { return m_semantic.declaredIdent(written, id); }

   /// The bridge, for the components that take it as a collaborator.
   const st2cpp::codegen::SemanticBridge& semanticBridge() const { return m_semantic; }

   // ===== Dependency ordering (forwarded to DependencyOrdering) =====

   std::vector<std::string> orderedFbNamesFromSemantic() { return deps().orderedFbNamesFromSemantic(); }
   std::unordered_map<std::string, std::unordered_set<std::string>>
   buildFBDependencies(const TranslationUnit& tu) const { return deps().buildFBDependencies(tu); }
   std::vector<std::string> topologicalSort(
      const std::unordered_map<std::string, std::unordered_set<std::string>>& d) const
   { return deps().topologicalSort(d); }
   bool structContainsFB(const std::string& structName, const TranslationUnit& tu) const
   { return deps().structContainsFB(structName, tu); }
   BuildStructDepType buildStructDependencies(const TranslationUnit& tu) const
   { return deps().buildStructDependencies(tu); }
   BuildStructDepType buildStructDependenciesForStructs(const std::vector<StructType>& structs) const
   { return deps().buildStructDependenciesForStructs(structs); }
   std::vector<std::string> topologicalSortStructs(const BuildStructDepType& d) const
   { return deps().topologicalSortStructs(d); }
   st2cpp::codegen::DependencyOrdering& deps() const { return *m_dependencyOrdering; }

   // ===== Type mapping (forwarded to TypeMapper) =====

   std::string mapBaseType(BaseType b) const { return types().mapBaseType(b); }
   std::string mapType(const TypeRef& tr) const { return types().mapType(tr); }
   std::string mapTypeId(st2cpp::semantic::TypeId id) const { return types().mapTypeId(id); }
   std::string getBaseTypeName(const TypeRef& tr) const { return types().getBaseTypeName(tr); }
   std::string getBaseFBName(const std::string& n) const { return types().getBaseFBName(n); }
   bool isVoidType(const TypeRef& tr) const { return types().isVoidType(tr); }
   std::string getArrayType(const std::string& b, const TypeRef& tr) const { return types().getArrayType(b, tr); }
   void registerTypeAliases(const std::vector<TypeAlias>& a) { types().registerTypeAliases(a); }
   std::string resolveAliasLegacy(const std::string& n) const { return types().resolveAliasLegacy(n); }
   int getTypeSizeInBytes(const TypeRef& tr) const { return types().getTypeSizeInBytes(tr); }
   int getTypeAlignment(const TypeRef& tr) const { return types().getTypeAlignment(tr); }
   bool isSemanticBoolType(st2cpp::semantic::TypeId id) const { return types().isSemanticBoolType(id); }
   bool isSemanticEnumType(st2cpp::semantic::TypeId id) const { return types().isSemanticEnumType(id); }
   std::string applySemanticAssignmentCast(const Expr& l, const Expr& r, const std::string& c) const
   { return types().applySemanticAssignmentCast(l, r, c); }
   st2cpp::codegen::TypeMapper& types() const { return *m_typeMapper; }

   // Identifier spelling, kept for the many existing call sites.
   std::string normalize(const std::string& str) const { return types().normalize(str); }
   std::string normalizeIdent(const std::string& str) const { return types().normalizeIdent(str); }
   std::string normalizeType(const std::string& str) const { return types().normalizeType(str); }

   // ===== Library include tracker (forwarded to LibraryIncludeTracker) =====

   void computeLibraryIncludeLines(const TranslationUnit& tu) { m_libraryIncludes->computeLibraryIncludeLines(tu); }
   std::string libraryIncludeBlock() const { return m_libraryIncludes->libraryIncludeBlock(); }
   const std::vector<std::string>& libraryIncludeLines() const { return m_libraryIncludes->includeLines(); }
   void collectUsedLibrariesFromExpr(const Expr& e, std::unordered_set<std::string>& u) const
   { m_libraryIncludes->collectUsedLibrariesFromExpr(e, u); }
   void collectUsedLibrariesFromStmt(const Stmt& s, std::unordered_set<std::string>& u) const
   { m_libraryIncludes->collectUsedLibrariesFromStmt(s, u); }
   void collectUsedLibrariesFromTypeRef(const TypeRef& tr, std::unordered_set<std::string>& u) const
   { m_libraryIncludes->collectUsedLibrariesFromTypeRef(tr, u); }
   void recordUsedLibraryForSymbol(const st2cpp::semantic::Symbol& sym, std::unordered_set<std::string>& u) const
   { m_libraryIncludes->recordUsedLibraryForSymbol(sym, u); }
   void recordUsedLibraryForTypeId(st2cpp::semantic::TypeId id, std::unordered_set<std::string>& u) const
   { m_libraryIncludes->recordUsedLibraryForTypeId(id, u); }

   /// Order the members of a struct initializer the way the struct declares them.
   std::vector<StructInitExpr::MemberInit> orderStructMembers(const std::vector<StructInitExpr::MemberInit>& members,
                                                              const std::string& typeName) const
   { return deps().orderStructMembers(members, typeName); }

   // ===== Namespace, runtime header and generated file identity =====

   std::string m_namespace;     // Namespace per il codice generato
   std::string m_runtimeHeader; // Header del runtime da includere
   std::ostringstream m_hdr;    // header stream
   std::ostringstream m_src;    // source stream
   std::string m_hdrName;
   std::string m_currentFunctionName; // Useful for return variable of functions
   int m_indent = 0;
   // false = fold identifiers to uppercase (the PLC default), true = keep case
   bool m_caseSensitive = false;
   std::string m_currentFBBase; // Base function block named by SUPER^

   // Handler for variables scope
   ScopeManager m_scope;

   // Optional semantic analysis output consumed by the generator. Non-const so
   // the generator may append binding warnings to the diagnostics. Declared
   // before the bridge it feeds.
   st2cpp::semantic::SemanticInfo* m_semanticInfo = nullptr;
   // The read-only view over the semantic analysis, rebuilt whenever the
   // analysis is attached or the identifier policy changes.
   st2cpp::codegen::SemanticBridge m_semantic {m_semanticInfo, {}};
   std::unique_ptr<st2cpp::codegen::LibraryIncludeTracker> m_libraryIncludes;
   // Holds references to the collected maps, filled by the declaration pass and
   // read by the ordering pass.
   mutable std::unique_ptr<st2cpp::codegen::DependencyOrdering> m_dependencyOrdering;
   // Holds a reference to the alias table, filled by the declaration pass.
   mutable std::unique_ptr<st2cpp::codegen::TypeMapper> m_typeMapper;

   // ===== Collected state =====

   std::unordered_map<std::string, FunctionSignature> m_signatures;
   std::unordered_map<std::string, std::unordered_set<std::string>> m_enumValues; // enum -> enumerators
   std::unordered_map<std::string, std::string> m_enumeratorToEnum;               // enumerator -> enum (O(1))
   std::unordered_map<std::string, FunctionSignature> m_methodSignatures;
   std::unordered_map<std::string, bool> m_enumTypes; // enum name -> isScoped
   std::unordered_set<std::string> m_structTypes;
   std::unordered_map<std::string, TypeRef> m_aliasTypes; // alias (uppercase) -> underlying TypeRef
   std::unordered_map<std::string, std::vector<std::string>> m_structMembers; // struct -> ordered members
   std::string m_currentFunctionReturnType;                                   // Empty if void
   ProjectStyle m_projectStyle = ProjectStyle::FLAT;                          // Current mode
   std::string m_outputDir;                                                   // Output directory
   std::unordered_map<std::string, POU> m_fbMap;                              // FB name -> POU
   std::unordered_map<std::string, bool> m_isFB;                              // Type name -> is FB

   // ===== Process image / AT addresses =====

   ProcessImageConfig m_piConfig;
   bool m_hasAddresses{false};
   AddressAllocator m_addressAllocator; // Address allocator for AT placeholders
   std::unordered_map<std::string, AddressExpr> m_resolvedATAddresses;
};

} // namespace st2cpp::codegen
