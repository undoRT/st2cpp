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
    
    // Access to symbol table for inspection (const)
    const SymbolTable& getSymbolTable() const { return symTab_; }
    SymbolTable& getSymbolTable() { return symTab_; }
    
    const Diagnostics& getDiagnostics() const { return diagnostics_; }
    Diagnostics& getDiagnostics() { return diagnostics_; }
    
private:
    SymbolTable symTab_;
    Diagnostics diagnostics_;
};

} // namespace st2cpp::semantic

} // namespace st2cpp