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
 * @brief Helper to find MSVC compiler on Windows
 */
#ifdef _WIN32
static std::string findMSVCCompiler()
{
   // Try to find cl.exe in PATH
   std::string cmd = "where cl.exe 2>nul";
   std::string output = TestHelper::runCommand(cmd);
   if (!output.empty()) {
      // Remove newline
      output.erase(output.find_last_not_of(" \n\r\t") + 1);
      return output;
   }

   // Try common VS installation paths
   std::vector<std::string> paths = {
      "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
      "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
      "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
      "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe",
   };

   for (const auto& pattern : paths) {
      // Use dir to find the actual path
      std::string dirCmd = "dir /s /b \"" + pattern + "\" 2>nul | findstr /v \"Hostx86\"";
      std::string result = TestHelper::runCommand(dirCmd);
      if (!result.empty()) {
         // Take the first line
         size_t pos = result.find_first_of("\n");
         if (pos != std::string::npos) {
            result = result.substr(0, pos);
         }
         result.erase(result.find_last_not_of(" \n\r\t") + 1);
         return result;
      }
   }

   return "";
}
#endif

/**
 * @brief Helper to compile the generated code
 * 
 * @param sourcePath filepath to compile
 * @param includePath filepath for the headers
 * @return the result
 */
int TestHelper::compileSource(const std::string& sourcePath, const std::string& includePath, bool isCpp20)
{
   std::string output;
   int result = 0;

#ifdef _WIN32
   // Windows: use MSVC compiler (cl.exe)
   std::string compiler = findMSVCCompiler();
   if (compiler.empty()) {
      std::cerr << "MSVC compiler not found! Falling back to g++ (may fail)" << std::endl;
      // Fallback: try g++ (from MSYS2/MinGW)
      std::string stdFlag = isCpp20 ? "-std=c++20" : "-std=c++17";
      std::string cmd = "g++ " + stdFlag + " -fsyntax-only \"" + sourcePath + "\" -I\"" + includePath + "\" 2>&1";
      output = runCommand(cmd);
   } else {
      // Use MSVC cl.exe
      std::string stdFlag = isCpp20 ? "/std:c++20" : "/std:c++17";
      std::string cmd = "\"" + compiler + "\" /EHsc " + stdFlag + " /I\"" + includePath + "\" \"" + sourcePath
                        + "\" /link /out:test.exe 2>&1";
      output = runCommand(cmd);
   }

   // Check for errors in output
   if (output.find("error") != std::string::npos || output.find("Error") != std::string::npos
       || output.find("fatal error") != std::string::npos) {
      std::cerr << "Compilation errors:\n" << output << std::endl;
      result = 1;
   } else {
      result = 0;
   }
#else
   // Linux/macOS: use g++
   std::string stdFlag = isCpp20 ? "-std=c++20" : "-std=c++17";
   std::string cmd = "g++ " + stdFlag + " -fsyntax-only \"" + sourcePath + "\" -I\"" + includePath + "\" 2>&1";
   output = runCommand(cmd);

   if (output.find("error:") != std::string::npos || output.find("Error:") != std::string::npos
       || output.find("fatal error:") != std::string::npos) {
      std::cerr << "Compilation errors:\n" << output << std::endl;
      result = 1;
   } else {
      result = 0;
   }
#endif

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