/**
 * @file TestHelper.cpp
 * @brief Implementation of helper class for tests
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "TestHelper.h"
#include "codegen/CodeGenerator.h"
#include "parser/Parser.h"
#include "lexer/Lexer.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <array>
#include <memory>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

/**
 * @brief Helper class to call the transpiler st2cpp generation functions
 * 
 * @param stCode ST code as input, to transform
 * @param headerName Name of the header files to the guards
 * @param namespaceName Name of namespace
 * @param caseSensitive if true it is case sensitive, otherwise not (all uppercase)
 * @return GeneratedCode Header and source file in cpp
 */
GeneratedCode TestHelper::generateFromST(const std::string& stCode,
                                         const std::string& headerName,
                                         const std::string& namespaceName,
                                         bool caseSensitive)
{
   try {
      Lexer lexer(stCode, "<test>");
      auto tokens = lexer.tokenize();
      Parser parser(std::move(tokens));
      auto tu = parser.parseTranslationUnit();

      CodeGenerator gen;
      gen.setNamespace(namespaceName);
      gen.setRuntimeHeader("undoCore/types.hpp");
      gen.setCaseSensitive(caseSensitive);

      ProcessImageConfig piConfig;
      piConfig.inputBytes = 1024;
      piConfig.outputBytes = 1024;
      piConfig.markerBytes = 1024;
      gen.setProcessImageConfig(piConfig);

      auto result = gen.generate(tu, headerName, namespaceName, "undoCore/types.hpp", caseSensitive);

      return {result.headerCode, result.sourceCode};
   } catch (const std::exception& e) {
      throw std::runtime_error(std::string("Generation failed: ") + e.what());
   }
}

/**
 * @brief Like generateFromST but in project-style
 * 
 * @param stCode ST code as input, to transform
 * @param outputDir Dir in which there will be the outcome code
 * @return std::vector<GeneratedFile> vecotr of generated codes
 */
std::vector<GeneratedFile> TestHelper::generateModularFromST(const std::string& stCode, const std::string& outputDir)
{
   Lexer lexer(stCode, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   CodeGenerator gen;
   gen.setNamespace("undoCore");
   gen.setRuntimeHeader("undoCore/types.hpp");
   return gen.generateModularProject(tu, outputDir);
}

/**
 * @brief Read a file
 * 
 * @param path filepath
 * @return std::string file read
 */
std::string TestHelper::readFile(const std::string& path)
{
   std::ifstream f(path);
   if (!f) {
      throw std::runtime_error("Cannot open file: " + path);
   }
   std::ostringstream ss;
   ss << f.rdbuf();
   return ss.str();
}

/**
 * @brief Write a file
 * 
 * @param path filepath
 * @param content to write in the file
 */
void TestHelper::writeFile(const std::string& path, const std::string& content)
{
   std::ofstream f(path);
   if (!f) {
      throw std::runtime_error("Cannot write file: " + path);
   }
   f << content;
}

/**
 * @brief Helper to compile the generated code
 * 
 * @param sourcePath filepath to compile
 * @param includePath filepath for the headers
 * @return the result
 */
int TestHelper::compileSource(const std::string& sourcePath, const std::string& includePath, bool isCpp20)
{
   // Build the compilation command
   std::string cmd;
   if (isCpp20) {
      cmd = "g++ -std=c++20 -fsyntax-only " + sourcePath + " -I" + includePath + " 2>&1 > /dev/null";
   } else {
      cmd = "g++ -std=c++17 -fsyntax-only " + sourcePath + " -I" + includePath + " 2>&1 > /dev/null";
   }

   int result = std::system(cmd.c_str());

   if (result != 0) {
      std::string cmd2;
      if (isCpp20) {
         cmd2 = "g++ -std=c++20 -fsyntax-only " + sourcePath + " -I" + includePath + " 2>&1 | head -30";
      } else {
         cmd2 = "g++ -std=c++17 -fsyntax-only " + sourcePath + " -I" + includePath + " 2>&1 | head -30";
      }
      std::string errors = runCommand(cmd2);
      if (!errors.empty()) {
         std::cerr << "Compilation errors:\n" << errors << std::endl;
      }
   }

   return result;
}

/**
 * @brief Helper to run a command cmd
 * 
 * @param cmd to pass
 * @return std::string result
 */
std::string TestHelper::runCommand(const std::string& cmd)
{
   std::array<char, 128> buffer;
   std::string result;

   FILE* pipe = popen(cmd.c_str(), "r");
   if (!pipe) {
      return "";
   }

   while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
      result += buffer.data();
   }

   pclose(pipe);
   return result;
}