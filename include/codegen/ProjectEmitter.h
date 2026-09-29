/**
 * @file ProjectEmitter.h
 * @brief File assembly: the flat translation unit and the modular project
 * @details Owns the two entry points of the generator. The flat entry emits one
 * header and one source for the whole translation unit; the modular entry
 * splits it into one file per function block, function, program and global
 * list, plus the master headers that include them. It delegates declarations
 * to the `DeclEmitter` and bodies to the `BodyEmitter`.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "codegen/DeclEmitter.h"
#include "codegen/EmissionContext.h"

namespace st2cpp::codegen {

/**
 * @class ProjectEmitter
 * @brief Assembles the generated files
 */
class ProjectEmitter
{
public:
   ProjectEmitter(EmissionContext& ctx, BodyEmitter& body, DeclEmitter& decl)
      : m_ctx(ctx), m_body(body), m_decl(decl)
   {}

   CodegenResult generate(const TranslationUnit& tu,
                          const std::string& headerName,
                          const std::string& namespaceName = "undoCore",
                          const std::string& runtimeHeader = "undoCore/types.hpp",
                          bool caseSensitive = false);

   std::vector<GeneratedFile> generateModularProject(const TranslationUnit& tu, const std::string& outputDir);
   std::vector<GeneratedFile> generateModular(const TranslationUnit& tu, const std::string& outputDir);

   std::string generateSimpleGVLsHeader(const TranslationUnit& tu);
   std::string generateGVLsHeader(const TranslationUnit& tu);
   std::string generateGVLsSource(const TranslationUnit& tu);

   std::string generateFunctionBlocksMaster(const std::vector<std::string>& fbNames);
   std::string generateFBHeader(const POU& pou, const std::unordered_set<std::string>& dependencies);
   std::string generateFunctionsHeader(const TranslationUnit& tu);
   std::string generateFunctionsSource(const TranslationUnit& tu);
   std::string generateFBSource(const POU& pou);

   std::string generateProgramsMaster(const std::vector<std::string>& progNames);
   std::string generateProgramHeader(const POU& pou);
   std::string generateProgramSource(const POU& pou);

   std::string generateFunctionBody(const POU& pou);
   std::string generateFBOperatorBody(const POU& pou);
   std::string generateProgramBody(const POU& pou);

private:
   EmissionContext& m_ctx;
   BodyEmitter& m_body;
   DeclEmitter& m_decl;
};

} // namespace st2cpp::codegen
