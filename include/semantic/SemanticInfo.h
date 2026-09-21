/**
 * @file SemanticInfo.h
 * @brief Semantic analysis output passed to CodeGenerator
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

namespace st2cpp::semantic {

/**
 * @brief IEC strictness level used for a semantic analysis run (Fase 6)
 *
 * Permissive preserves the legacy byte-for-byte baseline (implicit
 * conversions delegated to C++); Strict enforces the IEC 61131-3
 * implicit-conversion rules as diagnostics.
 */
enum class AnalysisStrictness : uint8_t {
    Permissive,
    Strict
};

/**
 * @brief Preserved-semantics record (Fase 6)
 *
 * Evidence that the decorated AST is a faithful, complete semantic model of
 * the source. Consumed by the CLI to gate generation in Strict mode and by
 * diagnostics: `preserved()` is true only when the analyzer ran and every
 * identifier reference was resolved.
 */
struct PreservedSemantics {
    bool semanticModeApplied = false;    // true: the analyzer ran and produced a SymbolTable
    AnalysisStrictness strictness = AnalysisStrictness::Permissive;
    size_t resolvedCount = 0;            // identifier references resolved during body analysis
    size_t unresolvedCount = 0;          // identifier references left unresolved

    /**
     * @brief True when the decorated AST is a complete semantic model
     */
    bool preserved() const {
        return semanticModeApplied && unresolvedCount == 0;
    }
};

/**
 * @brief Result of semantic analysis, passed to CodeGenerator
 * 
 * Contains all semantic information needed for code generation.
 * The SymbolTable is owned by the SemanticInfo so the result stays valid
 * even after the SemanticAnalyzer that produced it is destroyed.
 */
struct SemanticInfo {
    std::unique_ptr<SymbolTable> symbolTable;       // Owned
    Diagnostics diagnostics;                        // Owned, move-only
    
    // Pre-computed orders for CodeGenerator (Phase 2 migration)
    std::vector<SymbolId> fbTopoOrder;           // FunctionBlocks in dependency order
    std::vector<SymbolId> structTopoOrder;       // STRUCTs in dependency order
    std::unordered_map<SymbolId, SymbolId> fbBaseClass;  // fbId -> baseClassSymbolId
    std::unordered_map<SymbolId, std::vector<SymbolId>> fbImplements;  // fbId -> interface IDs
    
    // Address allocation results (for AT placeholder resolution)
    // Using string->string map to avoid dependency on AddressExpr from CodeGenerator
    std::unordered_map<std::string, std::string> resolvedATAddresses;  // "FBName::varName" -> resolved address string
    
    // Process Image sizing (copied from CodeGenerator's ProcessImageConfig to avoid dependency)
    struct ProcessImageConfig {
        size_t inputBytes = 1024;
        size_t outputBytes = 1024;
        size_t markerBytes = 1024;
        bool autoDetect = true;
    };
    ProcessImageConfig piConfig;

    // Fase 6: strictness used and evidence that the semantic model was preserved
    PreservedSemantics preservedSemantics;
    
    bool empty() const { return symbolTable == nullptr; }
};

} // namespace st2cpp::semantic