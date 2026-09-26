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
#include "semantic/SemanticAnalyzer.h"
#include "semantic/SemanticInfo.h"
#include "semantic/LibraryDescriptorBuilder.h"
#include "library/LibrarySerializer.h"
#include "project/ProjectConfigLoader.h"
#include "project/ProjectLoader.h"
#include "version.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <cstring>
#include <cctype>
#include <set>
#include <map>

namespace fs = std::filesystem;

static bool verbose = false;
static bool strictMode = false;

static bool hasUnresolvableCircularDependency(const st2cpp::semantic::SemanticInfo& info)
{
   for (const auto& diagnostic : info.diagnostics.all()) {
      if (diagnostic.severity == st2cpp::semantic::DiagnosticSeverity::Error
          && diagnostic.code == st2cpp::semantic::DiagnosticCode::CircularDependency) {
         return true;
      }
   }
   return false;
}

static void reportGenerationBlocked(const st2cpp::semantic::SemanticInfo& info)
{
   if (strictMode) {
      std::cerr << "Strict mode: " << info.diagnostics.errorCount() << " semantic error(s) block generation.\n";
      return;
   }
   std::cerr << "Generation blocked: unresolvable circular by-value dependency.\n";
}

/**
 * @brief Run semantic analysis on a translation unit for codegen consumption
 *
 * The CodeGenerator consumes the decorated AST and SymbolTable when available.
 * Semantic errors are reported by the CLI but do NOT block generation in the
 * default Permissive mode: codegen degrades gracefully to the legacy syntactic
 * inference. Only `--strict` (explicitly requested) enables generation blocking
 * on semantic errors, in which case the diagnostics are printed.
 */
static st2cpp::semantic::SemanticInfo runSemanticAnalysis(
   const TranslationUnit& tu, const st2cpp::library::LibraryRegistry* registry = nullptr,
   const std::string& sourceName = "", const std::string& sourceText = "",
   const std::map<std::string, std::string>* sources = nullptr)
{
   auto strictness = strictMode ? st2cpp::semantic::SemanticAnalyzer::Strictness::Strict
                                : st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive;
   st2cpp::semantic::SemanticAnalyzer analyzer;
   if (!sourceName.empty()) {
      analyzer.setSourceName(sourceName);
   }
   st2cpp::semantic::SemanticInfo info;
   if (registry != nullptr && registry->size() > 0) {
      info = analyzer.analyze(tu, *registry, strictness);
   } else {
      info = analyzer.analyze(tu, strictness);
   }
    const bool fatalCycle = hasUnresolvableCircularDependency(info);
    if (verbose || strictMode || fatalCycle) {
       if (info.diagnostics.totalCount() > 0) {
          info.diagnostics.setSourceText(sourceText);
          // Register every workspace file so a diagnostic can print the
          // snippet of the file it actually points into, not of the workspace.
          if (sources != nullptr) {
             for (const auto& entry : *sources) {
                info.diagnostics.addSourceFile(entry.first, entry.second);
             }
          }
          info.diagnostics.print(std::cerr);
       }
       std::cerr << "Semantic analysis: " << info.diagnostics.errorCount() << " errors, " << info.diagnostics.warningCount()
                 << " warnings\n";
    }
   return info;
}

/**
 * @brief Check whether Strict mode must block generation
 *
 * Generation is blocked ONLY when `--strict` was explicitly requested AND the
 * semantic analysis produced errors. The default Permissive mode never blocks.
 */
static bool strictBlocksGeneration(const st2cpp::semantic::SemanticInfo& info)
{
   return (strictMode && info.diagnostics.hasErrors()) || hasUnresolvableCircularDependency(info);
}

/**
 * @brief Print the full version string to standard output
 */
static void printVersion()
{
   std::cout << getFullVersion() << "\n";
}

/**
 * @brief Find all .st files in a directory, optionally recursing into subdirectories
 * @param directory Directory to search
 * @param recursive Whether to search subdirectories recursively
 * @return Paths of all .st files found
 */
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

