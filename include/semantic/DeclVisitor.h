/**
 * @file DeclVisitor.h
 * @brief Declaration visitor: registers types, POU, variables, methods in symbol table
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "ast/AST.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace st2cpp::semantic {

/**
 * @brief Visitor for declaration phase
 * 
 * First pass: registers all declarations in symbol table.
 * Handles: user-defined types, interfaces, POU, variables, methods.
 * Supports forward references by doing two sub-passes:
 *   1. Register all type/POU/interface declarations (headers only)
 *   2. Register variables, methods, inheritance links
 */
class DeclVisitor {
public:
    DeclVisitor(SymbolTable& symTab, Diagnostics& diag);
    
    // Main entry points
    void visitTranslationUnit(const TranslationUnit& tu);
    
    // Accessors for SemanticInfo
    const std::vector<SymbolId>& getFbTopoOrder() const { return fbTopoOrder_; }
    const std::vector<SymbolId>& getStructTopoOrder() const { return structTopoOrder_; }
    const std::unordered_map<SymbolId, SymbolId>& getFbBaseClass() const { return fbBaseClass_; }
    const std::unordered_map<SymbolId, std::vector<SymbolId>>& getFbImplements() const { return fbImplements_; }

private:
    SymbolTable& symTab_;
    Diagnostics& diag_;
    
    // State for forward reference resolution
    std::unordered_map<std::string, std::string> pendingExtends_;      // POU name -> base class name
    std::unordered_map<std::string, std::vector<std::string>> pendingImplements_; // POU name -> interface names
    std::unordered_map<std::string, ScopeId> structScopes_;            // struct name (normalized) -> STRUCT_ scope id
    std::unordered_map<std::string, ScopeId> interfaceScopes_;         // interface name (normalized) -> INTERFACE_ scope id
    
    // Results for SemanticInfo
    std::vector<SymbolId> fbTopoOrder_;
    std::vector<SymbolId> structTopoOrder_;
    std::unordered_map<SymbolId, SymbolId> fbBaseClass_;
    std::unordered_map<SymbolId, std::vector<SymbolId>> fbImplements_;
    
    // Current POU context (for nested scopes)
    SymbolId currentPouId_ = 0;
    SymbolId currentMethodId_ = 0;

    // Source file of the entity currently being visited. Workspace analysis
    // merges declarations coming from many .st files into a single
    // TranslationUnit, so a location must name the file the entity was
    // declared in rather than one global source name.
    std::string currentFile_;
    
    // --- Type Registration (two-pass: header then body) ---
    void registerEnumType(const EnumType& et);
    void registerStructHeader(const StructType& st);
    void registerStructBody(const StructType& st);
    void registerTypeAlias(const TypeAlias& alias);
    
    // --- Interface Registration (two-pass: header then body) ---
    void registerInterfaceHeader(const Interface& iface);
    void registerInterfaceBody(const Interface& iface);
    
    // --- POU Registration (two-pass) ---
    void registerPouHeaders(const std::vector<POU>& pous);
    void registerPouBodies(const std::vector<POU>& pous);
    void registerPou(const POU& pou);
    
    // --- Variable Registration ---
    void registerVarSection(const VarSection& section, SymbolId scopeId, 
                           SymbolKind varKind, bool isMethodLocal = false);
    void registerVarDecl(const VarDecl& decl, SymbolId scopeId, SymbolKind varKind, VarKind sectionKind = VarKind::VAR);
    
    // --- Method Registration ---
    void registerMethods(const std::vector<Method>& methods, SymbolId fbScopeId);
    void registerMethod(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId);
    
// --- Inheritance Resolution ---
    void resolveInheritance();
    TypeId resolveTypeName(const std::string& name);
    bool wouldCreateCycle(SymbolId derived, SymbolId base);
    TypeId resolveNamedTypeSilent(const std::string& name);
    
// --- Topological Sorting ---
    void computeTopoOrders();
    void topoSortFbs();
    void topoSortStructs();
    void collectFbCompositionEdges(Symbol* fb, const std::vector<SymbolId>& allFbs,
                                  std::unordered_map<SymbolId, std::vector<SymbolId>>& graph,
                                  std::unordered_map<SymbolId, int>& inDegree);
    void detectValueCycles();
    SymbolId valuePayloadSymbol(TypeId typeId) const;
    
    // --- Type Resolution ---
    TypeId resolveTypeRef(const TypeRef& typeRef, uint32_t line = 0, uint32_t col = 0);
    TypeId resolveBaseType(BaseType baseType);
    std::string baseTypeName(BaseType baseType);
    TypeId resolveNamedType(const std::string& name, uint32_t line = 0, uint32_t col = 0);
    
    // --- Helpers ---
    SourceLocation makeLocation(uint32_t line, uint32_t col = 0) const;
    void reportError(DiagnosticCode code, const std::string& msg, const SourceLocation& loc);
    void reportWarning(DiagnosticCode code, const std::string& msg, const SourceLocation& loc);
};

} // namespace st2cpp::semantic