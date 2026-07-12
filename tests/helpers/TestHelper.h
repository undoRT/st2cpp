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
   static GeneratedCode generateFromST(const std::string& stCode,
                                       const std::string& headerName = "test.hpp",
                                       const std::string& namespaceName = "undoCore",
                                       bool caseSensitive = false);

   static std::vector<GeneratedFile> generateModularFromST(const std::string& stCode, const std::string& outputDir = "generated");

   static std::string readFile(const std::string& path);
   static void writeFile(const std::string& path, const std::string& content);
   static int compileSource(const std::string& sourcePath, const std::string& includePath, bool isCpp20 = false);
   static std::string runCommand(const std::string& cmd);
};