/**
 * @brief Write a file inside a directory, creating the directory if needed
 * @param dir Destination directory
 * @param filename File name to write
 * @param content File contents
 * @throws std::runtime_error if the file cannot be written
 */
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

/**
 * @brief Write all generated files under a base directory
 * @param files Generated files to write
 * @param baseDir Base output directory
 * @throws std::runtime_error if a directory or file cannot be created or written
 */
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

/**
 * @brief Print usage and command-line options to standard error
 * @param prog Program name as invoked, shown in the usage line
 */
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
                "  --strict             Strict IEC 61131-3 mode: block generation on semantic errors\n"
                "                      (default: permissive, errors never block generation)\n"
                "  --caseSensitive      Preserve original case (default: convert to uppercase)\n"
                "  --workspace <path>   Process all .st files in workspace (recursive)\n"
                "  --ext-libs <file>    Project JSON listing the external libraries to load\n"
                "  --project-style      Generate modular project structure (separate files for each FB)\n"
                "  --output-dir <dir>   Output directory (default: generated)\n"
                "  --pi-auto            Auto-detect Process Image sizes (default)\n"
                "  --pi-no-auto         Disable auto-detection, use manual sizes\n"
                "  --pi-input <bytes>   Process Image Input size in bytes (default: 1024)\n"
                "  --pi-output <bytes>  Process Image Output size in bytes (default: 1024)\n"
                "  --pi-marker <bytes>  Process Image Marker size in bytes (default: 1024)\n"
                "  --export-descriptor <file.json>\n"
                "                       Export a semantic-only JSON Library Descriptor from the\n"
                "                       analyzed ST (works with a single file or --workspace)\n"
                "  --lib-id <id>        Library id for --export-descriptor (required)\n"
                "  --lib-name <name>    Library name for --export-descriptor (required)\n"
                "  --lib-version <ver>  Library version (semver) for --export-descriptor (required)\n"
                "  --lib-description <text>\n"
                "                       Optional library description\n"
                "  --lib-dependency <id>=<constraint>\n"
                "                       Version policy for an external dependency (repeatable,\n"
                "                       e.g. --lib-dependency timerlib=^1.0.0)\n"
                "  -v, --verbose        Print detailed processing information\n"
                "  -h, --help           Show this help\n\n"
                "Examples:\n"
                "  Single file:     st2cpp counter.st -o counter.cpp\n"
                "  Workspace:       st2cpp --workspace ./my_plc_project\n"
                "  Project style:   st2cpp --workspace ./my_plc_project --project-style --output-dir build\n"
                "  Export library:  st2cpp lib.st --export-descriptor lib.json --lib-id timerlib\n"
                "                          --lib-name TimerLib --lib-version 1.0.0\n";
}

/**
 * @brief Read an entire file into a string
 * @param path File path to read
 * @return File contents
 * @throws std::runtime_error if the file cannot be opened
 */
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

static void printParseError(const ParseError& error)
{
   std::string sourceName = error.fileName.empty() ? "<input>" : error.fileName;
   std::string sourceText;
   if (!error.fileName.empty()) {
      try {
         sourceText = readFile(error.fileName);
      } catch (const std::exception&) {
      }
   }

   st2cpp::semantic::Diagnostics diagnostics;
   diagnostics.setSourceName(sourceName);
   diagnostics.setSourceText(sourceText);

   st2cpp::semantic::SourceLocation location;
   location.fileName = sourceName;
   location.line = error.line;
   location.column = error.col;
   location.endLine = error.line;
   location.endColumn = error.endCol;
   diagnostics.addError(st2cpp::semantic::DiagnosticCode::SyntaxError, error.message, location);
   diagnostics.print(std::cerr);
}

/**
 * @brief Write content to a file, overwriting it if it exists
 * @param path Destination file path
 * @param content File contents
 * @throws std::runtime_error if the file cannot be written
 */
static void writeFile(const std::string& path, const std::string& content)
{
   std::ofstream f(path);
   if (!f) {
      throw std::runtime_error("Cannot write file: " + path);
   }
   f << content;
}

