/**
 * @file CodeGenerator.cpp
 * @brief Facade implementation of the code generator
 * @details `CodeGenerator` is a thin facade: it owns the shared
 * `EmissionContext` and the three emitters, and forwards the public entry
 * points to them. The emission logic lives in `BodyEmitter`, `DeclEmitter` and
 * `ProjectEmitter`.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/CodeGenerator.h"

CodegenResult CodeGenerator::generate(const TranslationUnit& tu,
                                      const std::string& headerName,
                                      const std::string& namespaceName,
                                      const std::string& runtimeHeader,
                                      bool caseSensitive)
{
   return m_project.generate(tu, headerName, namespaceName, runtimeHeader, caseSensitive);
}

std::vector<GeneratedFile> CodeGenerator::generateModularProject(const TranslationUnit& tu, const std::string& outputDir)
{
   return m_project.generateModularProject(tu, outputDir);
}
