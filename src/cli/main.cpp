/**
 * @file main.cpp
 * @brief Command-line driver for the st2cpp transpiler
 *
 * Provides a small CLI that reads a Structured Text file, tokenizes and parses
 * it, runs the code generator and writes the generated C++ header and source
 * files. See `printUsage()` for available command-line options.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "codegen/CodeGenerator.h"
#include "version.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <cstring>

namespace fs = std::filesystem;

static bool verbose = false;

static void printVersion()
{
   std::cout << getFullVersion() << "\n";
}

static std::vector<std::string> findStFiles(const std::string& directory, bool recursive)
{
   std::vector<std::string> files;

   if (recursive) {
      for (const auto& entry : fs::recursive_directory_iterator(directory)) {
         if (entry.is_regular_file() && entry.path().extension() == ".st") {
            files.push_back(entry.path().string());
         }
      }
   } else {
      for (const auto& entry : fs::directory_iterator(directory)) {
         if (entry.is_regular_file() && entry.path().extension() == ".st") {
            files.push_back(entry.path().string());
         }
      }
   }

   return files;
}

static void writeFileInDir(const std::string& dir, const std::string& filename, const std::string& content)
{
   fs::path dirPath(dir);
   if (!fs::exists(dirPath)) {
      fs::create_directories(dirPath);
   }

   fs::path filePath = dirPath / filename;
   std::ofstream f(filePath);
   if (!f) {
      throw std::runtime_error("Cannot write file: " + filePath.string());
   }
   f << content;
}

static void writeGeneratedFiles(const std::vector<GeneratedFile>& files, const std::string& baseDir)
{
   // Crea la directory base se non esiste
   fs::path basePath(baseDir);
   if (!fs::exists(basePath)) {
      if (!fs::create_directories(basePath)) {
         throw std::runtime_error("Cannot create directory: " + baseDir);
      }
      if (verbose) {
         std::cout << "Created directory: " << baseDir << "\n";
      }
   }

   for (const auto& file : files) {
      std::string fullPath = baseDir;
      if (!file.subdir.empty()) {
         fullPath += "/" + file.subdir;
         fs::path subPath(fullPath);
         if (!fs::exists(subPath)) {
            if (!fs::create_directories(subPath)) {
               throw std::runtime_error("Cannot create directory: " + fullPath);
            }
            if (verbose) {
               std::cout << "Created directory: " << fullPath << "\n";
            }
         }
      }

      std::string extension = (file.type == GenFileType::SOURCE) ? ".cpp" : ".hpp";
      fullPath += "/" + file.name + extension;

      std::ofstream out(fullPath);
      if (!out) {
         throw std::runtime_error("Cannot write file: " + fullPath);
      }
      out << file.content;
      out.close();

      if (verbose) {
         std::cout << "  Generated: " << fullPath << "\n";
      }
   }
}

static void printUsage(const char* prog)
{
   std::cerr << "st2cpp - Structured Text to C++ Compiler\n"
                "Version: "
             << getVersion()
             << "\n"
                "License: GPL-3.0\n"
                "\n"
                "Usage: "
             << prog
             << " <input.st> [options]\n\n"
                "Options:\n"
                "  -o <output.cpp>      Output C++ source file (default: <input>.cpp)\n"
                "  -H <output.hpp>      Output C++ header file (default: <input>.hpp)\n"
                "  --namespace <name>   Set C++ namespace for generated code (default: undoCore)\n"
                "  --runtime <file>     Custom runtime header file (default: undoCore/undoCore.hpp)\n"
                "  --tokens             Dump token list and exit\n"
                "  --caseSensitive      Preserve original case (default: convert to uppercase)\n"
                "  --workspace <path>   Process all .st files in workspace (recursive)\n"
                "  --project-style      Generate modular project structure (separate files for each FB)\n"
                "  --output-dir <dir>   Output directory (default: generated)\n"
                "  --pi-auto            Auto-detect Process Image sizes (default)\n"
                "  --pi-no-auto         Disable auto-detection, use manual sizes\n"
                "  --pi-input <bytes>   Process Image Input size in bytes (default: 1024)\n"
                "  --pi-output <bytes>  Process Image Output size in bytes (default: 1024)\n"
                "  --pi-marker <bytes>  Process Image Marker size in bytes (default: 1024)\n"
                "  -v, --verbose        Print detailed processing information\n"
                "  -h, --help           Show this help\n\n"
                "Examples:\n"
                "  Single file:     st2cpp counter.st -o counter.cpp\n"
                "  Workspace:       st2cpp --workspace ./my_plc_project\n"
                "  Project style:   st2cpp --workspace ./my_plc_project --project-style --output-dir build\n";
}

static std::string readFile(const std::string& path)
{
   std::ifstream f(path);
   if (!f) {
      throw std::runtime_error("Cannot open file: " + path);
   }
   std::ostringstream ss;
   ss << f.rdbuf();
   return ss.str();
}

static void writeFile(const std::string& path, const std::string& content)
{
   std::ofstream f(path);
   if (!f) {
      throw std::runtime_error("Cannot write file: " + path);
   }
   f << content;
}

/**
 * @brief Process a single translation unit (for workspace or project style)
 */