/**
 * @brief Normalized (case-folded, whitespace-free) key for a type/Var name
 *
 * IEC 61131-3 identifiers are case-insensitive and may be decorated with
 * surrounding whitespace, so workspace merging must compare them ignoring case.
 */
static std::string normalizedKey(const std::string& name)
{
   std::string key;
   key.reserve(name.size());
   for (unsigned char c : name) {
      if (!std::isspace(c)) {
         key.push_back(static_cast<char>(std::toupper(c)));
      }
   }
   return key;
}

/**
 * @brief Shallow structural equality for type references (name + base only)
 */
static bool sameTypeRef(const TypeRef& a, const TypeRef& b)
{
   if (a.base != b.base || a.name != b.name || a.isPointer != b.isPointer) {
      return false;
   }
   return a.arrayDims.size() == b.arrayDims.size();
}

/**
 * @brief Compare two structs for type-level equality (normalized member names and types)
 * @param a First struct
 * @param b Second struct
 * @return true if the member lists match
 */
static bool sameStruct(const StructType& a, const StructType& b)
{
   if (a.members.size() != b.members.size()) {
      return false;
   }
   for (size_t i = 0; i < a.members.size(); ++i) {
      if (normalizedKey(a.members[i].name) != normalizedKey(b.members[i].name) || !sameTypeRef(a.members[i].type, b.members[i].type)) {
         return false;
      }
   }
   return true;
}

/**
 * @brief Compare two enums for equality (normalized enumerator names)
 * @param a First enum
 * @param b Second enum
 * @return true if the enumerator lists match
 */
static bool sameEnum(const EnumType& a, const EnumType& b)
{
   if (a.enumerators.size() != b.enumerators.size()) {
      return false;
   }
   for (size_t i = 0; i < a.enumerators.size(); ++i) {
      if (normalizedKey(a.enumerators[i].name) != normalizedKey(b.enumerators[i].name)) {
         return false;
      }
   }
   return true;
}

/**
 * @brief Compare two interfaces for equality (normalized method names and signatures)
 * @param a First interface
 * @param b Second interface
 * @return true if the method lists match
 */
static bool sameInterface(const Interface& a, const Interface& b)
{
   if (a.methods.size() != b.methods.size()) {
      return false;
   }
   for (size_t i = 0; i < a.methods.size(); ++i) {
      const Method& ma = a.methods[i];
      const Method& mb = b.methods[i];
      if (normalizedKey(ma.name) != normalizedKey(mb.name) || !sameTypeRef(ma.returnType, mb.returnType)
          || ma.parameters.size() != mb.parameters.size()) {
         return false;
      }
   }
   return true;
}

/**
 * @brief Compare two POUs for equality (kind, return type, inheritance and var counts)
 * @param a First POU
 * @param b Second POU
 * @return true if the two POUs match
 */
static bool samePou(const POU& a, const POU& b)
{
   if (a.kind != b.kind || !sameTypeRef(a.returnType, b.returnType)) {
      return false;
   }
   if (normalizedKey(a.extends) != normalizedKey(b.extends) || a.implements.size() != b.implements.size()) {
      return false;
   }
   for (size_t i = 0; i < a.implements.size(); ++i) {
      if (normalizedKey(a.implements[i]) != normalizedKey(b.implements[i])) {
         return false;
      }
   }
   size_t am = 0, bm = 0;
   for (const auto& vs : a.varSections) {
      am += vs.decls.size();
   }
   for (const auto& vs : b.varSections) {
      bm += vs.decls.size();
   }
   return am == bm;
}

/**
 * @brief Deduplicate a merged translation unit in place
 *
 * Workspace files are standalone samples that often re-declare the same shared
 * types (STRUCT/ENUM/INTERFACE) and global variables independently. Exact
 * duplicates (same normalized name and same body) are dropped; conflicts that
 * differ in body are kept as warnings on stderr.
 */
