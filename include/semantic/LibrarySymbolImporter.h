/**
 * @file LibrarySymbolImporter.h
 * @brief Imports external library symbols into the semantic SymbolTable
 *
 * The importer is the architectural bridge between the library layer and the
 * semantic layer:
 *   SemanticAnalyzer -> LibrarySymbolImporter -> LibraryRegistry
 *                              -> LibraryDescriptor (validated model)
 *
 * It reads only validated LibraryDescriptor objects (never JSON) and materializes
 * their entities as symbols in the SymbolTable's dedicated external scope, so the
 * rest of the analyzer works unchanged:
 *   - enum types (with their enumerators),
 *   - struct types (with their members),
 *   - function blocks (with their parameter interface),
 *   - functions (with their parameters and return type),
 *   - global variables and constants.
 *
 * The LibraryDescriptor stays the source of truth of the descriptor layer; the
 * symbol table is the working semantic model the analyzer operates on (the same
 * transformation the AST already undergoes via DeclVisitor). Project-local
 * declarations always win over external ones because the external scope is the
 * final resolution fallback.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "library/LibraryDescriptor.h"
#include "library/LibraryRegistry.h"
#include "semantic/Diagnostics.h"
#include "semantic/SymbolTable.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace st2cpp::semantic {

/**
 * @brief Imports library symbols into a SymbolTable's external scope.
 */
class LibrarySymbolImporter {
public:
    /**
     * @param symTab The symbol table to populate (external scope)
     * @param diag The diagnostics collector for import warnings/errors
     */
    LibrarySymbolImporter(SymbolTable& symTab, Diagnostics& diag)
        : symTab_(symTab), diag_(diag) {}

    /**
     * @brief Import every library of the registry.
     * @details Libraries are processed in deterministic order
     * (LibraryRegistry::allOrdered, by uppercase id). When two libraries export
     * the same symbol name the first library wins and a warning diagnostic
     * (ExternalSymbolCollision) is emitted.
     * @param registry The registry of loaded libraries
     */
    void import(const st2cpp::library::LibraryRegistry& registry);

private:
    // ---- Per-entity importers ----
    void importLibrary(const st2cpp::library::LibraryDescriptor& lib);
    void importEnum(const st2cpp::library::EnumTypeDef& e, const st2cpp::library::LibraryDescriptor& owner);
    void importStruct(const st2cpp::library::StructTypeDef& s, const st2cpp::library::LibraryDescriptor& owner);
    void importFunction(const st2cpp::library::FunctionDef& f, const st2cpp::library::LibraryDescriptor& owner);
    void importFunctionBlock(const st2cpp::library::FunctionBlockDef& fb, const st2cpp::library::LibraryDescriptor& owner);
    void importConstant(const st2cpp::library::Constant& c, const st2cpp::library::LibraryDescriptor& owner);
    void importGlobalVariable(const st2cpp::library::GlobalVariable& g, const st2cpp::library::LibraryDescriptor& owner);

    // ---- Type resolution ----
    TypeId resolveLibraryType(const st2cpp::library::TypeRef& ref, const st2cpp::library::LibraryDescriptor& owner);
    TypeId resolvePrimitiveType(st2cpp::library::TypeRefKind kind, BaseType primitive, const st2cpp::library::LibraryDescriptor& owner);
    const st2cpp::library::LibraryDescriptor* findLibrary(const std::string& id) const;

    // ---- Helpers ----
    static std::string cacheKey(const std::string& libId, const std::string& name);
    ParamDir paramDirection(st2cpp::library::ParamDirection dir) const;
    void reportCollision(const std::string& name, const st2cpp::library::LibraryDescriptor& lib);
    SourceLocation makeLocation() const;

    SymbolTable& symTab_;
    Diagnostics& diag_;
    const st2cpp::library::LibraryRegistry* registry_ = nullptr;
    // Cache of imported types keyed by "<normalized lib id>::<normalized type name>".
    std::unordered_map<std::string, TypeId> importedTypes_;
};

} // namespace st2cpp::semantic