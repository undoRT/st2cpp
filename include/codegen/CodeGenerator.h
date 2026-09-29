/**
 * @file CodeGenerator.h
 * @brief Code generator interface for translating AST to C++ code
 *
 * This header declares `CodeGenerator`, the facade of the generation pipeline.
 * It owns the shared `EmissionContext` and the three emitters that fill it, and
 * forwards the public entry points to them. The actual work lives in
 * `BodyEmitter` (expressions and statements), `DeclEmitter` (declarations) and
 * `ProjectEmitter` (file assembly), so this class carries no emission logic of
 * its own.
 *
 * The implementation produces a minimal runtime-compatible C++ output using the
 * header-only types.hpp in `st2cpp_includes/undoCore/include/undoCore/types.hpp`.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */
#pragma once

#include "ast/AST.h"
#include "codegen/BodyEmitter.h"
#include "codegen/CodegenTypes.h"
#include "codegen/DeclEmitter.h"
#include "codegen/EmissionContext.h"
#include "codegen/ProjectEmitter.h"
#include "semantic/SemanticInfo.h"
#include <string>
#include <vector>

// The value types the pipeline names without qualification. They live in
// `st2cpp::codegen` now, next to the components that produce them; they are
// re-exported because most call sites still refer to them unqualified.
using st2cpp::codegen::CodegenResult;
using st2cpp::codegen::GenFileType;
using st2cpp::codegen::GeneratedFile;
using st2cpp::codegen::ProjectStyle;
using st2cpp::codegen::ParameterInfo;
using st2cpp::codegen::FunctionSignature;
using st2cpp::codegen::ExternalFbCallInfo;
using BuildStructDepType = st2cpp::codegen::BuildStructDepType;

/**
 * @class CodeGenerator
 * @brief Emit C++ code from the parsed AST
 *
 * The `CodeGenerator` visits the `TranslationUnit` and produces a pair of
 * strings: a C++ header and a C++ source. The generator targets the small
 * `mpscpp` runtime provided in `undoCore/include/undoCore/types.hpp`.
 */
class CodeGenerator
{
public:
   CodeGenerator() = default;

   // ============================================================================
   //  Top-level entry
   // ============================================================================

   CodegenResult generate(const TranslationUnit& tu,
                          const std::string& headerName,
                          const std::string& namespaceName = "undoCore",
                          const std::string& runtimeHeader = "undoCore/types.hpp",
                          bool caseSensitive = false);

   std::vector<GeneratedFile> generateModularProject(const TranslationUnit& tu, const std::string& outputDir);
   void setNamespace(const std::string& ns) { m_ctx.setNamespace(ns); }
   void setRuntimeHeader(const std::string& rt) { m_ctx.setRuntimeHeader(rt); }
   /**
    * @brief Turn the identifier case policy on or off.
    * @details Also refreshes the semantic bridge, which spells every identifier
    * it reports with this policy and must never disagree with the emitter.
    */
   void setCaseSensitive(bool caseSensitive) { m_ctx.setCaseSensitive(caseSensitive); }
   void setProcessImageConfig(const ProcessImageConfig& config) { m_ctx.setProcessImageConfig(config); }
   void setProcessImageGlobal(bool isGlobal) { m_ctx.m_piConfig.useGlobalPI = isGlobal; }

   /**
    * @brief Attach a semantic analysis, or detach it with null.
    * @details When present, the generator consumes the decorated AST
    * (Expr::resolvedTypeId, Expr::symbolId, CallExpr::calleeSymbolId) and the
    * SymbolTable instead of re-inferring names, types and signatures from the
    * syntax. The semantic components are rebuilt because the bridge holds the
    * analysis by pointer, so a bridge built before the analysis was attached
    * would keep querying a null table and silently degrade to syntactic
    * inference.
    */
   void setSemanticInfo(st2cpp::semantic::SemanticInfo* info) { m_ctx.setSemanticInfo(info); }

private:
   // Declaration order matters: the emitters borrow the context, and each
   // emitter borrows the ones before it.
   st2cpp::codegen::EmissionContext m_ctx;
   st2cpp::codegen::BodyEmitter m_body{m_ctx};
   st2cpp::codegen::DeclEmitter m_decl{m_ctx, m_body};
   st2cpp::codegen::ProjectEmitter m_project{m_ctx, m_body, m_decl};
};