static void deduplicateMergedTu(TranslationUnit& tu)
{
   {
      std::set<std::string> seen;
      std::vector<StructType> out;
      for (const auto& item : tu.structs) {
         std::string key = normalizedKey(item.name);
         if (seen.insert(key).second) {
            out.push_back(item);
            continue;
         }
         for (const auto& existing : out) {
            if (normalizedKey(existing.name) == key && !sameStruct(existing, item)) {
               std::cerr << "  Warning: duplicate STRUCT '" << item.name << "' differs across workspace files; keeping first\n";
               break;
            }
         }
      }
      tu.structs = std::move(out);
   }

   {
      std::set<std::string> seen;
      std::vector<EnumType> out;
      for (const auto& item : tu.enums) {
         std::string key = normalizedKey(item.name);
         if (seen.insert(key).second) {
            out.push_back(item);
            continue;
         }
         for (const auto& existing : out) {
            if (normalizedKey(existing.name) == key && !sameEnum(existing, item)) {
               std::cerr << "  Warning: duplicate ENUM '" << item.name << "' differs across workspace files; keeping first\n";
               break;
            }
         }
      }
      tu.enums = std::move(out);
   }

   {
      std::set<std::string> seen;
      std::vector<Interface> out;
      for (const auto& item : tu.interfaces) {
         std::string key = normalizedKey(item.name);
         if (seen.insert(key).second) {
            out.push_back(item);
            continue;
         }
         for (const auto& existing : out) {
            if (normalizedKey(existing.name) == key && !sameInterface(existing, item)) {
               std::cerr << "  Warning: duplicate INTERFACE '" << item.name << "' differs across workspace files; keeping first\n";
               break;
            }
         }
      }
      tu.interfaces = std::move(out);
   }

   {
      std::set<std::string> seen;
      std::vector<POU> out;
      for (const auto& item : tu.pous) {
         std::string key = normalizedKey(item.name);
         if (seen.insert(key).second) {
            out.push_back(item);
            continue;
         }
         for (const auto& existing : out) {
            if (normalizedKey(existing.name) == key && !samePou(existing, item)) {
               std::cerr << "  Warning: duplicate POU '" << item.name << "' differs across workspace files; keeping first\n";
               break;
            }
         }
      }
      tu.pous = std::move(out);
   }

   {
      // Type aliases: same name in several files means the same declaration
      // repeated (typically a shared header copied per file). Keep the first,
      // and warn only if the aliased types actually disagree.
      std::set<std::string> seen;
      std::vector<TypeAlias> out;
      for (const auto& item : tu.typeAliases) {
         std::string key = normalizedKey(item.name);
         if (seen.insert(key).second) {
            out.push_back(item);
            continue;
         }
         for (const auto& existing : out) {
            if (normalizedKey(existing.name) == key && !sameTypeRef(existing.type, item.type)) {
               std::cerr << "  Warning: duplicate TYPE alias '" << item.name << "' differs across workspace files; keeping first\n";
               break;
            }
         }
      }
      tu.typeAliases = std::move(out);
   }

   {
      // Globals: deduplicate VarDecl by name across all sections
      std::set<std::string> seen;
      for (auto& section : tu.globals) {
         std::vector<VarDecl> decls;
         for (const auto& decl : section.decls) {
            std::string key = normalizedKey(decl.name);
            if (seen.insert(key).second) {
               decls.push_back(decl);
            }
         }
         section.decls = std::move(decls);
      }
   }
}

/**
 * @brief Process a single translation unit (for workspace or project style)
 */
static TranslationUnit processSingleFile(const std::string& filePath, bool dumpTokens, std::string* sourceOut = nullptr)
{
   std::string source = readFile(filePath);
   if (sourceOut != nullptr) {
      *sourceOut = source;
   }
   Lexer lexer(source, filePath);
   auto tokens = lexer.tokenize();

   if (dumpTokens) {
      std::cout << "\nTokens for " << filePath << ":\n";
      for (const auto& tok : tokens) {
         std::cout << "  " << tok.line << ":" << tok.col << "\t" << static_cast<int>(tok.type) << "\t" << tok.text << "\n";
      }
   }

   Parser parser(std::move(tokens), filePath);
   Parser::clearParsedInterfaces();
   return parser.parseTranslationUnit();
}

