/**
 * @file CodeGenerator.h
 * @brief Code generator interface for translating AST to C++ code
 *
 * This header declares the `CodeGenerator` class which emits C++ header
 * and source strings from a parsed Structured Text `TranslationUnit`.
 * The implementation produces a minimal runtime-compatible C++ output
 * using the header-only types.hpp in `st2cpp_includes/undoCore/include/undoCore/types.hpp`.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */
#pragma once
#include "ast/AST.h"
#include "parser/Parser.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>

/**
 * @struct ParameterInfo
 * @brief Information about a function / FB parameter
 *
 * Stores the parameter name, its type and whether it is an input parameter.
 */
struct ParameterInfo
{
   std::string name;
   TypeRef type;
   bool isInput;
   std::shared_ptr<Expr> defaultValue;
   bool isOutputVar; // true for OUTPUT parameters (passed as pointer)

   // Default constructor
   ParameterInfo() = default;

   // Constructor for easy initialization
   ParameterInfo(const std::string& n, const TypeRef& t, bool input, std::shared_ptr<Expr> def, bool outPtr = false)
      : name(n), type(t), isInput(input), defaultValue(def), isOutputVar(outPtr)
   {}
};

/**
 * @struct FunctionSignature
 * @brief Simplified signature representation for functions and FBs
 *
 * Used by the code generator to map arguments when emitting calls.
 */
struct FunctionSignature
{
   std::string name;
   std::vector<ParameterInfo> parameters;
   TypeRef returnType;
};

/**
 * @struct CodegenResult
 * @brief Holds the generated header and source text
 */
struct CodegenResult
{
   std::string headerCode; // .hpp
   std::string sourceCode; // .cpp
};

/**
 * @brief Type of generated file
 */
enum class GenFileType {
   HEADER, // .hpp
   SOURCE, // .cpp
   MASTER  // aggregator header
};

/**
 * @brief Information about a generated file
 */
struct GeneratedFile
{
   std::string name;    // File name without extension
   std::string content; // File content
   GenFileType type;    // Type of file
   std::string subdir;  // Subdirectory (empty for root)
};

/**
 * @brief Project generation mode
 */
enum class ProjectStyle {
   FLAT,   // All files in same directory (current behavior)
   MODULAR // Organized in subdirectories with master headers
};

/**
 * @brief Configuration for Process Image sizing
 */
struct ProcessImageConfig
{
   size_t inputBytes = 1024;
   size_t outputBytes = 1024;
   size_t markerBytes = 1024;
   bool autoDetect = true;   // Auto-detect from addresses used
   bool useGlobalPI = false; // Project-style: shared process image
   std::string instanceName = "processImage";
};

/**
 * Type simplifier
 */
using BuildStructDepType = std::unordered_map<std::string, std::unordered_set<std::string>>;

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
   // ============================================================================
   //  Top-level entry
   // ============================================================================

   CodegenResult generate(const TranslationUnit& tu,
                          const std::string& headerName,
                          const std::string& namespaceName = "undoCore",
                          const std::string& runtimeHeader = "undoCore/types.hpp",
                          bool caseSensitive = false);

   std::vector<GeneratedFile> generateModularProject(const TranslationUnit& tu, const std::string& outputDir);
   void setNamespace(const std::string& ns) { m_namespace = ns; }
   void setRuntimeHeader(const std::string& rt) { m_runtimeHeader = rt; }
   void setCaseSensitive(const bool caseSensitive) { m_caseSensitive = caseSensitive; }
   void setProcessImageConfig(const ProcessImageConfig& config) { m_piConfig = config; }
   void setProcessImageGlobal(bool isGlobal) { m_piConfig.useGlobalPI = isGlobal; }

