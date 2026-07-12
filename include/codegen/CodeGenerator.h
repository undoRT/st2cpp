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

// ============================================================================
//  Address Allocator - Gestisce l'allocazione di indirizzi AT con placeholder '*'
// ============================================================================

/**
 * @brief Manages allocation of AT addresses with placeholder '*'
 * 
 * Handles:
 * - Fixed addresses (e.g., %IX0.0) - marked as occupied
 * - Placeholder addresses (e.g., %IX*) - allocated to first free position
 * - Proper alignment for multi-byte types (WORD aligned to 2, DWORD to 4, etc.)
 * - Memory region expansion when needed
 */
class AddressAllocator
{
public:
   /**
    * @brief Result of an address allocation
    */
   struct AllocationResult
   {
      int byteOffset;
      int bitOffset; // -1 if not a bit access
      bool success;
   };

   /**
    * @brief Information about a memory region
    */
   struct MemoryRegion
   {
      size_t size;                    // Current size in bytes
      std::vector<bool> byteOccupied; // true if byte is occupied
      std::vector<bool> bitOccupied;  // true if bit is occupied (for BIT access)
   };

   AddressAllocator();

   /**
    * @brief Mark a fixed address as occupied
    * @param addr The address to mark
    * @param sizeInBytes Size of the variable in bytes
    */
   void markFixedAddress(const AddressExpr& addr, int sizeInBytes);

   /**
    * @brief Allocate a placeholder address
    * @param area Memory area (INPUT, OUTPUT, MARKER)
    * @param qualifier Address qualifier (BIT, BYTE, WORD, DWORD, LWORD)
    * @param sizeInBytes Size of the variable in bytes
    * @param alignment Required alignment in bytes
    * @return AllocationResult with the allocated offset
    */
   AllocationResult allocatePlaceholder(AddressExpr::AddressType area,
                                        AddressExpr::AddressQualifier qualifier,
                                        int sizeInBytes,
                                        int alignment);

   /**
    * @brief Get the current size of a memory region
    * @param area Memory area
    * @return Size in bytes
    */
   size_t getRegionSize(AddressExpr::AddressType area) const;

   /**
    * @brief Check if the allocator has any allocations
    */
   bool hasAllocations() const { return !m_regions.empty(); }

private:
   std::unordered_map<AddressExpr::AddressType, MemoryRegion> m_regions;

   /**
    * @brief Ensure a region exists with at least the specified size
    */
   void ensureRegion(AddressExpr::AddressType area, size_t minSize);

   /**
    * @brief Expand a region to accommodate more allocations
    */
   void expandRegion(AddressExpr::AddressType area, size_t newSize);

   /**
    * @brief Align a value to the next multiple of alignment
    */
   size_t alignUp(size_t value, size_t alignment) const;

   /**
    * @brief Get the size of a region type
    */
   size_t getDefaultRegionSize(AddressExpr::AddressType area) const;
};

// ============================================================================
//  Scope Manager
// ============================================================================

/**
 * @brief Manages the symbol scope stack for code generation.
 *
 * Each scope holds:
 * - variable name → C++ type
 * - variable name → AT address string (if any)
 * - temporary counters for output parameters
 * - base class name for SUPER^ calls
 *
 * Lookup searches from the innermost scope outward.
 */
class ScopeManager
{
public:
   struct VarInfo
   {
      std::string type;
      std::optional<std::string> atAddress;
      bool isFunctionLocal = false;
   };

   // --- Scope lifecycle ---
   void pushScope(); ///< Enter a new scope (POU, method, etc.)
   void popScope();  ///< Leave the current scope

   // --- Variable registration ---
   void addVariable(const std::string& name, const std::string& cppType);
   void addATVariable(const std::string& name, const std::string& atAddress);

   // --- Lookup (innermost first) ---
   std::optional<std::string> lookupVariable(const std::string& name) const;
   std::optional<std::string> lookupATAddress(const std::string& name) const;
   std::optional<VarInfo> lookupVariableInfo(const std::string& name) const;

   // --- Base class for SUPER^ ---
   void setBaseClass(const std::string& base);
   std::string getBaseClass() const;

   // --- Temporary counters (for output parameters) ---
   int getNextTempCounter(const std::string& baseName);

   // --- To analyze function AT Statement variables ---
   void setFunctionScope(bool isFunc);
   bool isFunctionScope() const;
   void setLocalToFunction(bool isLocal);
   bool isLocalToFunction() const;

private:
   struct Scope
   {
      std::unordered_map<std::string, std::string> vars;
      std::unordered_map<std::string, std::string> atAddrs;
      std::unordered_map<std::string, int> tempCounters;
      std::string baseClass;
      bool isFunctionScope = false;
      bool isLocalToFunction = false;
   };

   std::vector<Scope> m_scopes; // index 0 = global scope
};

// ============================================================================
//  CodeGenerator
// ============================================================================

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

   // Handler for variables scope
   ScopeManager m_scope;

   std::unordered_map<std::string, FunctionSignature> m_signatures;
   std::unordered_map<std::string, std::unordered_set<std::string>> m_enumValues; // enum name -> set of enumerators
   std::unordered_map<std::string, std::string> m_enumeratorToEnum;               // enumerator -> enum name (O(1) lookup)
   std::unordered_map<std::string, FunctionSignature> m_methodSignatures;
   std::unordered_map<std::string, bool> m_enumTypes; // enum name -> isScoped
   std::unordered_set<std::string> m_structTypes;
   std::unordered_map<std::string, std::vector<std::string>> m_structMembers; // struct name -> ordered members list
   std::string m_currentFunctionReturnType;                                   // Empty if void, otherwise the return type
   ProjectStyle m_projectStyle = ProjectStyle::FLAT;                          // Current mode
   std::string m_outputDir;                                                   // Output directory
   std::unordered_map<std::string, POU> m_fbMap;                              // FB name -> POU
   std::unordered_map<std::string, bool> m_isFB;                              // Type name -> is FB
   ProcessImageConfig m_piConfig;
   bool m_hasAddresses{false};
   AddressAllocator m_addressAllocator; // Address allocator for AT placeholders
   std::unordered_map<std::string, AddressExpr> m_resolvedATAddresses;

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
   std::string generateAddressAccess(const AddressExpr& addr, const TypeRef* type) const;
   std::string generateAddressWrite(const AddressExpr& addr, const std::string& value);
   std::string generateAddressWrite(const AddressExpr& addr, const std::string& value, const TypeRef* type) const;
   int getTypeSizeInBytes(const TypeRef& tr) const;
   int getTypeAlignment(const TypeRef& tr) const;

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

   std::vector<StructInitExpr::MemberInit> orderStructMembers(const std::vector<StructInitExpr::MemberInit>& members,
                                                              const std::string& structName);
   std::string generateOrderedStructInit(const TypeRef& type, const std::shared_ptr<Expr>& initExpr);

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