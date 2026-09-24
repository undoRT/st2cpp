/**
 * @file TestHelper.h
 * @brief Header of helper class for tests
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "codegen/CodeGenerator.h"
#include "library/LibraryRegistry.h"
#include "semantic/LibraryDescriptorBuilder.h"
#include <string>
#include <vector>

struct GeneratedCode
{
   std::string header;
   std::string source;
};

class TestHelper
{
public:
    static TranslationUnit parseST(const std::string& stCode,
                                    const std::string& filename = "<test>");

    static GeneratedCode generateFromST(const std::string& stCode,
                                        const std::string& headerName = "test.hpp",
                                        const std::string& namespaceName = "undoCore",
                                        bool caseSensitive = false);

    /// Like generateFromST but attaches the semantic analysis output to the
    /// CodeGenerator so that the semantic-driven codegen paths are exercised.
    static GeneratedCode generateFromSTWithSemantics(const std::string& stCode,
                                                     const std::string& headerName = "test.hpp",
                                                     const std::string& namespaceName = "undoCore",
                                                     bool caseSensitive = false);

    /// Like generateFromSTWithSemantics but the analysis runs against a library
    /// registry, so external-library symbols resolve and the generated C++
    /// binds to the descriptor cppBinding entries (functions, FB instances,
    /// globals, constants, enum/struct types and includes).
    static GeneratedCode generateFromSTWithLibraries(const std::string& stCode,
                                                     const st2cpp::library::LibraryRegistry& registry,
                                                     const std::string& headerName = "test.hpp",
                                                     const std::string& namespaceName = "undoCore",
                                                     bool caseSensitive = false);

    static std::vector<GeneratedFile> generateModularFromST(const std::string& stCode, const std::string& outputDir = "generated");

    /// Parse, analyze and convert an ST library into a semantic-only
    /// LibraryDescriptor (no cppBinding). Analysis runs without an external
    /// library registry, so all referenced types must be project-local.
    static st2cpp::semantic::LibraryExportResult buildDescriptorFromST(
        const std::string& stCode,
        const st2cpp::semantic::LibraryExportOptions& options);

    /// Like buildDescriptorFromST but the semantic analysis may resolve symbols
    /// against an external library registry; the exported descriptor then
    /// carries dependencies on the referenced libraries.
    static st2cpp::semantic::LibraryExportResult buildDescriptorFromST(
        const std::string& stCode,
        const st2cpp::semantic::LibraryExportOptions& options,
        const st2cpp::library::LibraryRegistry& registry);

   static std::string readFile(const std::string& path);
   static void writeFile(const std::string& path, const std::string& content);
   static int compileSource(const std::string& sourcePath, const std::string& includePath, bool isCpp20 = false);
   static int compileSources(const std::vector<std::string>& sourceFiles, const std::string& includePath, bool isCpp20 = false);
   static std::string runCommand(const std::string& cmd);
};