private:
   std::string m_namespace;     // Namespace per il codice generato
   std::string m_runtimeHeader; // Header del runtime da includere
   std::ostringstream m_hdr;    // header stream
   std::ostringstream m_src;    // source stream
   std::string m_hdrName;
   std::string m_currentFunctionName; // Useful for return variable of functions
   int m_indent = 0;
   bool m_caseSensitive;        // true if identifiers should preserve original case, false to convert to uppercase (default for PLCs)
   std::string m_currentFBBase; // Base function block named by SUPER^
   std::string m_currentBaseClass;

   std::unordered_map<std::string, FunctionSignature> m_signatures;
   std::unordered_map<std::string, std::unordered_set<std::string>> m_enumValues; // enum name -> set of enumerators
   std::unordered_map<std::string, std::string> m_enumeratorToEnum;               // enumerator -> enum name (O(1) lookup)
   std::unordered_map<std::string, FunctionSignature> m_methodSignatures;
   std::unordered_map<std::string, std::string> m_varTypes;              // variable name -> type name (for FB instances)
   std::unordered_map<std::string, bool> m_enumTypes;                    // enum name -> isScoped
   std::unordered_map<std::string, int> m_tempCounter;                   // Current scope counters
   std::string m_currentFunctionReturnType;                              // Empty if void, otherwise the return type
   std::vector<std::unordered_map<std::string, int>> m_tempCounterStack; // Stack for nested scopes
   ProjectStyle m_projectStyle = ProjectStyle::FLAT;                     // Current mode
   std::string m_outputDir;                                              // Output directory
   std::unordered_map<std::string, POU> m_fbMap;                         // FB name -> POU
   std::unordered_map<std::string, bool> m_isFB;                         // Type name -> is FB
   ProcessImageConfig m_piConfig;
   bool m_hasAddresses{false};
   std::unordered_map<std::string, std::string> m_localVarTypes;     // local variables of current POU
   std::unordered_map<std::string, std::string> m_globalVarTypes;    // global variables
   std::unordered_map<std::string, std::string> m_localAtVariables;  // local AT variables
   std::unordered_map<std::string, std::string> m_globalAtVariables; // global AT variables

   // ============================================================================
   //  Signature collection
   // ============================================================================

   void collectSignature(const POU& pou);

   // ============================================================================
   //  POU dispatch
   // ============================================================================

   void genPOU(const POU& pou);

   // ============================================================================
   //  FUNCTION BLOCK generation
   // ============================================================================

   void genFunctionBlock(const POU& pou);

   // ============================================================================
   //  FUNCTION generation
   // ============================================================================

   void emitFunctionDecl(const POU& pou, std::ostringstream& out);
   void genFunction(const POU& pou);

   // ============================================================================
   //  PROGRAM generation
   // ============================================================================

   void genProgram(const POU& pou);

   // ============================================================================
   //  STRUCT and ENUM generation
   // ============================================================================

   void genStruct(const StructType& st);
   void genInterface(const Interface& iface);
   void generateStructsInOrder(const std::vector<StructType>& structs, std::ostringstream* out = nullptr);
   BuildStructDepType buildStructDependenciesForStructs(const std::vector<StructType>& structs);
   void genEnum(const EnumType& et);

   // ============================================================================
   //  GLOBALS generation
   // ============================================================================

   void genGlobals(const std::vector<VarSection>& globals);

   // ============================================================================
   //  METHOD generation
   // ============================================================================

   void genMethodDeclaration(const Method& method);
   void genMethodDefinition(const std::string& fbName, const Method& method);

   // ============================================================================
   //  Member declaration helper
   // ============================================================================

   std::string memberDecl(const VarDecl& d);

   // ============================================================================
   //  Type mapping utilities
   // ============================================================================

   std::string mapBaseType(BaseType b) const;
   std::string getArrayType(const std::string& base, const TypeRef& tr) const;
   std::string mapType(const TypeRef& tr) const;
   std::string getBaseTypeName(const TypeRef& tr) const;
   std::string getBaseFBName(const std::string& calleeName) const;
   bool isVoidType(const TypeRef& tr) const;

   // ============================================================================
   //  Normalization utilities
   // ============================================================================

   std::string normalize(const std::string& str) const;
   std::string normalizeType(const std::string& str) const;
   std::string normalizeIdent(const std::string& str) const;

   // ============================================================================
   //  Statement generation
   // ============================================================================

   void genStmt(const Stmt& stmt);
   void genIf(const IfStmt& s);
   void genFor(const ForStmt& s);
   void genWhile(const WhileStmt& s);
   void genRepeat(const RepeatStmt& s);
   void genCase(const CaseStmt& s);

   // ============================================================================
   //  Temporary Variable Management
   // ============================================================================

   void pushTempScope();
   void popTempScope();
   std::string getUniqueTempName(const std::string& baseName);

   // ============================================================================
   //  Expression generation
   // ============================================================================

   std::string genExpr(const Expr& expr);

   // ============================================================================
   //  Address and process image
   // ============================================================================
   std::string generateAddressAccess(const AddressExpr& addr);
   std::string generateAddressWrite(const AddressExpr& addr, const std::string& value);
   bool isATVariable(const std::string& varName) const;

   // ============================================================================
   //  Project-style generation
   // ============================================================================

   std::unordered_map<std::string, std::unordered_set<std::string>> buildFBDependencies(const TranslationUnit& tu);
   std::vector<std::string> topologicalSort(const std::unordered_map<std::string, std::unordered_set<std::string>>& dependencies);
   std::vector<GeneratedFile> generateModular(const TranslationUnit& tu, const std::string& outputDir);
   bool structContainsFB(const std::string& structName, const TranslationUnit& tu) const;
   std::string generateSimpleGVLsHeader(const TranslationUnit& tu);
   std::string generateGVLsHeader(const TranslationUnit& tu);
   std::string generateGVLsSource(const TranslationUnit& tu);
   std::string generateFunctionBlocksMaster(const std::vector<std::string>& fbNames);
   std::string generateFBHeader(const POU& pou, const std::unordered_set<std::string>& dependencies);
   std::unordered_map<std::string, POU> collectFunctionBlocks(const TranslationUnit& tu);
   std::string generateFunctionsHeader(const TranslationUnit& tu);
   std::string generateFunctionsSource(const TranslationUnit& tu);
   std::string generateFBSource(const POU& pou);
   std::string generateProgramsMaster(const std::vector<std::string>& progNames);
   std::string generateProgramHeader(const POU& pou);
   std::string generateProgramSource(const POU& pou);
   BuildStructDepType buildStructDependencies(const TranslationUnit& tu);
   std::vector<std::string> topologicalSortStructs(const BuildStructDepType& dependencies);
   // Body generation helpers
   std::string generateFunctionBody(const POU& pou);
   std::string generateMethodBody(const Method& method);
   std::string generateFBOperatorBody(const POU& pou);
   std::string generateProgramBody(const POU& pou);

   // ============================================================================
   //  Helper
   // ============================================================================

   std::string generateHeaderComment() const;
   std::string ind() const { return std::string(m_indent * 4, ' '); }
   void push() { ++m_indent; }
   void pop() { --m_indent; }
};

/**
 * @brief Analyzer for detecting Process Image sizes from AST
 */
class ProcessImageAnalyzer
{
public:
   struct AddressInfo
   {
      AddressExpr::AddressType type;
      int maxByteOffset = 0;
      int maxBitOffset = 0;
      bool hasBitAccess = false;
   };

   void analyze(const TranslationUnit& tu);
   ProcessImageConfig getRecommendedConfig() const;
   bool hasAddresses() const { return !m_addressInfos.empty(); }

private:
   std::vector<AddressInfo> m_addressInfos;
   std::unordered_map<AddressExpr::AddressType, AddressInfo> m_typeMap;

   void findAddresses(const std::shared_ptr<Stmt>& stmt);
   void findAddressesInExpr(const std::shared_ptr<Expr>& expr);
   void updateMaxOffset(const AddressExpr& addr);
   size_t nextPowerOfTwo(size_t n) const;
};