/**
 * @brief Merge the declarations of one file into a workspace-wide unit
 *
 * Every declaration kind carried by a TranslationUnit must be forwarded here.
 * Keeping the merge in a single place is what guarantees a type declared in one
 * .st file is visible from the others: forgetting a kind (as type aliases were)
 * silently makes that kind of declaration file-local.
 */
static void mergeTranslationUnit(TranslationUnit& dst, const TranslationUnit& src)
{
   auto append = [](auto& to, const auto& from) {
      to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
   };
   append(dst.pous, src.pous);
   append(dst.structs, src.structs);
   append(dst.enums, src.enums);
   append(dst.globals, src.globals);
   append(dst.interfaces, src.interfaces);
   append(dst.typeAliases, src.typeAliases);
}

/**
 * @brief Program entry point: parse command-line arguments and drive the transpilation
 *
 * Supports single-file, flat-workspace and project-style workspace modes. Returns 0 on
 * success (or for --help/--version), 1 on usage, parse or generation errors, and when
 * strict mode blocks generation on semantic errors.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code: 0 on success, 1 on error
 */
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
    std::string extLibsConfig;
    std::string exportDescriptorPath;
    std::string libId, libName, libVersion, libDescription;
    std::vector<std::string> dependencyFlags;

   for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
         printUsage(argv[0]);
         return 0;
      } else if (std::strcmp(argv[i], "--version") == 0) {
         printVersion();
         return 0;
      } else if (std::strcmp(argv[i], "--tokens") == 0) {
         dumpTokens = true;
      } else if (std::strcmp(argv[i], "--strict") == 0) {
         strictMode = true;
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
      } else if (std::strcmp(argv[i], "--ext-libs") == 0 && i + 1 < argc) {
         extLibsConfig = argv[++i];
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
       } else if (std::strcmp(argv[i], "--export-descriptor") == 0 && i + 1 < argc) {
          exportDescriptorPath = argv[++i];
       } else if (std::strcmp(argv[i], "--lib-id") == 0 && i + 1 < argc) {
          libId = argv[++i];
       } else if (std::strcmp(argv[i], "--lib-name") == 0 && i + 1 < argc) {
          libName = argv[++i];
       } else if (std::strcmp(argv[i], "--lib-version") == 0 && i + 1 < argc) {
          libVersion = argv[++i];
       } else if (std::strcmp(argv[i], "--lib-description") == 0 && i + 1 < argc) {
          libDescription = argv[++i];
       } else if (std::strcmp(argv[i], "--lib-dependency") == 0 && i + 1 < argc) {
          dependencyFlags.push_back(argv[++i]);
       } else if (argv[i][0] != '-') {
         inputPath = argv[i];
      } else {
         std::cerr << "Unknown option: " << argv[i] << "\n";
         printUsage(argv[0]);
         return 1;
      }
   }

   // ========================================================================
   // EXTERNAL LIBRARIES (--ext-libs <project.json>)
   // Loads the project JSON, resolves every declared library descriptor and
   // builds a LibraryRegistry that semantic analysis feeds into the codegen.
   // ========================================================================
   st2cpp::library::LibraryRegistry libraryRegistry;
   if (!extLibsConfig.empty()) {
      auto cfgRes = st2cpp::project::ProjectConfigLoader::fromFile(extLibsConfig);
      if (!cfgRes.ok()) {
         std::cerr << "Error: invalid project JSON for --ext-libs: " << extLibsConfig << "\n";
         for (const auto& err : cfgRes.errors) {
            std::cerr << "  - " << err.toString() << "\n";
         }
         return 1;
      }
      auto loaded = st2cpp::project::ProjectLoader::load(*cfgRes.config);
      if (!loaded.ok()) {
         std::cerr << "Error: cannot load the libraries declared by " << extLibsConfig << "\n";
         for (const auto& err : loaded.errors) {
            std::cerr << "  - " << err.toString() << "\n";
         }
         return 1;
      }
      libraryRegistry = std::move(loaded.registry);
      if (verbose) {
         std::cout << "External libraries loaded from " << extLibsConfig << ":\n";
         for (const auto& id : libraryRegistry.ids()) {
            std::cout << "  - " << id << "\n";
         }
      }
   }

   // ========================================================================
   // DESCRIPTOR EXPORT MODE (--export-descriptor <file.json>)
   // ST -> SemanticAnalyzer -> LibraryDescriptorBuilder -> JSON
   // Produces a semantic-only Library Descriptor v1.0 (no cppBinding) that is
   // re-importable through --ext-libs. Constructs not representable in the
   // JSON schema surface as explicit export errors (never silent).
   // ========================================================================
   if (!exportDescriptorPath.empty()) {
      if (inputPath.empty() && !workspaceMode) {
         std::cerr << "Error: --export-descriptor requires an input .st file or --workspace\n";
         printUsage(argv[0]);
         return 1;
      }
      if (libId.empty() || libName.empty() || libVersion.empty()) {
         std::cerr << "Error: --export-descriptor requires --lib-id, --lib-name and --lib-version\n";
         printUsage(argv[0]);
         return 1;
      }

      st2cpp::semantic::LibraryExportOptions options;
      options.id = libId;
      options.name = libName;
      options.version = libVersion;
      options.description = libDescription;
      for (const auto& dep : dependencyFlags) {
         size_t eq = dep.find('=');
         if (eq == std::string::npos || eq == 0 || eq + 1 == dep.size()) {
            std::cerr << "Error: invalid --lib-dependency '" << dep
                      << "' (expected <id>=<constraint>, e.g. timerlib=^1.0.0)\n";
            return 1;
         }
         options.dependencyVersions[dep.substr(0, eq)] = dep.substr(eq + 1);
      }

      TranslationUnit tu;
      std::string sourceName;
      std::string sourceText;
      std::map<std::string, std::string> sources;
      try {
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
            int parsed = 0;
            for (const auto& file : stFiles) {
               std::string fileSource;
               auto fileTu = processSingleFile(file, false, &fileSource);
               sources.emplace(file, std::move(fileSource));
               mergeTranslationUnit(tu, fileTu);
               parsed++;
            }
            if (parsed == 0) {
               std::cerr << "Error: no .st files could be parsed in workspace: " << workspacePath << "\n";
               return 1;
            }
            deduplicateMergedTu(tu);
            sourceName = workspacePath;
         } else {
            tu = processSingleFile(inputPath, false, &sourceText);
            sourceName = inputPath;
         }
      } catch (const ParseError& e) {
          printParseError(e);
          return 1;
      } catch (const std::exception& e) {
         std::cerr << "Error: " << e.what() << "\n";
         return 1;
      }

      auto semanticInfo = runSemanticAnalysis(tu, &libraryRegistry, sourceName, sourceText, &sources);
      auto result = st2cpp::semantic::LibraryDescriptorBuilder::build(tu, semanticInfo, options);

      for (const auto& err : result.errors) {
         std::cerr << "Export error: " << err.toString() << "\n";
      }

      if (!result.descriptor) {
         std::cerr << "Error: no library descriptor could be built; nothing written to "
                   << exportDescriptorPath << "\n";
         return 1;
      }

      try {
         writeFile(exportDescriptorPath, st2cpp::library::LibrarySerializer::toJson(*result.descriptor, 2));
      } catch (const std::exception& e) {
         std::cerr << "Error: cannot write " << exportDescriptorPath << ": " << e.what() << "\n";
         return 1;
      }

      std::cout << "Exported library descriptor: " << exportDescriptorPath << "\n";
      std::cout << "  id: " << options.id << "\n";
      std::cout << "  name: " << options.name << "\n";
      std::cout << "  version: " << options.version << "\n";
      std::cout << "  sections: enums=" << result.descriptor->enums.size()
                << " types=" << result.descriptor->types.size()
                << " constants=" << result.descriptor->constants.size()
                << " globals=" << result.descriptor->globalVariables.size()
                << " functions=" << result.descriptor->functions.size()
                << " fbs=" << result.descriptor->functionBlocks.size()
                << " deps=" << result.descriptor->dependencies.size() << "\n";

      return result.ok() ? 0 : 1;
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
      std::map<std::string, std::string> sources;
      int successCount = 0;
      int failCount = 0;

      for (const auto& file : stFiles) {
         fs::path filePath(file);
         if (verbose) {
            std::cout << "Parsing: " << filePath.filename().string() << "\n";
         }

         try {
            std::string fileSource;
            auto tu = processSingleFile(file, dumpTokens, &fileSource);
            sources.emplace(file, std::move(fileSource));

            // Merge translation units
            mergeTranslationUnit(mergedTu, tu);

            successCount++;
          } catch (const ParseError& e) {
             printParseError(e);
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

      // Drop duplicate declarations shared across standalone workspace files
      deduplicateMergedTu(mergedTu);

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
         auto semanticInfo = runSemanticAnalysis(mergedTu, &libraryRegistry, workspacePath, "", &sources);

       if (strictBlocksGeneration(semanticInfo)) {
          reportGenerationBlocked(semanticInfo);
          return 1;
       }

       CodeGenerator gen;
       gen.setNamespace(namespaceName);
       gen.setRuntimeHeader(runtimeHeader);
       gen.setCaseSensitive(caseSensitive);
       gen.setProcessImageConfig(piConfig);
       gen.setSemanticInfo(&semanticInfo);
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
            std::string fileSource;
            auto tu = processSingleFile(file, dumpTokens, &fileSource);

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

std::map<std::string, std::string> oneSource{{file, std::move(fileSource)}};
auto semanticInfo = runSemanticAnalysis(tu, &libraryRegistry, file, fileSource, &oneSource);

             if (strictBlocksGeneration(semanticInfo)) {
                reportGenerationBlocked(semanticInfo);
                failCount++;
                continue;
             }

            CodeGenerator gen;
            gen.setProcessImageConfig(piConfig);
            gen.setSemanticInfo(&semanticInfo);
            auto result = gen.generate(tu, headerFilename, namespaceName, runtimeHeader, caseSensitive);

            writeFileInDir(outputDir, headerFilename, result.headerCode);
            writeFileInDir(outputDir, sourceFilename, result.sourceCode);

            if (verbose) {
               std::cout << "    -> " << outputDir << "/" << headerFilename << "\n";
               std::cout << "    -> " << outputDir << "/" << sourceFilename << "\n";
            }

            successCount++;
          } catch (const ParseError& e) {
             printParseError(e);
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
      std::string sourceText;
      auto tu = processSingleFile(inputPath, dumpTokens, &sourceText);

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

      auto semanticInfo = runSemanticAnalysis(tu, &libraryRegistry, inputPath, sourceText);

       if (strictBlocksGeneration(semanticInfo)) {
          reportGenerationBlocked(semanticInfo);
          return 1;
       }

      CodeGenerator gen;
      gen.setProcessImageConfig(piConfig);
      gen.setSemanticInfo(&semanticInfo);
      auto result = gen.generate(tu, headerName, namespaceName, runtimeHeader, caseSensitive);

      writeFile(outputHpp, result.headerCode);
      writeFile(outputCpp, result.sourceCode);

      std::cout << "Generated: " << outputHpp << "\n";
      std::cout << "Generated: " << outputCpp << "\n";
      std::cout << "\nTo compile the output:\n";
      std::cout << "  g++ -std=c++17 -I<path-to-runtime/include> " << outputCpp << " -o your_program\n";

    } catch (const ParseError& e) {
       printParseError(e);
       return 1;
   } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << "\n";
      return 1;
   }

   return 0;
}