static TranslationUnit processSingleFile(const std::string& filePath, bool dumpTokens)
{
   std::string source = readFile(filePath);
   Lexer lexer(source, filePath);
   auto tokens = lexer.tokenize();

   if (dumpTokens) {
      std::cout << "\nTokens for " << filePath << ":\n";
      for (const auto& tok : tokens) {
         std::cout << "  " << tok.line << ":" << tok.col << "\t" << static_cast<int>(tok.type) << "\t" << tok.text << "\n";
      }
   }

   Parser parser(std::move(tokens));
   Parser::clearParsedInterfaces();
   return parser.parseTranslationUnit();
}

int main(int argc, char* argv[])
{
   if (argc < 2) {
      printUsage(argv[0]);
      return 1;
   }

   std::string inputPath;
   std::string outputCpp, outputHpp;
   bool dumpTokens = false;
   std::string namespaceName = "undoCore";
   std::string runtimeHeader = "undoCore/undoCore.hpp";
   bool caseSensitive = false;
   bool workspaceMode = false;
   bool projectStyle = false;
   std::string workspacePath;
   std::string outputDir = "generated";
   bool autoDetectPI = true;
   size_t piInputBytes = 1024;
   size_t piOutputBytes = 1024;
   size_t piMarkerBytes = 1024;

   for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
         printUsage(argv[0]);
         return 0;
      } else if (std::strcmp(argv[i], "--version") == 0) {
         printVersion();
         return 0;
      } else if (std::strcmp(argv[i], "--tokens") == 0) {
         dumpTokens = true;
      } else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
         outputCpp = argv[++i];
      } else if (std::strcmp(argv[i], "--caseSensitive") == 0) {
         caseSensitive = true;
      } else if (std::strcmp(argv[i], "-H") == 0 && i + 1 < argc) {
         outputHpp = argv[++i];
      } else if (std::strcmp(argv[i], "--runtime") == 0 && i + 1 < argc) {
         runtimeHeader = argv[++i];
      } else if (std::strcmp(argv[i], "--namespace") == 0 && i + 1 < argc) {
         namespaceName = argv[++i];
         for (char c : namespaceName) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
               std::cerr << "Error: Invalid namespace name. Use only letters, numbers, and underscores.\n";
               return 1;
            }
         }
      } else if (std::strcmp(argv[i], "--workspace") == 0 && i + 1 < argc) {
         workspaceMode = true;
         workspacePath = argv[++i];
      } else if (std::strcmp(argv[i], "--project-style") == 0) {
         projectStyle = true;
      } else if (std::strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
         outputDir = argv[++i];
      } else if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
         verbose = true;
      } else if (std::strcmp(argv[i], "--pi-auto") == 0) {
         autoDetectPI = true;
      } else if (std::strcmp(argv[i], "--pi-no-auto") == 0) {
         autoDetectPI = false;
      } else if (std::strcmp(argv[i], "--pi-input") == 0 && i + 1 < argc) {
         piInputBytes = std::stoul(argv[++i]);
      } else if (std::strcmp(argv[i], "--pi-output") == 0 && i + 1 < argc) {
         piOutputBytes = std::stoul(argv[++i]);
      } else if (std::strcmp(argv[i], "--pi-marker") == 0 && i + 1 < argc) {
         piMarkerBytes = std::stoul(argv[++i]);
      } else if (argv[i][0] != '-') {
         inputPath = argv[i];
      } else {
         std::cerr << "Unknown option: " << argv[i] << "\n";
         printUsage(argv[0]);
         return 1;
      }
   }

   // ========================================================================
   // WORKSPACE MODE with PROJECT STYLE
   // ========================================================================
   if (workspaceMode && projectStyle) {
      if (workspacePath.empty()) {
         std::cerr << "Error: --workspace requires a path\n";
         return 1;
      }

      fs::path ws(workspacePath);
      if (!fs::exists(ws)) {
         std::cerr << "Error: Workspace path does not exist: " << workspacePath << "\n";
         return 1;
      }

      // Collect all .st files
      auto stFiles = findStFiles(workspacePath, true);
      if (stFiles.empty()) {
         std::cerr << "Warning: No .st files found in workspace: " << workspacePath << "\n";
         return 0;
      }

      if (verbose) {
         std::cout << "Found " << stFiles.size() << " .st files in workspace\n";
         std::cout << "Project style: ENABLED\n";
         std::cout << "Output directory: " << outputDir << "\n\n";
      }

      // Parse all files and merge translation units
      TranslationUnit mergedTu;
      int successCount = 0;
      int failCount = 0;

      for (const auto& file : stFiles) {
         fs::path filePath(file);
         if (verbose) {
            std::cout << "Parsing: " << filePath.filename().string() << "\n";
         }

         try {
            auto tu = processSingleFile(file, dumpTokens);

            // Merge translation units
            mergedTu.pous.insert(mergedTu.pous.end(), tu.pous.begin(), tu.pous.end());
            mergedTu.structs.insert(mergedTu.structs.end(), tu.structs.begin(), tu.structs.end());
            mergedTu.enums.insert(mergedTu.enums.end(), tu.enums.begin(), tu.enums.end());
            mergedTu.globals.insert(mergedTu.globals.end(), tu.globals.begin(), tu.globals.end());

            successCount++;
         } catch (const ParseError& e) {
            std::cerr << "  Parse error in " << file << ": " << e.what() << "\n";
            failCount++;
         } catch (const std::exception& e) {
            std::cerr << "  Error in " << file << ": " << e.what() << "\n";
            failCount++;
         }
      }

      if (successCount == 0) {
         std::cerr << "No files successfully parsed. Aborting.\n";
         return 1;
      }

      if (verbose) {
         std::cout << "\nParsing complete: " << successCount << " successful, " << failCount << " failed\n";
         std::cout << "Generating project files...\n\n";
      }

      // Process Image auto-detection for modular mode
      ProcessImageAnalyzer piAnalyzer;
      piAnalyzer.analyze(mergedTu);

      ProcessImageConfig piConfig;
      if (autoDetectPI && piAnalyzer.hasAddresses()) {
         piConfig = piAnalyzer.getRecommendedConfig();
         if (verbose) {
            std::cout << "Auto-detected Process Image sizes:\n";
            std::cout << "  Input:  " << piConfig.inputBytes << " bytes\n";
            std::cout << "  Output: " << piConfig.outputBytes << " bytes\n";
            std::cout << "  Marker: " << piConfig.markerBytes << " bytes\n\n";
         }
      } else {
         piConfig.inputBytes = piInputBytes;
         piConfig.outputBytes = piOutputBytes;
         piConfig.markerBytes = piMarkerBytes;
      }
      piConfig.autoDetect = autoDetectPI;

      // Generate modular project
      try {
         CodeGenerator gen;
         gen.setNamespace(namespaceName);
         gen.setRuntimeHeader(runtimeHeader);
         gen.setCaseSensitive(caseSensitive);
         gen.setProcessImageConfig(piConfig);
         auto files = gen.generateModularProject(mergedTu, outputDir);
         writeGeneratedFiles(files, outputDir);

         std::cout << "\nProject generation complete!\n";
         std::cout << "Output directory: " << outputDir << "/\n";
         std::cout << "\nGenerated files:\n";
         std::cout << "  - GVLs.hpp\n";
         std::cout << "  - Functions.hpp / Functions.cpp\n";
         std::cout << "  - FunctionBlocks.hpp (master include)\n";
         std::cout << "  - FunctionBlocks/*.hpp / *.cpp\n";
         std::cout << "  - Programs.hpp / Programs.cpp\n";

      } catch (const std::exception& e) {
         std::cerr << "Generation error: " << e.what() << "\n";
         return 1;
      }

      return (failCount == 0) ? 0 : 1;
   }

   // ========================================================================
   // WORKSPACE MODE (FLAT - separate files per .st)
   // ========================================================================
   if (workspaceMode) {
      if (workspacePath.empty()) {
         std::cerr << "Error: --workspace requires a path\n";
         return 1;
      }

      fs::path ws(workspacePath);
      if (!fs::exists(ws)) {
         std::cerr << "Error: Workspace path does not exist: " << workspacePath << "\n";
         return 1;
      }

      auto stFiles = findStFiles(workspacePath, true);
      if (stFiles.empty()) {
         std::cerr << "Warning: No .st files found in workspace: " << workspacePath << "\n";
         return 0;
      }

      if (verbose) {
         std::cout << "Found " << stFiles.size() << " .st files in workspace\n";
         std::cout << "Output directory: " << outputDir << "\n\n";
      }

      int successCount = 0;
      int failCount = 0;

      for (const auto& file : stFiles) {
         fs::path filePath(file);
         std::string baseName = filePath.stem().string();
         std::string relativePath = fs::relative(file, workspacePath).string();

         if (verbose) {
            std::cout << "Processing: " << relativePath << " -> " << baseName << "\n";
         }

         try {
            auto tu = processSingleFile(file, dumpTokens);

            // Process Image auto-detection for flat mode
            ProcessImageAnalyzer piAnalyzer;
            piAnalyzer.analyze(tu);

            ProcessImageConfig piConfig;
            if (autoDetectPI && piAnalyzer.hasAddresses()) {
               piConfig = piAnalyzer.getRecommendedConfig();
               if (verbose) {
                  std::cout << "    Auto-detected Process Image sizes:\n";
                  std::cout << "      Input:  " << piConfig.inputBytes << " bytes\n";
                  std::cout << "      Output: " << piConfig.outputBytes << " bytes\n";
                  std::cout << "      Marker: " << piConfig.markerBytes << " bytes\n";
               }
            } else {
               piConfig.inputBytes = piInputBytes;
               piConfig.outputBytes = piOutputBytes;
               piConfig.markerBytes = piMarkerBytes;
            }
            piConfig.autoDetect = autoDetectPI;

            std::string headerFilename = baseName + ".hpp";
            std::string sourceFilename = baseName + ".cpp";

            CodeGenerator gen;
            gen.setProcessImageConfig(piConfig);
            auto result = gen.generate(tu, headerFilename, namespaceName, runtimeHeader, caseSensitive);

            writeFileInDir(outputDir, headerFilename, result.headerCode);
            writeFileInDir(outputDir, sourceFilename, result.sourceCode);

            if (verbose) {
               std::cout << "    -> " << outputDir << "/" << headerFilename << "\n";
               std::cout << "    -> " << outputDir << "/" << sourceFilename << "\n";
            }

            successCount++;
         } catch (const ParseError& e) {
            std::cerr << "  Parse error: " << e.what() << "\n";
            failCount++;
         } catch (const std::exception& e) {
            std::cerr << "  Error: " << e.what() << "\n";
            failCount++;
         }
         if (verbose && !stFiles.empty()) {
            std::cout << "\n";
         }
      }

      std::cout << "Workspace processing complete:\n";
      std::cout << "  Successful: " << successCount << "\n";
      std::cout << "  Failed: " << failCount << "\n";
      std::cout << "Output directory: " << outputDir << "/\n";

      return (failCount == 0) ? 0 : 1;
   }

   // ========================================================================
   // SINGLE FILE MODE with PROJECT STYLE? (Not supported, fallback to flat)
   // ========================================================================
   if (projectStyle && !workspaceMode) {
      std::cerr << "Warning: --project-style without --workspace is not supported.\n";
      std::cerr << "Falling back to flat generation for single file.\n\n";
   }

   // ========================================================================
   // SINGLE FILE MODE (FLAT)
   // ========================================================================
   if (inputPath.empty()) {
      std::cerr << "Error: no input file specified\n";
      printUsage(argv[0]);
      return 1;
   }

   // Derive output paths
   fs::path inp(inputPath);
   if (outputCpp.empty()) {
      outputCpp = (inp.stem().string() + ".cpp");
   }
   if (outputHpp.empty()) {
      outputHpp = (inp.stem().string() + ".hpp");
   }
   std::string headerName = fs::path(outputHpp).filename().string();

   try {
      auto tu = processSingleFile(inputPath, dumpTokens);

      if (dumpTokens) {
         return 0;
      }

      // Process Image auto-detection for single file
      ProcessImageAnalyzer piAnalyzer;
      piAnalyzer.analyze(tu);

      ProcessImageConfig piConfig;
      if (autoDetectPI && piAnalyzer.hasAddresses()) {
         piConfig = piAnalyzer.getRecommendedConfig();
         if (verbose) {
            std::cout << "\nAuto-detected Process Image sizes:\n";
            std::cout << "  Input:  " << piConfig.inputBytes << " bytes\n";
            std::cout << "  Output: " << piConfig.outputBytes << " bytes\n";
            std::cout << "  Marker: " << piConfig.markerBytes << " bytes\n\n";
         }
      } else {
         piConfig.inputBytes = piInputBytes;
         piConfig.outputBytes = piOutputBytes;
         piConfig.markerBytes = piMarkerBytes;
      }
      piConfig.autoDetect = autoDetectPI;

      CodeGenerator gen;
      gen.setProcessImageConfig(piConfig);
      auto result = gen.generate(tu, headerName, namespaceName, runtimeHeader, caseSensitive);

      writeFile(outputHpp, result.headerCode);
      writeFile(outputCpp, result.sourceCode);

      std::cout << "Generated: " << outputHpp << "\n";
      std::cout << "Generated: " << outputCpp << "\n";
      std::cout << "\nTo compile the output:\n";
      std::cout << "  g++ -std=c++17 -I<path-to-runtime/include> " << outputCpp << " -o your_program\n";

   } catch (const ParseError& e) {
      std::cerr << "Parse error: " << e.what() << "\n";
      return 1;
   } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << "\n";
      return 1;
   }

   return 0;
}