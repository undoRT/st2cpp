/**
 * @file SemanticAnalyzer.cpp
 * @brief Semantic analyzer implementation (DeclVisitor + BodyVisitor integration)
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/SemanticAnalyzer.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "semantic/DeclVisitor.h"
#include "semantic/BodyVisitor.h"
#include "semantic/LibrarySymbolImporter.h"
#include "semantic/SemanticInfo.h"
#include <cctype>
#include <string>

namespace st2cpp::semantic {

namespace {
/**
 * @brief Convert a string to uppercase for identifier normalization.
 * @details Case-insensitive identifier normalization, matching the
 * CodeGenerator's normalizeIdent/normalizeType for user (non-runtime)
 * names: uppercase.
 * @param s The input string
 * @return The uppercased copy of the input
 */
std::string upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return r;
}
} // namespace

/**
 * @brief Run the semantic pipeline over a translation unit assuming the
 * symbol table and diagnostics have already been reset.
 * @param tu The translation unit to analyze
 * @param strictness The strictness mode controlling IEC compliance checks
 * @return The populated SemanticInfo for the translation unit
 */
/**
 * @brief Run the full semantic analysis pipeline over a translation unit.
 * @details Executes the header-registration pass (DeclVisitor) followed by the
 * body-visiting pass (BodyVisitor), then assembles the SemanticInfo result.
 * Records the preserved-semantics evidence (mode, strictness and resolved
 * identifier counts) and the AT placement addresses consumed by the
 * CodeGenerator, keyed as "FBName::varName" for POU locals and "varName" for
 * globals, skipping method-local variables.
 * @param tu The translation unit to analyze
 * @param strictness The strictness mode controlling IEC compliance checks
 * @return The populated SemanticInfo for the translation unit
 */
SemanticInfo SemanticAnalyzer::analyze(const TranslationUnit& tu, Strictness strictness) {
    // Reset diagnostics and symbol table for this analysis
    diagnostics_ = Diagnostics{};
    diagnostics_.setSourceName(sourceName_);
    symTab_ = SymbolTable{};
    return analyzeCore(tu, strictness);
}

/**
 * @brief Analyze a translation unit with external library symbols imported.
 * @details Imports every registered library into the symbol table's external
 * scope (deterministic order; project-local declarations still win) and then
 * runs the regular semantic pipeline.
 * @param tu The translation unit to analyze
 * @param registry The library registry whose symbols become resolvable
 * @param strictness The strictness mode controlling IEC compliance checks
 * @return The populated SemanticInfo for the translation unit
 */
SemanticInfo SemanticAnalyzer::analyze(const TranslationUnit& tu,
    const st2cpp::library::LibraryRegistry& registry, Strictness strictness) {
    // Reset diagnostics and symbol table for this analysis
    diagnostics_ = Diagnostics{};
    diagnostics_.setSourceName(sourceName_);
    symTab_ = SymbolTable{};

    // Phase 0: import external library symbols before any project declaration.
    // Colliding imports become warnings (ExternalSymbolCollision), never errors.
    LibrarySymbolImporter importer(symTab_, diagnostics_);
    importer.import(registry);

    SemanticInfo info = analyzeCore(tu, strictness);
    // Transport the registry (single source of truth of the C++ bindings) to
    // the CodeGenerator. It references the same descriptors the importer read,
    // so external symbols (decorated with externalLibraryId) can be resolved
    // back to their LibraryDescriptor by the generator.
    info.libraryRegistry = &registry;
    return info;
}

SemanticInfo SemanticAnalyzer::analyzeCore(const TranslationUnit& tu, Strictness strictness) {
    // Phase 1: Run declaration visitor
    DeclVisitor declVisitor(symTab_, diagnostics_);
    declVisitor.visitTranslationUnit(tu);
    
    // Phase 2: Run body visitor for name resolution
    BodyVisitor bodyVisitor(symTab_, diagnostics_);
    if (strictness == Strictness::Strict) {
        bodyVisitor.setStrict(true);
    }
    bodyVisitor.visitTranslationUnit(tu);
    
    // Build SemanticInfo (the symbol table is moved into the result so that
    // it outlives this analyzer instance)
    SemanticInfo info;
    info.symbolTable = std::make_unique<SymbolTable>(std::move(symTab_));
    info.diagnostics = std::move(diagnostics_);
    info.fbTopoOrder = declVisitor.getFbTopoOrder();
    info.structTopoOrder = declVisitor.getStructTopoOrder();
    info.fbBaseClass = declVisitor.getFbBaseClass();
    info.fbImplements = declVisitor.getFbImplements();

    // Fase 6: record the preserved-semantics evidence: analysis ran, strictness
    // used, and how many identifier references the body phase could resolve.
    info.preservedSemantics.semanticModeApplied = true;
    info.preservedSemantics.strictness = (strictness == Strictness::Strict)
        ? AnalysisStrictness::Strict : AnalysisStrictness::Permissive;
    info.preservedSemantics.resolvedCount = bodyVisitor.getResolvedCount();
    info.preservedSemantics.unresolvedCount = bodyVisitor.getUnresolvedCount();

    // Fase 7: record the placement (AT) addresses with the keys the CodeGenerator
    // looks up: "FBName::varName" for POU locals (normalized uppercase) and the
    // plain "varName" for globals. Method-local variables are skipped: the codegen
    // does not emit getters/setters for them, so no key is ever consumed there.
    if (info.symbolTable) {
        const SymbolTable& st = *info.symbolTable;
        st.forEachSymbol([&st, &info](const Symbol& sym) {
            if (sym.atAddress.empty()) {
                return;
            }
            const Scope* scope = st.getScope(sym.scopeId);
            if (!scope) {
                return;
            }
            std::string pouName;
            if (scope->id != st.globalScopeId()) {
                static const char* kPouPrefixes[] = {"FB_", "FUNC_", "PROG_"};
                for (const char* prefix : kPouPrefixes) {
                    const size_t len = std::char_traits<char>::length(prefix);
                    if (scope->name.compare(0, len, prefix) == 0) {
                        pouName = scope->name.substr(len);
                        break;
                    }
                }
                if (pouName.empty()) {
                    return; // method/other scope: not generated by the codegen
                }
            }
            const std::string key =
                pouName.empty() ? upper(sym.name)
                                : upper(pouName) + "::" + upper(sym.name);
            info.resolvedATAddresses[key] = sym.atAddress;
        });
    }
    
    return info;
}

} // namespace st2cpp::semantic