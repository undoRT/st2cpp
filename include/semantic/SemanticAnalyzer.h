/**
 * @file SemanticAnalyzer.h
 * @brief Semantic Analyzer - performs semantic analysis on TranslationUnit
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "semantic/SemanticInfo.h"
#include "library/LibraryRegistry.h"
#include "ast/AST.h"
#include <vector>
#include <unordered_map>

namespace st2cpp {
namespace semantic {

/**
 * @brief Semantic Analyzer - performs semantic analysis on TranslationUnit
 * 
 * Produces SemanticInfo containing SymbolTable, Diagnostics, and pre-computed maps.
 * CodeGenerator can optionally use this for improved diagnostics and pre-computed data.
 */
class SemanticAnalyzer {
public:
    enum class Strictness {
        Permissive,   // Unknown constructs -> warning, continue
        Strict        // Unknown constructs -> error
    };
    
    SemanticAnalyzer() = default;
    
    /**
     * @brief Analyze a translation unit
     * @param tu Translation unit to analyze
     * @param strictness Error handling mode (default: Permissive for migration)
     * @return SemanticInfo with symbol table, diagnostics, and pre-computed data
     */
    SemanticInfo analyze(const TranslationUnit& tu, Strictness strictness = Strictness::Permissive);

    /**
     * @brief Analyze a translation unit whose identifier resolution may fall
     * back to an external library registry.
     * @details The external symbols are imported into the analyzer's symbol
     * table (dedicated external scope) before the declaration pass runs.
     * Project-local declarations always shadow external symbols regardless of
     * the registry contents.
     * @param tu Translation unit to analyze
     * @param registry Loaded library registry (may be empty)
     * @param strictness Error handling mode (default: Permissive for migration)
     * @return SemanticInfo with symbol table, diagnostics, and pre-computed data
     */
    SemanticInfo analyze(const TranslationUnit& tu, const st2cpp::library::LibraryRegistry& registry,
        Strictness strictness = Strictness::Permissive);
    
    // Access to symbol table for inspection (const)
    const SymbolTable& getSymbolTable() const { return symTab_; }
    SymbolTable& getSymbolTable() { return symTab_; }
    
    const Diagnostics& getDiagnostics() const { return diagnostics_; }
    Diagnostics& getDiagnostics() { return diagnostics_; }
    
    /**
     * @brief Set the source file name attached to every reported diagnostic.
     * @details Used by the CLI/tools so diagnostics point at the real input file
     * instead of the default "<input>" placeholder.
     * @param name The source file name (may be a path)
     */
    void setSourceName(const std::string& name) { sourceName_ = name; }
    
private:
    /**
     * @brief Shared analysis pipeline; assumes symTab_/diagnostics_ are reset.
     */
    SemanticInfo analyzeCore(const TranslationUnit& tu, Strictness strictness);

    SymbolTable symTab_;
    Diagnostics diagnostics_;
    std::string sourceName_ = "<input>";
};

} // namespace st2cpp::semantic

} // namespace st2cpp