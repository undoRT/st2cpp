/**
 * @file CodeGenerator.cpp
 * @brief Implementation of the code generator that emits C++ from the AST
 *
 * Converts `TranslationUnit`, `POU`, `StructType` and `EnumType` nodes into
 * C++ header and source text. The implementation is intentionally minimal and
 * focuses on readable generated output rather than optimization.
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/CodeGenerator.h"
#include <stdexcept>
#include <algorithm>
#include <queue>

// ============================================================================
//  Top-level entry
// ============================================================================

/**
 * @brief Main entry point for code generation
 *
 * Converts an entire translation unit (AST) into C++ header and source code.
 * This method orchestrates the entire generation process:
 * - Creates header guards and includes
 * - Opens namespaces if specified
 * - Generates global variables, enums, structs, and POUs in order
 * - Closes namespaces and returns the generated code
 *
 * @param tu The translation unit AST to convert
 * @param headerName Name of the header file (used for include guards)
 * @param namespaceName C++ namespace to wrap generated code (defaults to "undoCore")
 * @param runtimeHeader Path to runtime header to include (defaults to "undoCore/undoCore.hpp")
 * @param caseSensitive If true, preserves original case; if false, converts identifiers to uppercase
 * @return CodegenResult containing both header and source code strings
 */
CodegenResult CodeGenerator::generate(const TranslationUnit& tu,
                                      const std::string& headerName,
                                      const std::string& namespaceName,
                                      const std::string& runtimeHeader,
                                      bool caseSensitive)
{
   m_hdrName = headerName;
   m_namespace = namespaceName;
   m_runtimeHeader = runtimeHeader;
   m_caseSensitive = caseSensitive;

   // Build header guard from filename
   std::string guard = headerName;
   std::transform(guard.begin(), guard.end(), guard.begin(), ::toupper);
   for (auto& c : guard) {
      if (c == '.' || c == '/' || c == '-') {
         c = '_';
      }
   }
   guard += "_HPP";

   // Check if any AT addresses are used
   for (const auto& pou : tu.pous) {
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               m_hasAddresses = true;
               break;
            }
         }
         if (m_hasAddresses) {
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }
   // Also check globals
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_hasAddresses = true;
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }

   // Emit header prolog
   m_hdr << generateHeaderComment();
   m_hdr << "#pragma once\n";
   m_hdr << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   if (m_runtimeHeader.find('/') != std::string::npos || m_runtimeHeader.find('\\') != std::string::npos) {
      // Path with slashes - include with full path
      m_hdr << "#include \"" << m_runtimeHeader << "\"\n\n";
   } else {
      // Just filename - include with relative path
      m_hdr << "#include \"" << m_runtimeHeader << "\"\n\n";
   }

   // print comment about auto-generated code
   m_src << generateHeaderComment();
   // Open namespace if specified
   if (m_namespace.empty()) {
      m_src << "#include \"" << headerName << "\"\n\n";
   } else {
      m_hdr << "namespace " << m_namespace << " {\n\n";
      m_src << "#include \"" << headerName << "\"\n\n";
      m_src << "namespace " << m_namespace << " {\n\n";
   }

   // Generate Global process image
   if (m_hasAddresses) {
      m_hdr << "// Global Process Image instance\n";
      m_hdr << "inline ProcessImage<" << m_piConfig.inputBytes << ", " << m_piConfig.outputBytes << ", " << m_piConfig.markerBytes << "> "
            << m_piConfig.instanceName << ";\n\n";
   }
   // Generate all AST components in dependency order
   for (const auto& en : tu.enums) {
      genEnum(en); // Enums first (always independent)
   }

   // Generate interface
   for (const auto& iface : tu.interfaces) {
      genInterface(iface);
   }

   // Collect FBs first to identify them
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         m_isFB[normalizeType(pou.name)] = true;
      }
      collectSignature(pou);
   }

   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);
         m_varTypes[varName] = varType;
      }
   }

   // Separate structs
   std::vector<StructType> simpleStructs;  // Without FB members
   std::vector<StructType> complexStructs; // With FB members

   for (const auto& st : tu.structs) {
      if (structContainsFB(normalizeType(st.name), tu)) {
         complexStructs.push_back(st);
      } else {
         simpleStructs.push_back(st);
      }
   }

   // Separate globals
   std::vector<VarSection> simpleGlobals;
   std::vector<VarSection> complexGlobals;

   for (const auto& sec : tu.globals) {
      VarSection simpleSec;
      VarSection complexSec;
      simpleSec.kind = sec.kind;
      complexSec.kind = sec.kind;

      for (const auto& d : sec.decls) {
         bool isComplex = false;
         if (d.type.base == BaseType::NAMED) {
            std::string typeName = normalizeType(d.type.name);
            if (m_isFB.find(typeName) != m_isFB.end()) {
               isComplex = true;
            }
         }
         // Also check array of FB
         if (!isComplex && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
            std::string typeName = normalizeType(d.type.name);
            if (m_isFB.find(typeName) != m_isFB.end()) {
               isComplex = true;
            }
         }

         if (isComplex) {
            complexSec.decls.push_back(d);
         } else {
            simpleSec.decls.push_back(d);
         }
      }

      if (!simpleSec.decls.empty()) {
         simpleGlobals.push_back(simpleSec);
      }
      if (!complexSec.decls.empty()) {
         complexGlobals.push_back(complexSec);
      }
   }

   // Generate simple structs (without FB)
   if (!simpleStructs.empty()) {
      generateStructsInOrder(simpleStructs);
   }

   // Generate simple globals (without FB)
   if (!simpleGlobals.empty()) {
      genGlobals(simpleGlobals);
      m_hdr << "\n";
   }

   // Generate POUs in correct dependency order: FBs first, then Functions, then Programs
   // First pass: generate all Function Blocks
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         genPOU(pou);
      }
   }

   // Generate complex structs (with FB members) - after FBs are defined
   if (!complexStructs.empty()) {
      m_hdr << "// Complex STRUCTs (containing Function Blocks)\n";
      generateStructsInOrder(complexStructs);
   }

   // Generate complex globals (with FB types) - after FBs are defined
   if (!complexGlobals.empty()) {
      m_hdr << "// Complex GLOBAL VARIABLES (containing Function Blocks)\n";
      genGlobals(complexGlobals);
      m_hdr << "\n";
   }

   // Second pass: generate all Functions
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         genPOU(pou);
      }
   }

   // Third pass: generate all Programs
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::PROGRAM) {
         genPOU(pou);
      }
   }

   // Close namespace if it was opened
   if (!m_namespace.empty()) {
      m_hdr << "} // namespace " << m_namespace << "\n";
      m_src << "} // namespace " << m_namespace << "\n";
   }

   return {m_hdr.str(), m_src.str()};
}

/**
 * @brief Generate code in modular project style (writes to filesystem)
 * @param tu Translation unit
 * @param outputDir Output directory path
 * @return Vector of generated file information
 */
std::vector<GeneratedFile> CodeGenerator::generateModularProject(const TranslationUnit& tu, const std::string& outputDir)
{
   m_projectStyle = ProjectStyle::MODULAR;
   m_outputDir = outputDir;

   // Clear internal streams (they won't be used in modular mode)
   m_hdr.str("");
   m_src.str("");
   m_hdr.clear();
   m_src.clear();

   // Reset state
   m_signatures.clear();
   m_methodSignatures.clear();
   m_varTypes.clear();
   m_enumTypes.clear();
   m_enumValues.clear();
   m_enumeratorToEnum.clear();
   m_fbMap.clear();
   m_isFB.clear();
   m_tempCounter.clear();
   m_tempCounterStack.clear();
   m_globalAtVariables.clear();
   m_localVarTypes.clear();
   m_localAtVariables.clear();

   return generateModular(tu, outputDir);
}

// ============================================================================
//  Signature collection
// ============================================================================

/**
 * @brief Collect function/method signature from a POU for later reference
 *
 * This method extracts the complete signature of a POU (parameters, return type)
 * and stores it in internal maps. Signatures are used during code generation
 * to:
 * - Validate function calls
 * - Handle named vs positional arguments
 * - Generate proper argument ordering
 * - Create temporary variables for output parameters
 *
 * For function blocks, it also collects signatures of all methods.
 *
 * @param pou The POU to analyze and collect signature from
 */
void CodeGenerator::collectSignature(const POU& pou)
{
   FunctionSignature sig;
   sig.name = normalizeIdent(pou.name);

   // Function blocks have void return type (they are stateful)
   if (pou.kind == POUKind::FUNCTION_BLOCK) {
      sig.returnType.base = BaseType::VOID;
   } else {
      sig.returnType = pou.returnType;
   }

   // Build parameter list from variable sections
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::INPUT) {
         // Input parameters: passed by value
         for (const auto& d : sec.decls) {
            sig.parameters.push_back({normalizeIdent(d.name), d.type, true, d.initialValue, false});
         }
      } else if (sec.kind == VarKind::OUTPUT) {
         // Output parameters: passed by reference
         for (const auto& d : sec.decls) {
            TypeRef refType = d.type;
            refType.isRefTo = true;
            sig.parameters.push_back({normalizeIdent(d.name), refType, false, d.initialValue, true});
         }
      } else if (sec.kind == VarKind::IN_OUT) {
         // In-out parameters: passed by reference, required (no default)
         for (const auto& d : sec.decls) {
            sig.parameters.push_back({normalizeIdent(d.name), d.type, false, nullptr, false});
         }
      }
   }

   m_signatures[sig.name] = sig;

   // For FUNCTION_BLOCK, collect method signatures as well
   if (pou.kind == POUKind::FUNCTION_BLOCK) {
      for (const auto& method : pou.methods) {
         FunctionSignature methodSig;
         methodSig.name = normalizeIdent(pou.name) + "::" + normalizeIdent(method.name);
         methodSig.returnType = method.returnType;

         // Collect method parameters
         for (const auto& param : method.parameters) {
            bool isOutputVar = (param.kind == VarKind::OUTPUT);
            methodSig.parameters.push_back(
               {normalizeIdent(param.name), param.type, param.kind == VarKind::INPUT, param.initialValue, isOutputVar});
         }

         m_methodSignatures[methodSig.name] = methodSig;
      }
   }
}

// ============================================================================
//  POU dispatch
// ============================================================================

/**
 * @brief Dispatch POU generation based on its kind
 *
 * This is the main dispatcher for Program Organization Units.
 * It first collects the signature, then calls the appropriate
 * generation method based on the POU type.
 *
 * @param pou The POU to generate code for
 */
void CodeGenerator::genPOU(const POU& pou)
{
   switch (pou.kind) {
   case POUKind::FUNCTION_BLOCK:
      genFunctionBlock(pou);
      break;
   case POUKind::FUNCTION:
      genFunction(pou);
      break;
   case POUKind::PROGRAM:
      genProgram(pou);
      break;
   }
}

// ============================================================================
//  FUNCTION BLOCK generation
// ============================================================================

/**
 * @brief Generate C++ struct for a Function Block
 *
 * Function blocks are emitted as C++ structs with:
 * - Member variables for all VAR sections
 * - Setter/getter methods for input/output parameters
 * - A default constructor
 * - An operator() for execution
 * - Methods for any defined FB methods
 *
 * In the source file, the operator() implementation and method definitions
 * are generated with local VAR_TEMP variables.
 *
 * @param pou The FUNCTION_BLOCK POU to generate
 */
void CodeGenerator::genFunctionBlock(const POU& pou)
{
   // Register FB local variables for method calls (important for flat mode)
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         m_varTypes[normalizeIdent(d.name)] = mapType(d.type);
      }
   }

   std::string upperName = normalizeType(pou.name);

   // ========== HEADER GENERATION ==========
   m_hdr << "// FUNCTION BLOCK " << upperName << "\n";
   m_hdr << "struct " << upperName;

   // Base struct (EXTENDS)
   if (!pou.extends.empty()) {
      m_hdr << " : public " << normalizeType(pou.extends);
   }

   // Interfacce (IMPLEMENTS)
   for (size_t i = 0; i < pou.implements.size(); ++i) {
      if (i == 0 && pou.extends.empty()) {
         m_hdr << " : public " << normalizeType(pou.implements[i]);
      } else {
         m_hdr << ", public " << normalizeType(pou.implements[i]);
      }
   }

   m_hdr << " {\n";
   m_hdr << "public:\n";
   push();

   // Emit all variable sections as members
   for (const auto& sec : pou.varSections) {
      std::string secLabel;
      switch (sec.kind) {
      case VarKind::INPUT:
         secLabel = "// VAR_INPUT";
         break;
      case VarKind::OUTPUT:
         secLabel = "// VAR_OUTPUT";
         break;
      case VarKind::IN_OUT:
         secLabel = "// VAR_IN_OUT";
         break;
      case VarKind::EXTERNAL:
         secLabel = "// VAR_EXTERNAL";
         break;
      case VarKind::GLOBAL:
         secLabel = "// VAR_GLOBAL";
         break;
      case VarKind::TEMP:
         secLabel = "// VAR_TEMP (local)";
         break;
      default:
         secLabel = "// VAR";
         break;
      }
      m_hdr << ind() << secLabel << "\n";

      for (const auto& d : sec.decls) {
         std::string ctype = mapType(d.type);
         std::string upperName_inst = normalizeIdent(d.name);
         std::string init = d.initialValue ? "{" + genExpr(*d.initialValue) + "}" : "{}";

         if (sec.kind == VarKind::IN_OUT) {
            // IN_OUT variables are wrapped in VAR_INOUT template
            m_hdr << ind() << "VAR_INOUT<" << ctype << "> " << upperName_inst << init << ";\n";
         } else {
            m_hdr << ind() << memberDecl(d) << ";\n";
         }
      }
   }

   // Generate setter/getter methods for each variable
   m_hdr << "\n";
   m_hdr << "// SETTER/GETTER\n";
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string ctype = mapType(d.type);
         std::string upperName_inst = normalizeIdent(d.name);
         if (sec.kind == VarKind::IN_OUT) {
            m_hdr << ind() << "inline void set_" << upperName_inst << "(" << ctype << "& refVal) { " << upperName_inst
                  << " = &refVal; }\n";
         } else if (sec.kind == VarKind::INPUT) {
            m_hdr << ind() << "inline void set_" << upperName_inst << "(" << ctype << " val) { " << upperName_inst << " = val; }\n";
         } else if (sec.kind == VarKind::OUTPUT) {
            m_hdr << ind() << "inline " << ctype << " get_" << upperName_inst << "() const { return " << upperName_inst << "; }\n";
         }
      }
   }
   m_hdr << "// End SETTER/GETTER\n\n";

   // Constructor, execution operator, and method declarations
   m_hdr << ind() << upperName << "();\n";
   m_hdr << ind() << "void operator()();\n";
   for (const auto& method : pou.methods) {
      genMethodDeclaration(method);
   }
   m_hdr << ind() << "virtual ~" << upperName << "() = default;\n";
   pop();
   m_hdr << "};\n\n";

   // ========== SOURCE GENERATION ==========
   // Constructor implementation
   m_src << "// FUNCTION BLOCK " << upperName << "\n";
   m_src << upperName << "::" << upperName << "() {}\n\n";

   // operator() implementation
   m_src << "void " << upperName << "::operator()() {\n";
   push();

   // Emit VAR_TEMP as local variables within operator()
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            std::string ctype = mapType(d.type);
            std::string upperName_inst = normalizeIdent(d.name);
            std::string init = d.initialValue ? "{" + genExpr(*d.initialValue) + "}" : "{}";
            m_src << ind() << ctype << " " << upperName_inst << init << ";\n";
         }
      }
   }

   // Generate the FB body statements
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }
   pop();
   m_src << "}\n";

   if (!pou.extends.empty()) {
      m_currentBaseClass = normalizeType(pou.extends);
   } else {
      m_currentBaseClass.clear();
   }

   // Generate method definitions
   for (const auto& method : pou.methods) {
      genMethodDefinition(upperName, method);
   }
   m_src << "// END FUNCTION BLOCK " << upperName << "\n\n";
}

// ============================================================================
//  FUNCTION generation
// ============================================================================

/**
 * @brief Emit function declaration (header or source)
 *
 * Generates the C++ function signature including return type,
 * function name, and parameter list. Handles INPUT (by value),
 * OUTPUT/IN_OUT (by reference) parameters appropriately.
 *
 * @param pou The FUNCTION POU to generate declaration for
 * @param out Output stream to write the declaration to
 */
void CodeGenerator::emitFunctionDecl(const POU& pou, std::ostringstream& out)
{
   std::string retType = "void";
   if (!isVoidType(pou.returnType)) {
      retType = mapType(pou.returnType);
   }

   out << retType << " " << normalizeIdent(pou.name) << "(";
   bool first = true;

   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::INPUT) {
         // Input parameters: pass by value
         for (const auto& d : sec.decls) {
            if (!first) {
               out << ", ";
            }
            first = false;
            out << mapType(d.type) << " " << normalizeIdent(d.name);
         }
      } else if (sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
         // Output and In-Out parameters: pass by reference
         for (const auto& d : sec.decls) {
            if (!first) {
               out << ", ";
            }
            first = false;
            out << mapType(d.type) << "& " << normalizeIdent(d.name);
         }
      }
   }
   out << ")";
}

/**
 * @brief Generate C++ function from a FUNCTION POU
 *
 * Emits both declaration (in header) and definition (in source).
 * The function follows IEC 61131-3 semantics where the function name
 * serves as the return variable. A `_ret` local variable is created
 * to store the return value, and any assignment to the function name
 * is translated to assignment to `_ret`.
 *
 * @param pou The FUNCTION POU to generate
 */
void CodeGenerator::genFunction(const POU& pou)
{
   pushTempScope();
   std::string upperName = normalizeIdent(pou.name);
   m_currentFunctionName = upperName; // Track for _ret translation

   // Save return type
   bool hasReturn = !isVoidType(pou.returnType);
   m_currentFunctionReturnType = hasReturn ? mapType(pou.returnType) : "";

   // Header declaration
   m_hdr << "// FUNCTION " << upperName << "\n";
   emitFunctionDecl(pou, m_hdr);
   m_hdr << ";\n\n";

   // Source definition
   m_src << "// FUNCTION " << upperName << "\n";
   emitFunctionDecl(pou, m_src);
   m_src << " {\n";
   push();

   // Return variable (IEC 61131-3: function name acts as return variable)
   if (hasReturn) {
      m_src << ind() << mapType(pou.returnType) << " " << upperName << "_ret{};\n";
   }

   // Local variables from VAR and VAR_TEMP sections
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::VAR || sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            std::string ctype = mapType(d.type);
            std::string upperName_inst = normalizeIdent(d.name);
            std::string init;
            if (d.initialValue) {
               init = "{" + genExpr(*d.initialValue) + "}";
            } else {
               init = "{}";
            }
            m_src << ind() << ctype << " " << upperName_inst << init << ";\n";
         }
      }
   }

   // Generate function body
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }

   m_currentFunctionName.clear();

   // Return the return variable
   if (hasReturn) {
      m_src << ind() << "return " << upperName << "_ret;\n";
   }

   // Clear return type
   m_currentFunctionReturnType.clear();

   pop();
   m_src << "}\n";
   m_src << "// END FUNCTION " << upperName << "\n\n";
   popTempScope();
}

// ============================================================================
//  PROGRAM generation
// ============================================================================

/**
 * @brief Helper function to parse an IEC address string into an AddressExpr
 * 
 * @param addrStr The address string (e.g., "%IX0.0", "%QW2", "%MW10")
 * @param out The AddressExpr to fill
 * @return true if parsing succeeded, false otherwise
 */
bool parseAddressString(const std::string& addrStr, AddressExpr& out)
{
   // Parse %IX0.0, %QB10, %MW5, etc.
   if (addrStr.size() < 3) {
      return false;
   }

   if (addrStr[0] != '%') {
      return false;
   }

   // Determine type
   switch (addrStr[1]) {
   case 'I':
      out.type = AddressExpr::AddressType::INPUT;
      break;
   case 'Q':
      out.type = AddressExpr::AddressType::OUTPUT;
      break;
   case 'M':
      out.type = AddressExpr::AddressType::MARKER;
      break;
   default:
      return false;
   }

   // Determine qualifier
   switch (addrStr[2]) {
   case 'X':
      out.qualifier = AddressExpr::AddressQualifier::BIT;
      break;
   case 'B':
      out.qualifier = AddressExpr::AddressQualifier::BYTE;
      break;
   case 'W':
      out.qualifier = AddressExpr::AddressQualifier::WORD;
      break;
   case 'D':
      out.qualifier = AddressExpr::AddressQualifier::DWORD;
      break;
   case 'L':
      out.qualifier = AddressExpr::AddressQualifier::LWORD;
      break;
   default:
      return false;
   }

   // Parse offsets
   size_t pos = 3;
   std::string numStr;
   while (pos < addrStr.size() && std::isdigit(addrStr[pos])) {
      numStr += addrStr[pos++];
   }
   if (numStr.empty()) {
      return false;
   }
   out.byteOffset = std::stoi(numStr);

   if (pos < addrStr.size() && addrStr[pos] == '.') {
      pos++;
      numStr.clear();
      while (pos < addrStr.size() && std::isdigit(addrStr[pos])) {
         numStr += addrStr[pos++];
      }
      if (numStr.empty()) {
         return false;
      }
      out.bitOffset = std::stoi(numStr);
   } else {
      out.bitOffset = -1;
   }

   out.rawText = addrStr;
   return true;
}

/**
 * @brief Generate C++ struct for a PROGRAM (FLAT mode)
 *
 * Programs are emitted as structs with:
 * - A ProcessImage member
 * - Member variables for all VAR sections
 * - Getter/setter methods for AT address variables
 * - A run() method containing the program body
 *
 * This approach allows multiple instances of the same program
 * (following IEC 61131-3 semantics where programs can be instantiated).
 *
 * @param pou The PROGRAM POU to generate
 */
void CodeGenerator::genProgram(const POU& pou)
{
   // Register variable types for use in genExpr (for enum resolution)
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);
         // Register as local variable
         m_localVarTypes[varName] = varType;
         m_varTypes[varName] = varType;
         if (!d.atAddress.empty()) {
            m_localAtVariables[varName] = d.atAddress;
         }
      }
   }

   std::string upperName = normalizeType(pou.name);
   m_hdr << "// PROGRAM " << upperName << "\n";
   m_hdr << "struct " << upperName << " {\n";
   push();

   // Generate all member variables
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            std::string varName = normalizeIdent(d.name);
            std::string ctype = mapType(d.type);

            AddressExpr addr;
            if (parseAddressString(d.atAddress, addr)) {
               m_hdr << ind() << "// AT " << d.atAddress << "\n";
               // Getter (const and non-const)
               m_hdr << ind() << "inline " << ctype << " getPi_" << varName << "() const {\n";
               m_hdr << ind() << "    return " << generateAddressAccess(addr) << ";\n";
               m_hdr << ind() << "}\n";
               // Setter
               m_hdr << ind() << "inline void setPi_" << varName << "(" << ctype << " value) {\n";
               m_hdr << ind() << "    " << generateAddressWrite(addr, "value") << ";\n";
               m_hdr << ind() << "}\n";
            } else {
               m_hdr << ind() << memberDecl(d) << ";\n";
            }
         } else {
            m_hdr << ind() << memberDecl(d) << ";\n";
         }
      }
   }

   m_hdr << "\n";
   m_hdr << ind() << "void run();\n";
   pop();
   m_hdr << "};\n\n";

   // Source implementation of run() method
   m_src << "void " << upperName << "::run() {\n";
   push();
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }
   pop();
   m_src << "}\n\n";
}

// ============================================================================
//  STRUCT and ENUM generation
// ============================================================================

/**
 * @brief Generate C++ struct from a STRUCT type
 *
 * Converts a STRUCT declaration from the AST into a C++ struct
 * with all members properly typed and initialized.
 *
 * @param st The StructType AST node to generate
 */
void CodeGenerator::genStruct(const StructType& st)
{
   std::string upperName = normalizeType(st.name);
   m_hdr << "// STRUCT " << upperName << "\n";
   m_hdr << "struct " << upperName << " {\n";
   push();
   for (const auto& member : st.members) {
      std::string ctype = mapType(member.type);
      std::string upperMember = normalizeIdent(member.name);
      std::string init = member.initialValue ? "{" + genExpr(*member.initialValue) + "}" : "{}";
      m_hdr << ind() << ctype << " " << upperMember << init << ";\n";
   }
   pop();
   m_hdr << "};\n\n";
}

/**
 * @brief Generate interface
 * 
 * @param iface struct interface to generate
 */
void CodeGenerator::genInterface(const Interface& iface)
{
   std::string name = normalizeType(iface.name);
   m_hdr << "// INTERFACE " << name << "\n";
   m_hdr << "struct " << name << " {\n";
   push();
   for (const auto& method : iface.methods) {
      genMethodDeclaration(method);
   }
   m_hdr << ind() << "virtual ~" << name << "() = default;\n";
   pop();
   m_hdr << "};\n\n";
}

/**
 * @brief Generate structs in dependency order (topological sort)
 * @param structs List of structs to generate
 */
void CodeGenerator::generateStructsInOrder(const std::vector<StructType>& structs, std::ostringstream* out)
{
   if (structs.empty()) {
      return;
   }
   // If out is nullptr, use m_hdr (FLAT mode) otherwise use out (MODULAR mode)
   // Build dependency graph
   auto structDeps = buildStructDependenciesForStructs(structs);

   // Topological sort
   std::vector<std::string> orderedStructNames;
   try {
      orderedStructNames = topologicalSortStructs(structDeps);
   } catch (const std::runtime_error& e) {
      // Circular dependency detected - fall back to original order
      for (const auto& st : structs) {
         if (out) {
            // Genera struct su out stream
            std::string upperName = normalizeType(st.name);
            *out << "// STRUCT " << upperName << "\n";
            *out << "struct " << upperName << " {\n";
            for (const auto& member : st.members) {
               std::string ctype = mapType(member.type);
               std::string upperMember = normalizeIdent(member.name);
               std::string init = member.initialValue ? "{" + genExpr(*member.initialValue) + "}" : "{}";
               *out << "    " << ctype << " " << upperMember << init << ";\n";
            }
            *out << "};\n\n";
         } else {
            genStruct(st);
         }
      }
      return;
   }

   // Generate in dependency order
   for (const auto& structName : orderedStructNames) {
      for (const auto& st : structs) {
         if (normalizeType(st.name) == structName) {
            if (out) {
               // Genera struct su out stream
               std::string upperName = normalizeType(st.name);
               *out << "// STRUCT " << upperName << "\n";
               *out << "struct " << upperName << " {\n";
               for (const auto& member : st.members) {
                  std::string ctype = mapType(member.type);
                  std::string upperMember = normalizeIdent(member.name);
                  std::string init = member.initialValue ? "{" + genExpr(*member.initialValue) + "}" : "{}";
                  *out << "    " << ctype << " " << upperMember << init << ";\n";
               }
               *out << "};\n\n";
            } else {
               genStruct(st);
            }
            break;
         }
      }
   }
}

/**
 * @brief Build dependency graph for a specific list of structs
 * @param structs List of structs
 * @return Map of struct name -> set of struct names it depends on
 */
BuildStructDepType CodeGenerator::buildStructDependenciesForStructs(const std::vector<StructType>& structs)
{
   BuildStructDepType deps;

   // First, build a set of all struct names in this list for quick lookup
   std::unordered_set<std::string> structNames;
   for (const auto& st : structs) {
      structNames.insert(normalizeType(st.name));
   }

   for (const auto& st : structs) {
      std::string structName = normalizeType(st.name);
      deps[structName]; // Ensure entry exists

      for (const auto& member : st.members) {
         if (member.type.base == BaseType::NAMED) {
            std::string memberType = normalizeType(member.type.name);
            // Only consider dependencies that are structs in our list
            if (structNames.find(memberType) != structNames.end() && memberType != structName) {
               deps[structName].insert(memberType);
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Generate C++ enum class from an ENUM type
 *
 * Converts an ENUM declaration from the AST into a C++11 enum class.
 * Enumerators can have explicit values, and the enum is scoped to
 * avoid name pollution.
 *
 * @param et The EnumType AST node to generate
 */
void CodeGenerator::genEnum(const EnumType& et)
{
   std::string upperName = normalizeType(et.name);
   // Register enum type for special handling in expression generation
   m_enumTypes[upperName] = true;

   // Register each enumerator with its enum name for qualified access
   for (const auto& enumerator : et.enumerators) {
      std::string upperEnumerator = normalizeIdent(enumerator.name);
      m_enumValues[upperName].insert(upperEnumerator);
      m_enumeratorToEnum[upperEnumerator] = upperName;
   }

   m_hdr << "// ENUM " << upperName << "\n";
   m_hdr << "enum class " << upperName << " : int {\n";
   push();
   for (size_t i = 0; i < et.enumerators.size(); ++i) {
      const auto& enumerator = et.enumerators[i];
      std::string upperEnumerator = normalizeIdent(enumerator.name);
      m_hdr << ind() << upperEnumerator;

      // Emit explicit value if present
      if (enumerator.value) {
         m_hdr << " = " << genExpr(*enumerator.value);
      }

      // Add comma for all but last enumerator
      if (i < et.enumerators.size() - 1) {
         m_hdr << ",";
      }
      m_hdr << "\n";
   }
   pop();
   m_hdr << "};\n";
   m_hdr << "// END ENUM " << upperName << "\n\n";
}

// ============================================================================
//  GLOBALS generation
// ============================================================================

/**
 * @brief Generate global variables as inline variables
 *
 * Global variables are emitted as C++17 inline variables in the header.
 * This avoids multiple definition issues when the header is included
 * in multiple translation units. Arrays are emitted using STArray type.
 *
 * For global variables with AT addresses, getter and setter functions
 * are generated instead of direct variable access.
 *
 * @param globals List of global variable sections to generate
 */
void CodeGenerator::genGlobals(const std::vector<VarSection>& globals)
{
   if (globals.empty()) {
      return;
   }

   m_hdr << "// GLOBAL VARIABLES\n";

   for (const auto& sec : globals) {
      for (const auto& d : sec.decls) {
         std::string ctype = mapType(d.type);
         std::string upperName = normalizeIdent(d.name);

         // Register as global variable
         m_globalVarTypes[upperName] = ctype;

         // Global AT address variable - generate getter and setter
         if (!d.atAddress.empty()) {
            AddressExpr addr;
            if (parseAddressString(d.atAddress, addr)) {
               // Register as AT variable
               m_globalAtVariables[upperName] = d.atAddress;

               // Getter
               m_hdr << "// AT " << d.atAddress << "\n";
               m_hdr << "inline " << ctype << " getPi_" << upperName << "() {\n";
               m_hdr << "    return " << generateAddressAccess(addr) << ";\n";
               m_hdr << "}\n";
               // Setter
               m_hdr << "inline void setPi_" << upperName << "(" << ctype << " value) {\n";
               m_hdr << "    " << generateAddressWrite(addr, "value") << ";\n";
               m_hdr << "}\n";
            }
            continue;
         }

         // Normal global variable
         if (!d.type.arrayDims.empty()) {
            // Array global - STArray type already includes bounds
            std::string arrayDecl = ctype + " " + upperName;

            if (d.initialValue) {
               m_hdr << "inline " << arrayDecl << " = " << genExpr(*d.initialValue) << ";\n";
            } else {
               m_hdr << "inline " << arrayDecl << "{};\n";
            }
         } else {
            // Scalar global
            if (d.initialValue) {
               m_hdr << "inline " << ctype << " " << upperName << " = " << genExpr(*d.initialValue) << ";\n";
            } else {
               m_hdr << "inline " << ctype << " " << upperName << "{};\n";
            }
         }
      }
   }
}

// ============================================================================
//  METHOD generation
// ============================================================================

/**
 * @brief Generate method declaration in class header
 *
 * Emits the method signature as it appears in the class definition.
 * Parameters are listed as they appear (INPUT by value, OUTPUT/IN_OUT by reference).
 * No default values are emitted in the declaration.
 *
 * @param method The Method AST node to generate declaration for
 */
void CodeGenerator::genMethodDeclaration(const Method& method)
{
   std::string upperMethodName = normalizeIdent(method.name);
   std::string returnType = "void";

   if (!isVoidType(method.returnType)) {
      returnType = mapType(method.returnType);
   }

   m_hdr << ind() << "virtual " << returnType << " " << upperMethodName << "(";

   bool first = true;

   for (const auto& param : method.parameters) {
      if (!first) {
         m_hdr << ", ";
      }
      first = false;

      std::string type = mapType(param.type);

      if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
         m_hdr << type << "& " << normalizeIdent(param.name);
      } else {
         m_hdr << type << " " << normalizeIdent(param.name);
      }
   }

   m_hdr << ")";

   // If present, override
   if (method.isOverride) {
      m_hdr << " override";
   }

   // If present, final
   if (method.isFinal) {
      m_hdr << " final";
   }

   // If present, abstract
   if (method.isAbstract) {
      m_hdr << " = 0";
   }

   m_hdr << ";\n";
}

/**
 * @brief Generate method definition in source file
 *
 * Emits the complete method implementation including:
 * - Return variable (if function returns a value)
 * - Local variables
 * - Body statements
 *
 * @param fbName Name of the containing function block (already normalized)
 * @param method The Method AST node to generate definition for
 */
void CodeGenerator::genMethodDefinition(const std::string& fbName, const Method& method)
{
   pushTempScope();
   std::string upperMethodName = normalizeIdent(method.name);
   std::string returnType = "void";

   // Save the RETURN type if it is not void
   if (!isVoidType(method.returnType)) {
      returnType = mapType(method.returnType);
   }
   bool hasReturn = !isVoidType(method.returnType);
   m_currentFunctionReturnType = hasReturn ? returnType : "";

   // Method signature
   m_src << returnType << " " << fbName << "::" << upperMethodName << "(";
   bool first = true;

   for (const auto& param : method.parameters) {
      if (!first) {
         m_src << ", ";
      }
      first = false;

      std::string type = mapType(param.type);

      if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
         m_src << type << "& " << normalizeIdent(param.name);
      } else {
         m_src << type << " " << normalizeIdent(param.name);
      }
   }

   m_src << ") {\n";
   push();

   // Return variable (if the method returns a value)
   if (hasReturn) {
      m_src << ind() << returnType << " " << upperMethodName << "_ret{};\n";
   }

   // Local variables within the method
   for (const auto& local : method.localVars) {
      m_src << ind() << memberDecl(local) << ";\n";
   }

   // Generate method body, tracking function name for _ret translation
   std::string savedFunctionName = m_currentFunctionName;
   m_currentFunctionName = upperMethodName;

   for (const auto& stmt : method.body) {
      genStmt(*stmt);
   }

   m_currentFunctionName = savedFunctionName;

   // Return statement if the method has a return value
   if (hasReturn) {
      m_src << ind() << "return " << upperMethodName << "_ret;\n";
   }

   // Clear return type
   m_currentFunctionReturnType.clear();

   pop();
   m_src << "}\n\n";
   popTempScope();
}

// ============================================================================
//  Member declaration helper
// ============================================================================

/**
 * @brief Generate a member variable declaration
 *
 * Creates a C++ member variable declaration with proper type,
 * name, and initializer. Handles both scalar and array types.
 * For arrays, it uses the STArray template type.
 *
 * @param d The variable declaration AST node
 * @return C++ code string for the member declaration
 */
std::string CodeGenerator::memberDecl(const VarDecl& d)
{
   std::string ctype = mapType(d.type);
   std::string upperName = normalizeIdent(d.name);
   std::string init;

   if (!d.type.arrayDims.empty()) {
      // Array member - STArray type already contains bounds
      std::string result = ctype + " " + upperName;

      if (d.initialValue) {
         init = " = " + genExpr(*d.initialValue);
      } else {
         init = "{}";
      }
      return result + init;
   }

   // Scalar member
   if (d.initialValue) {
      init = "{" + genExpr(*d.initialValue) + "}";
   } else if (d.isConstant) {
      init = " = {}";
   } else {
      init = "{}";
   }

   return ctype + " " + upperName + init;
}

// ============================================================================
//  Type mapping utilities
// ============================================================================

/**
 * @brief Map IEC 61131-3 base type to C++ runtime type
 *
 * Converts standard PLC data types to their corresponding
 * C++ runtime library types (e.g., Bool, Int16, UInt32).
 *
 * @param b The base type enumerator
 * @return String representation of the C++ type
 */
std::string CodeGenerator::mapBaseType(BaseType b) const
{
   switch (b) {
   case BaseType::BOOL:
      return "Bool";
   case BaseType::SINT:
      return "Int8";
   case BaseType::INT:
      return "Int16";
   case BaseType::DINT:
      return "Int32";
   case BaseType::LINT:
      return "Int64";
   case BaseType::USINT:
      return "UInt8";
   case BaseType::UINT:
      return "UInt16";
   case BaseType::UDINT:
      return "UInt32";
   case BaseType::ULINT:
      return "UInt64";
   case BaseType::REAL:
      return "Float";
   case BaseType::LREAL:
      return "Double";
   case BaseType::BYTE:
      return "UInt8";
   case BaseType::WORD:
      return "UInt16";
   case BaseType::DWORD:
      return "UInt32";
   case BaseType::LWORD:
      return "UInt64";
   case BaseType::STRING:
      return "String";
   case BaseType::WSTRING:
      return "WString";
   case BaseType::TIME:
      return "UInt32";
   case BaseType::DATE:
      return "UInt32";
   case BaseType::DT:
      return "UInt64";
   case BaseType::TOD:
      return "UInt64";
   case BaseType::VOID:
      return "void";
   case BaseType::NAMED:
      return ""; // Handled by caller
   }
   return "";
}

/**
 * @brief Generate STArray template type for array dimensions
 * 
 * Creates nested STArray templates for multidimensional arrays.
 * 
 * @param baseType The base element type (already normalized)
 * @param tr Type reference containing array dimensions
 * @return C++ type string like "STArray<Int16, 0, 10>" or 
 *         "STArray<STArray<Int16, 0, 4>, 1, 3>"
 */
std::string CodeGenerator::getArrayType(const std::string& baseType, const TypeRef& tr) const
{
   if (tr.arrayDims.empty()) {
      return baseType;
   }

   // Start from the innermost dimension and build outward
   std::string innerType = baseType;

   // Process dimensions from last to first (innermost to outermost)
   for (auto it = tr.arrayDims.rbegin(); it != tr.arrayDims.rend(); ++it) {
      const auto& dim = *it;

      // Extract low and high bounds from AST literals
      std::string low, high;

      // Handle low bound
      if (auto* lit = std::get_if<LiteralExpr>(&dim.low->node)) {
         low = lit->value;
      } else if (auto* unary = std::get_if<UnaryExpr>(&dim.low->node)) {
         // Handle negative numbers: -2
         if (unary->op == "-") {
            if (auto* lit = std::get_if<LiteralExpr>(&unary->operand->node)) {
               low = "-" + lit->value;
            }
         }
      }

      // Handle high bound
      if (auto* lit = std::get_if<LiteralExpr>(&dim.high->node)) {
         high = lit->value;
      } else if (auto* unary = std::get_if<UnaryExpr>(&dim.high->node)) {
         if (unary->op == "-") {
            if (auto* lit = std::get_if<LiteralExpr>(&unary->operand->node)) {
               high = "-" + lit->value;
            }
         }
      }

      // Wrap the current inner type with another STArray
      innerType = "STArray<" + innerType + ", " + low + ", " + high + ">";
   }

   return innerType;
}

/**
 * @brief Map a TypeRef to its complete C++ type string
 *
 * Handles all type modifiers: named types, base types,
 * pointers, references, and array dimensions.
 *
 * @param tr Type reference to map
 * @return Complete C++ type string
 */
std::string CodeGenerator::mapType(const TypeRef& tr) const
{
   std::string base;
   if (tr.base == BaseType::NAMED) {
      base = normalizeType(tr.name);
   } else {
      base = mapBaseType(tr.base);
   }

   if (tr.isPointer) {
      return base + "*";
   }
   if (tr.isRefTo) {
      return base + "&";
   }
   if (!tr.arrayDims.empty()) {
      return getArrayType(base, tr);
   }
   return base;
}

/**
 * @brief Get only the base type name without pointers or references
 *
 * Strips away '*', '&', and array decorations to return just
 * the underlying type name.
 *
 * @param tr Type reference to analyze
 * @return Base type name as string
 */
std::string CodeGenerator::getBaseTypeName(const TypeRef& tr) const
{
   if (tr.base == BaseType::NAMED) {
      return tr.name;
   }
   return mapBaseType(tr.base);
}

/**
 * @param calleeName
 */
std::string CodeGenerator::getBaseFBName(const std::string& calleeName) const
{
   // If it contains "[" it is an arratay access: extract the name before the parenthesis
   size_t bracketPos = calleeName.find('[');
   if (bracketPos != std::string::npos) {
      return calleeName.substr(0, bracketPos);
   }
   return calleeName;
}

/**
 * @brief Check if a type reference represents void
 * @param tr Type reference to check
 * @return true if the type is void
 */
bool CodeGenerator::isVoidType(const TypeRef& tr) const
{
   if (tr.base == BaseType::VOID) {
      return true;
   }
   if (tr.base == BaseType::NAMED && !tr.name.empty()) {
      std::string typeName = normalizeType(tr.name);
      std::transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
      if (typeName == "void") {
         return true;
      }
   }
   return false;
}

// ============================================================================
//  Normalization utilities
// ============================================================================

/**
 * @brief Normalize a string based on case sensitivity setting
 *
 * If case-sensitive mode is enabled, returns the string unchanged.
 * Otherwise, converts the entire string to uppercase.
 * Used as the base normalization function for all identifiers.
 *
 * @param str String to normalize
 * @return Normalized string
 */
std::string CodeGenerator::normalize(const std::string& str) const
{
   if (m_caseSensitive) {
      return str;
   }
   std::string result = str;
   std::transform(result.begin(), result.end(), result.begin(), ::toupper);
   return result;
}

/**
 * @brief Normalize a type name
 *
 * Special handling for type names: base runtime types (Bool, Int16, etc.)
 * preserve their case, while user-defined types (structs, enums) are
 * normalized according to the case sensitivity setting.
 *
 * @param str Type name to normalize
 * @return Normalized type name
 */
std::string CodeGenerator::normalizeType(const std::string& str) const
{
   static const std::unordered_set<std::string> baseTypes
      = {"Bool", "Int8", "Int16", "Int32", "Int64", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Double", "String", "WString", "Void"};

   if (baseTypes.find(str) != baseTypes.end()) {
      return str; // Preserve case for base types
   }

   return normalize(str); // Normalize user-defined types
}

/**
 * @brief Normalize an identifier (variable, member, function name)
 *
 * Applies standard normalization to identifiers based on the
 * case sensitivity setting. Most PLCs are case-insensitive,
 * so identifiers are typically converted to uppercase.
 *
 * @param str Identifier to normalize
 * @return Normalized identifier
 */
std::string CodeGenerator::normalizeIdent(const std::string& str) const
{
   return normalize(str);
}

// ============================================================================
//  Temporary Variable Management
// ============================================================================

/**
 * @brief Push a new scope for temporary variable counters
 *
 * Called when entering a new function or method scope that may contain
 * calls to functions/methods with OUTPUT parameters.
 *
 * When we generate code like:
 *   void MAIN::run() {
 *       DUMMY(..., __temp_C_1);
 *       DUMMY(..., __temp_C_2);
 *   }
 * Each call to DUMMY needs a unique __temp_C_N. The counter tracks
 * how many temporaries with base name "__temp_C" have been created.
 *
 * Without scope management, counters would accumulate across function
 * boundaries. With push/pop, each function starts with fresh counters.
 *
 * @note FUNCTION BLOCK operator() does NOT need this because:
 *       - It has no OUTPUT parameters (outputs are struct members)
 *       - It cannot be called with named output bindings like functions
 *       - No temporary variables are created for outputs in operator()
 */
void CodeGenerator::pushTempScope()
{
   // Save current counter state to stack
   m_tempCounterStack.push_back(m_tempCounter);
   // Start fresh counter map for new scope
   m_tempCounter.clear();
}

/**
 * @brief Pop the current scope and restore previous counter state
 *
 * Called when exiting a function or method scope. Restores the counter map
 * from the stack, allowing the parent scope to continue using its own
 * counters without interference from nested scopes.
 *
 * @details
 * Example:
 *   void outer() {           // pushTempScope() - outer counters start fresh
 *       DUMMY(..., __temp_C_1);
 *       inner() {            // pushTempScope() - inner starts fresh
 *           DUMMY(..., __temp_C_1);  // Inner gets its own __temp_C_1
 *       }                    // popTempScope() - inner counters discarded
 *       DUMMY(..., __temp_C_2);      // Outer continues with __temp_C_2
 *   }                        // popTempScope() - outer counters discarded
 *
 * @note FUNCTION BLOCK operator() does NOT need push/pop because it never
 *       creates temporary variables for output parameters.
 */
void CodeGenerator::popTempScope()
{
   if (!m_tempCounterStack.empty()) {
      // Restore previous counter state from stack
      m_tempCounter = m_tempCounterStack.back();
      m_tempCounterStack.pop_back();
   } else {
      // Stack is empty - just clear counters (shouldn't happen with proper push/pop)
      m_tempCounter.clear();
   }
}

/**
 * @brief Generate a unique temporary variable name
 *
 * Creates names like "__temp_RESULT2_1", "__temp_RESULT2_2", etc.
 * The counter is incremented each time a temporary with the same base name
 * is requested within the current scope.
 *
 * @param baseName The original variable name (e.g., "RESULT2")
 * @return Unique name like "__temp_RESULT2_1"
 *
 * @details
 * The function:
 * 1. Normalizes the base name (applies case sensitivity rules)
 * 2. Creates a key: "__temp_<normalized>"
 * 3. Looks up or creates a counter for this key
 * 4. Increments the counter
 * 5. Returns: key + "_" + counter
 *
 * Example sequence:
 *   getUniqueTempName("C")  -> "__temp_C_1"
 *   getUniqueTempName("C")  -> "__temp_C_2"
 *   getUniqueTempName("C")  -> "__temp_C_3"
 *   getUniqueTempName("RESULT") -> "__temp_RESULT_1"
 */
std::string CodeGenerator::getUniqueTempName(const std::string& baseName)
{
   // Normalize the base name (apply case sensitivity rules)
   std::string normalized = "__temp_" + normalizeIdent(baseName);

   // Get or create counter for this base name
   int& counter = m_tempCounter[normalized];

   // Increment and return unique name
   ++counter;
   return normalized + "_" + std::to_string(counter);
}

// ============================================================================
//  Statement generation
// ============================================================================

/**
 * @brief Extract the element type from an STArray template string
 * 
 * Parses a string like "STArray<MATHPROCESSOR, 1, 2>" and returns "MATHPROCESSOR".
 * Used to resolve the underlying FB type when calling methods on array elements.
 * 
 * @param arrayType The STArray type string (e.g., "STArray<MATHPROCESSOR, 1, 2>")
 * @return The extracted element type, or the original string if not an STArray
 */
static std::string extractArrayElementType(const std::string& arrayType)
{
   const std::string prefix = "STArray<";
   if (arrayType.rfind(prefix, 0) != 0) {
      return arrayType;
   }
   size_t start = prefix.length();
   size_t comma = arrayType.find(',', start);
   if (comma == std::string::npos) {
      return arrayType;
   }
   std::string elem = arrayType.substr(start, comma - start);
   // Trim spaces
   elem.erase(0, elem.find_first_not_of(" \t"));
   elem.erase(elem.find_last_not_of(" \t") + 1);
   return elem;
}

/**
 * @brief Generate C++ code for a single statement
 *
 * Handles all statement types including assignments, function/FB calls,
 * control flow statements, and empty statements. This is the main dispatcher
 * for statement generation.
 *
 * @param stmt The AST statement node to generate code for
 */
void CodeGenerator::genStmt(const Stmt& stmt)
{
   std::visit(
      [&](const auto& s) {
         using T = std::decay_t<decltype(s)>;

         if constexpr (std::is_same_v<T, AssignStmt>) {
            std::string lhsStr = genExpr(*s.lhs);
            std::string rhsStr = genExpr(*s.rhs);

            // Check if LHS is a getter for an AT variable (get_NOMEVAR())
            std::string varName;
            if (lhsStr.rfind("getPi_", 0) == 0 && lhsStr.length() > 6 && lhsStr.back() == ')') {
               // It is a getter: getPi_NOMEVAR()
               varName = lhsStr.substr(6, lhsStr.length() - 8); // Remove getPi_ and ()
               // Check if it is an AT variable
               if (isATVariable(varName)) {
                  // Use setter instead of assignment
                  m_src << ind() << "setPi_" << varName << "(" << rhsStr << ");\n";
                  return;
               }
            }

            // Check if LHS is an address (write access required)
            if (auto* addr = std::get_if<AddressExpr>(&s.lhs->node)) {
               std::string writeAccess = generateAddressWrite(*addr, rhsStr);
               m_src << ind() << writeAccess << ";\n";
               return;
            }

            // Normal assignment
            if (lhsStr == m_currentFunctionName) {
               lhsStr = m_currentFunctionName + "_ret";
            }
            m_src << ind() << lhsStr << " = " << rhsStr << ";\n";
         } else if constexpr (std::is_same_v<T, ExprStmt>) {
            // Expression statement (often a function call)
            if (auto* call = std::get_if<CallExpr>(&s.expr->node)) {
               std::string calleeName = genExpr(*call->callee);

               // Determine the type of the callee (for FB instance resolution)
               std::string calleeType;
               auto varIt = m_varTypes.find(calleeName);
               if (varIt != m_varTypes.end()) {
                  calleeType = varIt->second;
               } else {
                  // Check if it's an array access (e.g., PROCESSORS[1])
                  std::string baseName = getBaseFBName(calleeName);
                  auto baseIt = m_varTypes.find(baseName);
                  if (baseIt != m_varTypes.end()) {
                     calleeType = baseIt->second;
                  } else {
                     calleeType = calleeName;
                  }
               }

               // If calleeType is an STArray, extract the element type (the FB type)
               std::string elementType = extractArrayElementType(calleeType);

               // Look up function signature using the element type
               std::string calleeKey = normalizeIdent(elementType);
               auto sigIt = m_signatures.find(calleeKey);
               if (sigIt == m_signatures.end() && !elementType.empty()) {
                  std::string upperName = elementType;
                  upperName[0] = std::toupper(upperName[0]);
                  sigIt = m_signatures.find(upperName);
               }

               bool isFunctionBlock = (sigIt != m_signatures.end() && sigIt->second.returnType.base == BaseType::VOID);

               // Check if this is a method call (contains dot) vs direct FB call
               bool isMethodCall = (std::get_if<MemberExpr>(&call->callee->node) != nullptr);

               // CASE 1: Direct Function Block call (e.g., myFB(10, false))
               if (isFunctionBlock && !call->args.empty() && !isMethodCall) {
                  // Get the base FB name (without array indices)
                  std::string baseFBName = getBaseFBName(calleeName);

                  // Handle positional arguments (inputs) -> setters
                  if (!call->args.empty() && !call->args[0].named) {
                     size_t idx = 0;
                     for (const auto& param : sigIt->second.parameters) {
                        if (param.isInput && idx < call->args.size()) {
                           if (call->args[idx].name != "") {
                              std::ostringstream oss;
                              oss << "Error at line " << call->args[idx].line << ":" << call->args[idx].col
                                  << ": Mixed reference and positional parameters in call to function block '" << calleeName << "'";
                              throw std::runtime_error(oss.str());
                           }
                           std::string value = genExpr(*call->args[idx].value);
                           // Use calleeName (includes array index) for the actual call
                           m_src << ind() << calleeName << ".set_" << normalizeIdent(param.name) << "(" << value << ");\n";
                           idx++;
                        }
                     }
                  }
                  // Handle named arguments
                  else {
                     for (const auto& arg : call->args) {
                        std::string value = genExpr(*arg.value);

                        if (!arg.isOutput) {
                           if (arg.name == "") {
                              std::ostringstream oss;
                              oss << "Error at line " << arg.line << ":" << arg.col
                                  << ": Mixed reference and positional parameters in call to function block '" << calleeName << "'";
                              throw std::runtime_error(oss.str());
                           }
                           m_src << ind() << calleeName << ".set_" << normalizeIdent(arg.name) << "(" << value << ");\n";
                        }
                     }
                  }

                  // Execute the FB
                  m_src << ind() << calleeName << "();\n";

                  // Handle output bindings -> getters
                  if (!call->args.empty() && !call->args[0].named) {
                     size_t idx = 0;
                     for (const auto& param : sigIt->second.parameters) {
                        if (!param.isInput && idx < call->args.size()) {
                           std::ostringstream oss;
                           oss << "Error at line " << call->args[idx].line << ":" << call->args[idx].col
                               << ": Called VAR_OUTPUT or VAR_IN_OUT parameter without naming it in call to function block '"
                               << calleeName << "'";
                           throw std::runtime_error(oss.str());
                        } else if (param.isInput && idx < call->args.size()) {
                           idx++;
                        }
                     }
                  } else {
                     for (const auto& arg : call->args) {
                        std::string value = genExpr(*arg.value);
                        if (arg.isOutput) {
                           m_src << ind() << value << " = " << calleeName << ".get_" << normalizeIdent(arg.name) << "();\n";
                        }
                     }
                  }
                  return;
               }

               // CASE 2: Function or Method call
               FunctionSignature* activeSig = nullptr;
               std::string activeCalleeName = calleeName;

               // Try global function signature
               if (sigIt != m_signatures.end()) {
                  activeSig = &sigIt->second;
               } else {
                  size_t dotPos = calleeName.find('.');
                  if (dotPos != std::string::npos) {
                     std::string instanceName = calleeName.substr(0, dotPos);
                     std::string methodName = calleeName.substr(dotPos + 1);
                     auto varIt2 = m_varTypes.find(instanceName);
                     if (varIt2 != m_varTypes.end()) {
                        std::string fbType = varIt2->second;
                        std::string methodKey = fbType + "::" + methodName;
                        auto methodIt = m_methodSignatures.find(methodKey);
                        if (methodIt != m_methodSignatures.end()) {
                           activeSig = &methodIt->second;
                           activeCalleeName = calleeName;
                        }
                     }
                  }
               }

               bool hasArguments = !call->args.empty();

               // Enter if we have a signature OR there are arguments to process
               // For method calls (isMethodCall == true), we enter even if isFunctionBlock is true
               if ((activeSig != nullptr || hasArguments) && (!isFunctionBlock || isMethodCall)) {
                  if (activeSig != nullptr) {
                     std::unordered_map<std::string, std::string> providedArgs;
                     std::vector<std::string> tempVars;

                     // Determine call style: positional vs named (only if there are args)
                     bool isPositional = hasArguments && !call->args[0].named;

                     if (isPositional) {
                        size_t idx = 0;
                        for (const auto& param : activeSig->parameters) {
                           if (idx < call->args.size()) {
                              std::string value = genExpr(*call->args[idx].value);
                              std::string paramKey = normalizeIdent(param.name);
                              providedArgs[paramKey] = value;
                           }
                           idx++;
                        }
                     } else if (hasArguments) {
                        for (const auto& arg : call->args) {
                           std::string value = genExpr(*arg.value);
                           std::string normalizedName = normalizeIdent(arg.name);
                           providedArgs[normalizedName] = value;
                        }
                     }
                     // else: no arguments - providedArgs empty

                     // Build argument list in signature order
                     std::string callArgs;
                     bool first = true;
                     for (const auto& param : activeSig->parameters) {
                        if (!first) {
                           callArgs += ", ";
                        }
                        first = false;

                        std::string paramKey = normalizeIdent(param.name);
                        auto it = providedArgs.find(paramKey);

                        if (it != providedArgs.end()) {
                           callArgs += it->second;
                        } else if (param.isInput && param.defaultValue) {
                           // Input parameter with default value
                           callArgs += genExpr(*param.defaultValue);
                        } else if (param.isInput && !param.defaultValue) {
                           // Input parameter WITHOUT default value - use {} (value-initialization)
                           callArgs += "{}";
                        } else if (param.isOutputVar) {
                           // OUTPUT parameter NOT provided - create a temporary variable with unique name
                           std::string tempVar = getUniqueTempName(param.name);
                           tempVars.push_back(tempVar);
                           std::string type;
                           if (!param.type.arrayDims.empty()) {
                              type = getArrayType(normalizeType(getBaseTypeName(param.type)), param.type);
                           } else {
                              type = normalizeType(getBaseTypeName(param.type));
                           }
                           std::string initValue;
                           if (param.defaultValue) {
                              initValue = " = " + genExpr(*param.defaultValue);
                           } else {
                              initValue = "{}";
                           }
                           m_src << ind() << type << " " << tempVar << initValue << ";\n";
                           callArgs += tempVar;
                        } else if (!param.isInput && !param.isOutputVar) {
                           // IN_OUT parameter - REQUIRED but not provided!
                           uint32_t line = call->args.empty() ? 0 : call->args[0].line;
                           uint32_t col = call->args.empty() ? 0 : call->args[0].col;
                           std::ostringstream oss;
                           oss << "Error at line " << line << ":" << col << ": Missing required IN_OUT parameter '" << param.name
                               << "' in call to '" << activeCalleeName << "'";
                           throw std::runtime_error(oss.str());
                        }
                     }

                     m_src << ind() << activeCalleeName << "(" << callArgs << ");\n";
                     return;
                  } else if (hasArguments) {
                     // No signature but there are arguments - fallback to positional call
                     std::string callArgs;
                     bool first = true;
                     for (const auto& arg : call->args) {
                        if (!first) {
                           callArgs += ", ";
                        }
                        first = false;
                        callArgs += genExpr(*arg.value);
                     }
                     m_src << ind() << activeCalleeName << "(" << callArgs << ");\n";
                     return;
                  }
               }
            }

            // Fallback: normal expression statement
            m_src << ind() << genExpr(*s.expr) << ";\n";
         } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            // If the current function/method has a return type (non-void), return the _ret variable
            if (!m_currentFunctionReturnType.empty() && m_currentFunctionReturnType != "void") {
               m_src << ind() << "return " << m_currentFunctionName << "_ret;\n";
            } else {
               m_src << ind() << "return;\n";
            }
         } else if constexpr (std::is_same_v<T, ExitStmt>) {
            m_src << ind() << "break;\n";
         } else if constexpr (std::is_same_v<T, EmptyStmt>) {
            // Nothing to generate
         } else if constexpr (std::is_same_v<T, IfStmt>) {
            genIf(s);
         } else if constexpr (std::is_same_v<T, ForStmt>) {
            genFor(s);
         } else if constexpr (std::is_same_v<T, WhileStmt>) {
            genWhile(s);
         } else if constexpr (std::is_same_v<T, RepeatStmt>) {
            genRepeat(s);
         } else if constexpr (std::is_same_v<T, CaseStmt>) {
            genCase(s);
         }
      },
      stmt.node);
}

/**
 * @brief Generate C++ if-else statement
 *
 * Converts an IF statement with optional ELSIF and ELSE branches
 * into a C++ if-else if-else chain.
 *
 * @param s IfStmt AST node to generate
 */
void CodeGenerator::genIf(const IfStmt& s)
{
   bool first = true;
   for (const auto& branch : s.branches) {
      if (branch.condition) {
         if (first) {
            m_src << ind() << "if (";
         } else {
            m_src << ind() << "} else if (";
         }
         m_src << genExpr(*branch.condition) << ") {\n";
         first = false;
      } else {
         m_src << ind() << "} else {\n";
      }
      push();
      for (const auto& st : branch.body) {
         genStmt(*st);
      }
      pop();
   }
   m_src << ind() << "}\n";
}

/**
 * @brief Generate C++ for loop
 *
 * Converts a FOR loop with from, to, and optional by step
 * into a C++ for loop. Currently assumes positive step.
 *
 * @param s ForStmt AST node to generate
 */
void CodeGenerator::genFor(const ForStmt& s)
{
   std::string byExpr = s.by ? genExpr(*s.by) : "1";
   std::string normalizedVar = normalizeIdent(s.var);
   m_src << ind() << "for (auto " << normalizedVar << " = " << genExpr(*s.from) << "; " << normalizedVar << " <= " << genExpr(*s.to)
         << "; " << normalizedVar << " += " << byExpr << ") {\n";
   push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   pop();
   m_src << ind() << "}\n";
}

/**
 * @brief Generate C++ while loop
 *
 * Converts a WHILE loop into a standard C++ while loop.
 *
 * @param s WhileStmt AST node to generate
 */
void CodeGenerator::genWhile(const WhileStmt& s)
{
   m_src << ind() << "while (" << genExpr(*s.condition) << ") {\n";
   push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   pop();
   m_src << ind() << "}\n";
}

/**
 * @brief Generate C++ do-while loop
 *
 * Converts a REPEAT-UNTIL loop into a C++ do-while loop.
 * Note that REPEAT executes until condition is true,
 * so the condition is negated in the do-while.
 *
 * @param s RepeatStmt AST node to generate
 */
void CodeGenerator::genRepeat(const RepeatStmt& s)
{
   m_src << ind() << "do {\n";
   push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   pop();
   m_src << ind() << "} while (!(" << genExpr(*s.condition) << "));\n";
}

/**
 * @brief Generate C++ code for CASE statement
 * 
 * Converts a CASE statement into if-else if chain to support ranges.
 * C++ switch doesn't support ranges, so we use if-else instead.
 * 
 * Example ST:
 *   CASE value OF
 *       1, 3, 5:     result := 1;
 *       10..20:      result := 2;
 *       30..40, 50:  result := 3;
 *       ELSE         result := 0;
 *   END_CASE
 * 
 * Generated C++:
 *   if (value == 1 || value == 3 || value == 5) {
 *       result = 1;
 *   } else if (value >= 10 && value <= 20) {
 *       result = 2;
 *   } else if ((value >= 30 && value <= 40) || value == 50) {
 *       result = 3;
 *   } else {
 *       result = 0;
 *   }
 *
 * @param s CaseStmt AST node to generate
 */
void CodeGenerator::genCase(const CaseStmt& s)
{
   bool firstBranch = true;

   for (const auto& branch : s.branches) {
      if (branch.values.empty()) {
         // ELSE branch
         if (firstBranch) {
            // No conditions before ELSE - generate dummy if(false)
            m_src << ind() << "if (false) {\n";
         } else {
            m_src << ind() << "} else {\n";
         }
         firstBranch = false;
      } else {
         // Build condition expression for this branch
         std::string condition;
         bool firstValue = true;

         for (const auto& cv : branch.values) {
            if (!firstValue) {
               condition += " || ";
            }
            firstValue = false;

            if (cv.high) {
               // Range case: low..high
               condition += "(" + genExpr(*s.selector) + " >= " + genExpr(*cv.low) + " && " + genExpr(*s.selector)
                            + " <= " + genExpr(*cv.high) + ")";
            } else {
               // Single value case
               condition += genExpr(*s.selector) + " == " + genExpr(*cv.low);
            }
         }

         if (firstBranch) {
            m_src << ind() << "if (" << condition << ") {\n";
            firstBranch = false;
         } else {
            m_src << ind() << "} else if (" << condition << ") {\n";
         }
      }

      // Generate branch body
      push();
      for (const auto& stmt : branch.body) {
         genStmt(*stmt);
      }
      pop();
   }

   // Close the last if/else chain
   if (!firstBranch) {
      m_src << ind() << "}\n";
   }
}

// ============================================================================
//  Expression generation
// ============================================================================

/**
 * @brief Helper function for genExpr
 * 
 * @param op Is the operation to evaluate
 */
int getBinaryPrecFromOp(const std::string& op)
{
   if (op == "||" || op == "OR") {
      return 1;
   }
   if (op == "&&" || op == "AND") {
      return 2;
   }
   if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
      return 3;
   }
   if (op == "+" || op == "-") {
      return 4;
   }
   if (op == "*" || op == "/" || op == "%" || op == "MOD") {
      return 5;
   }
   if (op == "**") {
      return 6;
   }
   return 0;
}

/**
 * @brief Generate C++ expression text from an AST expression
 *
 * This is the main dispatcher for expression generation.
 * Handles all expression types including literals, identifiers,
 * unary/binary operations, member access, array indexing,
 * function calls, casts, and array initializers.
 *
 * @param expr The AST expression node to generate
 * @return C++ code string for the expression
 */
std::string CodeGenerator::genExpr(const Expr& expr)
{
   return std::visit(
      [&](const auto& e) -> std::string {
         using T = std::decay_t<decltype(e)>;

         if constexpr (std::is_same_v<T, LiteralExpr>) {
            // Time literals require special handling
            if (e.suffix == "TIME") {
               return "IEC_TIME_LITERAL(\"" + e.value + "\")";
            }
            std::string value = e.value;

            // Convert 16#... to 0x...
            if (value.rfind("16#", 0) == 0) {
               std::string hex = value.substr(3);
               // Remove underscores
               hex.erase(std::remove(hex.begin(), hex.end(), '_'), hex.end());
               return "0x" + hex;
            }
            // Convert 2#... to 0b... (C++14 binary literals)
            else if (value.rfind("2#", 0) == 0) {
               std::string bin = value.substr(2);
               bin.erase(std::remove(bin.begin(), bin.end(), '_'), bin.end());
               return "0b" + bin;
            }
            // Convert 8#... to 0... (octal)
            else if (value.rfind("8#", 0) == 0) {
               std::string oct = value.substr(2);
               oct.erase(std::remove(oct.begin(), oct.end(), '_'), oct.end());
               return "0" + oct;
            }

            return value;
         } else if constexpr (std::is_same_v<T, BoolLitExpr>) {
            return e.value ? "true" : "false";
         } else if constexpr (std::is_same_v<T, IdentExpr>) {
            std::string varName = normalizeIdent(e.name);

            // Check if this identifier is an enum enumerator (O(1) lookup)
            auto enumIt = m_enumeratorToEnum.find(varName);
            if (enumIt != m_enumeratorToEnum.end()) {
               // This is an enum enumerator - return qualified name
               return enumIt->second + "::" + varName;
            }

            // First, check if it's a local variable (local variables have priority over globals)
            if (m_localVarTypes.find(varName) != m_localVarTypes.end()) {
               // It's a local variable - check if it has AT address
               if (m_localAtVariables.find(varName) != m_localAtVariables.end()) {
                  // Local variable with AT address - use getter
                  return "getPi_" + varName + "()";
               }
               // Local variable without AT address - use directly
               return varName;
            }

            // Not a local variable - check if it's an AT global variable
            if (m_globalAtVariables.find(varName) != m_globalAtVariables.end()) {
               // Global AT variable - use getter
               return "getPi_" + varName + "()";
            }

            // Fallback: use name directly
            return varName;
         }

         else if constexpr (std::is_same_v<T, UnaryExpr>) {
            if (e.op == "NOT") {
               return "!" + genExpr(*e.operand);
            }
            return e.op + genExpr(*e.operand);
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            std::string op = e.op;
            // Map ST operators to C++
            if (op == "=" || op == "==") {
               op = "==";
            } else if (op == "AND" || op == "&&") {
               op = "&&";
            } else if (op == "OR" || op == "||") {
               op = "||";
            } else if (op == "XOR") {
               op = "^";
            } else if (op == "MOD") {
               op = "%";
            } else if (op == "<>") {
               op = "!=";
            } else if (op == "**") {
               return "std::pow(" + genExpr(*e.left) + ", " + genExpr(*e.right) + ")";
            }

            // Check if parentheses are actually needed
            bool needParens = false;

            // Get precedence of current operator
            int currentPrec = getBinaryPrecFromOp(op);

            // Check left operand - if it has lower precedence, we need parentheses
            if (auto* leftBinary = std::get_if<BinaryExpr>(&e.left->node)) {
               int leftPrec = getBinaryPrecFromOp(leftBinary->op);
               if (leftPrec < currentPrec) {
                  needParens = true;
               }
            }

            // Check right operand - similar logic
            if (auto* rightBinary = std::get_if<BinaryExpr>(&e.right->node)) {
               int rightPrec = getBinaryPrecFromOp(rightBinary->op);
               if (rightPrec < currentPrec || (rightPrec == currentPrec && (op == "-" || op == "/" || op == "%"))) {
                  needParens = true;
               }
            }

            if (needParens) {
               return "(" + genExpr(*e.left) + " " + op + " " + genExpr(*e.right) + ")";
            }
            return genExpr(*e.left) + " " + op + " " + genExpr(*e.right);
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            std::string object = genExpr(*e.object);
            std::string member = normalizeIdent(e.member);

            // Handle enum member access (needs :: instead of .)
            auto varIt = m_varTypes.find(object);
            if (varIt != m_varTypes.end() && m_enumTypes.find(varIt->second) != m_enumTypes.end()) {
               return varIt->second + "::" + member;
            } else if (m_enumTypes.find(object) != m_enumTypes.end()) {
               return object + "::" + member;
            } else {
               return object + "." + member;
            }
         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            std::string r = genExpr(*e.array);
            for (const auto& idx : e.indices) {
               r += "[" + genExpr(*idx) + "]";
            }
            return r;
         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            return "(*" + genExpr(*e.pointer) + ")";
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            std::string calleeName = genExpr(*e.callee);
            std::string r = calleeName + "(";
            bool first = true;

            // Check if we know the function signature (for named arguments)
            auto sigIt = m_signatures.find(calleeName);

            if (sigIt != m_signatures.end() && !e.args.empty() && e.args[0].named) {
               // Named arguments - reorder according to signature
               const auto& sig = sigIt->second;

               std::unordered_map<std::string, std::string> argMap;
               for (const auto& arg : e.args) {
                  if (arg.named) {
                     argMap[arg.name] = genExpr(*arg.value);
                  }
               }

               for (const auto& param : sig.parameters) {
                  if (!first) {
                     r += ", ";
                  }
                  first = false;

                  auto it = argMap.find(param.name);
                  if (it != argMap.end()) {
                     r += it->second;
                  } else {
                     r += "/* missing: " + param.name + " */";
                  }
               }
            } else {
               // Positional arguments - preserve order
               for (const auto& arg : e.args) {
                  if (!first) {
                     r += ", ";
                  }
                  first = false;
                  r += genExpr(*arg.value);
               }
            }

            r += ")";
            return r;
         } else if constexpr (std::is_same_v<T, AdrExpr>) {
            return "&(" + genExpr(*e.operand) + ")";
         } else if constexpr (std::is_same_v<T, SizeofExpr>) {
            return "sizeof(" + mapType(e.type) + ")";
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            return "static_cast<" + mapType(e.targetType) + ">(" + genExpr(*e.operand) + ")";
         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            std::string result = "{";
            bool first = true;
            for (const auto& elem : e.elements) {
               if (!first) {
                  result += ", ";
               }
               first = false;

               // Check if the element is itself an ArrayInitExpr (nested)
               if (auto* nestedArray = std::get_if<ArrayInitExpr>(&elem->node)) {
                  // Recursively generate nested initializer
                  result += genExpr(*elem);
               } else {
                  result += genExpr(*elem);
               }
            }
            result += "}";
            return result;
         } else if constexpr (std::is_same_v<T, SuperCallExpr>) {
            std::string r = m_currentBaseClass + "::" + normalizeIdent(e.methodName) + "(";
            bool first = true;
            for (const auto& arg : e.args) {
               if (!first) {
                  r += ", ";
               }
               first = false;
               r += genExpr(*arg.value);
            }
            r += ")";
            return r;
         } else if constexpr (std::is_same_v<T, AddressExpr>) {
            // Default to read access
            return generateAddressAccess(e);
         }

         return "";
      },
      expr.node);
}

// ============================================================================
//  Address and process image
// ============================================================================

/**
 * @brief Generate C++ code for accessing process image addresses
 */
std::string CodeGenerator::generateAddressAccess(const AddressExpr& addr)
{
   std::string imageVar = m_piConfig.instanceName;
   std::string accessor;

   switch (addr.qualifier) {
   case AddressExpr::AddressQualifier::BIT:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputBit"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputBit"
                                                                   : "readMarkerBit";
      if (addr.bitOffset >= 0) {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + std::to_string(addr.bitOffset) + ")";
      } else {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", 0)";
      }

   case AddressExpr::AddressQualifier::BYTE:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputByte"
                                                                   : "readMarkerByte";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ")";

   case AddressExpr::AddressQualifier::WORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputWord"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputWord"
                                                                   : "readMarkerWord";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ")";

   case AddressExpr::AddressQualifier::DWORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputDword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputDword"
                                                                   : "readMarkerDword";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ")";

   case AddressExpr::AddressQualifier::LWORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputLword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputLword"
                                                                   : "readMarkerLword";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ")";

   case AddressExpr::AddressQualifier::POINTER:
      return "&" + imageVar + ".busInputPtr()[" + std::to_string(addr.byteOffset) + "]";
   }

   return "";
}

/**
 * @brief Generate write access for addresses (when on LHS of assignment)
 */
std::string CodeGenerator::generateAddressWrite(const AddressExpr& addr, const std::string& value)
{
   std::string imageVar = m_piConfig.instanceName;
   std::string accessor;

   switch (addr.qualifier) {
   case AddressExpr::AddressQualifier::BIT:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputBit"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputBit"
                                                                   : "writeMarkerBit";
      if (addr.bitOffset >= 0) {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + std::to_string(addr.bitOffset) + ", " + value
                + ")";
      } else {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", 0, " + value + ")";
      }

   case AddressExpr::AddressQualifier::BYTE:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputByte"
                                                                   : "writeMarkerByte";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + value + ")";

   case AddressExpr::AddressQualifier::WORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputWord"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputWord"
                                                                   : "writeMarkerWord";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + value + ")";

   case AddressExpr::AddressQualifier::DWORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputDword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputDword"
                                                                   : "writeMarkerDword";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + value + ")";

   case AddressExpr::AddressQualifier::LWORD:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputLword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputLword"
                                                                   : "writeMarkerLword";
      return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + value + ")";

   default:
      throw std::runtime_error("Invalid address qualifier for write access");
   }
}

/**
 * @brief Check if a variable name corresponds to an AT address variable
 * @param varName The variable name to check
 * @return true if the variable has an AT address
 */
bool CodeGenerator::isATVariable(const std::string& varName) const
{
   // Check both local and global AT variables
   return m_localAtVariables.find(varName) != m_localAtVariables.end() || m_globalAtVariables.find(varName) != m_globalAtVariables.end();
}

// ============================================================================
//  Project-style generation
// ============================================================================

/**
 * @brief Build dependency graph between function blocks
 * @param tu Translation unit
 * @return Map of FB name -> set of FB names it depends on
 */
std::unordered_map<std::string, std::unordered_set<std::string>> CodeGenerator::buildFBDependencies(const TranslationUnit& tu)
{
   std::unordered_map<std::string, std::unordered_set<std::string>> deps;

   for (const auto& pou : tu.pous) {
      if (pou.kind != POUKind::FUNCTION_BLOCK) {
         continue;
      }

      std::string fbName = normalizeType(pou.name);
      deps[fbName]; // Ensure entry exists

      // Check all variable sections for FB-typed members
      for (const auto& sec : pou.varSections) {
         for (const auto& decl : sec.decls) {
            if (decl.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(decl.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  deps[fbName].insert(typeName);
               }
            }
         }
      }

      // Also check method parameters
      for (const auto& method : pou.methods) {
         for (const auto& param : method.parameters) {
            if (param.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(param.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  deps[fbName].insert(typeName);
               }
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Topological sort of function blocks (Kahn's algorithm)
 * @param dependencies Dependency map
 * @return Ordered list of FB names (no cycles)
 * @throws std::runtime_error if circular dependency detected
 */
std::vector<std::string> CodeGenerator::topologicalSort(
   const std::unordered_map<std::string, std::unordered_set<std::string>>& dependencies)
{
   std::unordered_map<std::string, int> inDegree;
   std::unordered_map<std::string, std::unordered_set<std::string>> adjList;

   // Initialize
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = 0;
      adjList[node] = deps;
   }

   // Calculate in-degree (number of nodes that depend on this node)
   // Wait - we need reverse: which nodes depend on this node?
   // Actually, for Kahn's algorithm, we need edges: u -> v means u depends on v
   // So we build adjacency from v (dependency) to u (dependent)

   std::unordered_map<std::string, std::unordered_set<std::string>> reverseAdj;
   for (const auto& [node, deps] : dependencies) {
      for (const auto& dep : deps) {
         reverseAdj[dep].insert(node);
      }
   }

   // Calculate in-degree (number of dependencies this node has)
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = deps.size();
   }

   // Queue for nodes with zero in-degree
   std::queue<std::string> q;
   for (const auto& [node, degree] : inDegree) {
      if (degree == 0) {
         q.push(node);
      }
   }

   std::vector<std::string> result;
   while (!q.empty()) {
      std::string node = q.front();
      q.pop();
      result.push_back(node);

      // Decrease in-degree of nodes that depend on this node
      for (const auto& dependent : reverseAdj[node]) {
         inDegree[dependent]--;
         if (inDegree[dependent] == 0) {
            q.push(dependent);
         }
      }
   }

   // Check for cycles
   if (result.size() != dependencies.size()) {
      throw std::runtime_error("Circular dependency detected among function blocks!");
   }

   return result;
}

/**
 * @brief Generate code in modular project style
 * @param tu Translation unit
 * @param outputDir Output directory path
 * @return Vector of generated files
 */
std::vector<GeneratedFile> CodeGenerator::generateModular(const TranslationUnit& tu, const std::string& outputDir)
{
   std::vector<GeneratedFile> files;
   m_outputDir = outputDir;

   // Step 1: Collect all signatures FIRST (before generating anything)
   for (const auto& pou : tu.pous) {
      collectSignature(pou);
   }

   // Step 2: Register enum types and their enumerators
   for (const auto& et : tu.enums) {
      std::string upperName = normalizeType(et.name);
      m_enumTypes[upperName] = true;
      for (const auto& enumerator : et.enumerators) {
         std::string upperEnumerator = normalizeIdent(enumerator.name);
         m_enumValues[upperName].insert(upperEnumerator);
         m_enumeratorToEnum[upperEnumerator] = upperName;
      }
   }

   // Step 3: Collect all FBs and mark them
   m_fbMap = collectFunctionBlocks(tu);
   for (const auto& [name, pou] : m_fbMap) {
      m_isFB[normalizeType(name)] = true;
   }

   // Step 4: Register variable types for all POUs (for enum resolution)
   for (const auto& pou : tu.pous) {
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            m_varTypes[normalizeIdent(d.name)] = mapType(d.type);
         }
      }
   }

   // Step 4.5: Register global variable types (so they can be resolved later)
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);
         m_varTypes[varName] = varType;
         m_globalVarTypes[varName] = varType;
         if (!d.atAddress.empty()) {
            m_globalAtVariables[varName] = d.atAddress;
         }
      }
   }

   // Step 5: Build dependency graph
   auto dependencies = buildFBDependencies(tu);

   // Step 6: Topological sort of FBs
   std::vector<std::string> orderedFBs;
   if (!dependencies.empty()) {
      orderedFBs = topologicalSort(dependencies);
   }

   // Step 7: Collect program names
   std::vector<std::string> programNames;
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::PROGRAM) {
         programNames.push_back(normalizeType(pou.name));
      }
   }

   // Step 7.5: Check if any AT addresses are used
   m_hasAddresses = false;
   for (const auto& pou : tu.pous) {
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               m_hasAddresses = true;
               break;
            }
         }
         if (m_hasAddresses) {
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_hasAddresses = true;
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }

   // ========== GENERATION ORDER ==========

   // 0. ProcessImage.hpp - Global Process Image (only an header, no source)
   if (m_hasAddresses) {
      std::ostringstream piHeader;
      piHeader << generateHeaderComment();
      piHeader << "#pragma once\n";
      piHeader << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
      piHeader << "#include \"" << m_runtimeHeader << "\"\n";

      if (!m_namespace.empty()) {
         piHeader << "namespace " << m_namespace << " {\n\n";
      }

      piHeader << "// Global Process Image instance\n";
      piHeader << "inline ProcessImage<" << m_piConfig.inputBytes << ", " << m_piConfig.outputBytes << ", " << m_piConfig.markerBytes
               << "> " << m_piConfig.instanceName << ";\n\n";

      if (!m_namespace.empty()) {
         piHeader << "} // namespace " << m_namespace << "\n";
      }

      files.push_back({"ProcessImage", piHeader.str(), GenFileType::HEADER, ""});
   }

   // 1. SimpleGVLs.hpp (ENUM + simple STRUCT + simple globals)
   files.push_back({"SimpleGVLs", generateSimpleGVLsHeader(tu), GenFileType::HEADER, ""});

   // 2. Generate each FB in its own file
   for (const auto& fbName : orderedFBs) {
      const auto& pou = m_fbMap[fbName];

      std::unordered_set<std::string> fbDeps;
      auto it = dependencies.find(fbName);
      if (it != dependencies.end()) {
         fbDeps = it->second;
      }

      if (!pou.extends.empty()) {
         std::string baseName = normalizeType(pou.extends);
         // Verifica che la base sia un FB (non un'interfaccia)
         if (m_isFB.find(baseName) != m_isFB.end()) {
            fbDeps.insert(baseName);
         }
      }

      for (const auto& iface : pou.implements) {
         std::string ifaceName = normalizeType(iface);
      }

      files.push_back({fbName, generateFBHeader(pou, fbDeps), GenFileType::HEADER, "FunctionBlocks"});
      files.push_back({fbName, generateFBSource(pou), GenFileType::SOURCE, "FunctionBlocks"});
   }

   // 3. FunctionBlocks.hpp master (includes SimpleGVLs.hpp + all FB headers)
   files.push_back({"FunctionBlocks", generateFunctionBlocksMaster(orderedFBs), GenFileType::MASTER, ""});

   // 4. GVLs.hpp (structs with FB + extern declarations)
   files.push_back({"GVLs", generateGVLsHeader(tu), GenFileType::HEADER, ""});

   // 5. GVLs.cpp (definitions)
   files.push_back({"GVLs", generateGVLsSource(tu), GenFileType::SOURCE, ""});

   // 6. Functions.hpp and Functions.cpp
   files.push_back({"Functions", generateFunctionsHeader(tu), GenFileType::HEADER, ""});
   files.push_back({"Functions", generateFunctionsSource(tu), GenFileType::SOURCE, ""});

   // 7. Generate each Program in its own file (in Programs subdirectory)
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::PROGRAM) {
         std::string progName = normalizeType(pou.name);
         files.push_back({progName, generateProgramHeader(pou), GenFileType::HEADER, "Programs"});
         files.push_back({progName, generateProgramSource(pou), GenFileType::SOURCE, "Programs"});
      }
   }

   // 8. Programs.hpp master
   if (!programNames.empty()) {
      files.push_back({"Programs", generateProgramsMaster(programNames), GenFileType::MASTER, ""});
   }

   return files;
}

/**
 * @brief Check if a struct type contains any FB (directly or indirectly)
 * @param structName Name of the struct to check
 * @param tu Translation unit for lookup
 * @return true if the struct contains any FB (directly or nested)
 */
bool CodeGenerator::structContainsFB(const std::string& structName, const TranslationUnit& tu) const
{
   // Find the struct definition
   const StructType* targetStruct = nullptr;
   for (const auto& st : tu.structs) {
      if (normalizeType(st.name) == structName) {
         targetStruct = &st;
         break;
      }
   }

   if (!targetStruct) {
      return false;
   }

   // Check each member
   for (const auto& member : targetStruct->members) {
      if (member.type.base == BaseType::NAMED) {
         std::string memberType = normalizeType(member.type.name);

         // Direct FB
         if (m_isFB.find(memberType) != m_isFB.end()) {
            return true;
         }

         // Recursive check: is this member a struct that contains FB?
         if (structContainsFB(memberType, tu)) {
            return true;
         }
      }
   }

   return false;
}

/**
 * @brief Generate SimpleGVLs header file (ENUM + simple STRUCT + simple globals)
 * 
 * This file contains:
 * - All ENUM types (always independent)
 * - STRUCT types that do NOT contain Function Block members
 * - Global variables of simple types (non-FB)
 * 
 * SimpleGVLs.hpp is the foundation that other headers can include without
 * creating circular dependencies. It has no dependencies on FunctionBlocks.
 *
 * @param tu Translation unit
 * @return File content as string
 */
std::string CodeGenerator::generateSimpleGVLsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n\n";

   if (m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Generate ENUMs (always, they have no dependencies)
   for (const auto& et : tu.enums) {
      std::string upperName = normalizeType(et.name);
      // Register enum type for special handling
      m_enumTypes[upperName] = true;
      for (const auto& enumerator : et.enumerators) {
         std::string upperEnumerator = normalizeIdent(enumerator.name);
         m_enumValues[upperName].insert(upperEnumerator);
         m_enumeratorToEnum[upperEnumerator] = upperName;
      }

      out << "// ENUM " << upperName << "\n";
      out << "enum class " << upperName << " : int {\n";
      for (size_t i = 0; i < et.enumerators.size(); ++i) {
         const auto& enumerator = et.enumerators[i];
         std::string upperEnumerator = normalizeIdent(enumerator.name);
         out << "    " << upperEnumerator;
         if (enumerator.value) {
            out << " = " << genExpr(*enumerator.value);
         }
         if (i < et.enumerators.size() - 1) {
            out << ",";
         }
         out << "\n";
      }
      out << "};\n";
      out << "// END ENUM " << upperName << "\n\n";
   }

   // Generate INTERFACEs (they have no dependencies)
   const std::vector<Interface>& interfaces = tu.interfaces.empty() ? Parser::getParsedInterfaces() : tu.interfaces;
   for (const auto& iface : interfaces) {
      std::string name = normalizeType(iface.name);
      out << "// INTERFACE " << name << "\n";
      out << "struct " << name << " {\n";
      for (const auto& method : iface.methods) {
         std::string retType = "void";
         if (!isVoidType(method.returnType)) {
            retType = mapType(method.returnType);
         }
         out << "    virtual " << retType << " " << normalizeIdent(method.name) << "(";
         bool first = true;
         for (const auto& param : method.parameters) {
            if (!first) {
               out << ", ";
            }
            first = false;
            std::string type = mapType(param.type);
            if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
               out << type << "& " << normalizeIdent(param.name);
            } else {
               out << type << " " << normalizeIdent(param.name);
            }
         }
         out << ") = 0;\n";
      }
      out << "    virtual ~" << name << "() = default;\n";
      out << "};\n\n";
   }

   // Generate getter/setter for global variables with AT addresses
   bool hasGlobalAT = false;
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            hasGlobalAT = true;
            break;
         }
      }
      if (hasGlobalAT) {
         break;
      }
   }

   if (hasGlobalAT) {
      out << "// GLOBAL AT VARIABLES (getter/setter)\n";
      for (const auto& sec : tu.globals) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               std::string upperName = normalizeIdent(d.name);
               std::string ctype = mapType(d.type);

               AddressExpr addr;
               if (parseAddressString(d.atAddress, addr)) {
                  // Getter
                  out << "// AT " << d.atAddress << "\n";
                  out << "inline " << ctype << " getPi_" << upperName << "() {\n";
                  out << "    return " << generateAddressAccess(addr) << ";\n";
                  out << "}\n";
                  // Setter
                  out << "inline void setPi_" << upperName << "(" << ctype << " value) {\n";
                  out << "    " << generateAddressWrite(addr, "value") << ";\n";
                  out << "}\n";
               }
            }
         }
      }
      out << "\n";
   }

   // Build a cache for structContainsFB to avoid repeated recursion
   std::unordered_map<std::string, bool> structContainsFBCache;
   for (const auto& st : tu.structs) {
      std::string structName = normalizeType(st.name);
      structContainsFBCache[structName] = structContainsFB(structName, tu);
   }

   // Separate structs with and without FB members
   std::vector<StructType> simpleStructs;  // Without FB
   std::vector<StructType> complexStructs; // With FB

   for (const auto& st : tu.structs) {
      std::string structName = normalizeType(st.name);
      if (structContainsFBCache[structName]) {
         complexStructs.push_back(st);
      } else {
         simpleStructs.push_back(st);
      }
   }

   // Generate simple structs in dependency order
   if (!simpleStructs.empty()) {
      generateStructsInOrder(simpleStructs, &out);
   }

   // Generate simple global variables (that don't involve FB types and are NOT AT)
   if (!tu.globals.empty()) {
      out << "// GLOBAL VARIABLES (simple types)\n";
      for (const auto& sec : tu.globals) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               continue; // Already generated as getter/setter
            }
            // Check if this global involves a FB type
            bool isFBType = false;
            if (d.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(d.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isFBType = true;
               }
            }

            // Also check if struct members contain FB (for array types)
            if (!isFBType && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(d.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isFBType = true;
               }
            }

            // Only simple globals go in SimpleGVLs
            if (!isFBType) {
               std::string ctype = mapType(d.type);
               std::string upperName = normalizeIdent(d.name);
               m_varTypes[upperName] = ctype;
               if (!d.type.arrayDims.empty()) {
                  std::string arrayDecl = ctype + " " + upperName;
                  if (d.initialValue) {
                     out << "inline " << arrayDecl << " = " << genExpr(*d.initialValue) << ";\n";
                  } else {
                     out << "inline " << arrayDecl << "{};\n";
                  }
               } else {
                  if (d.initialValue) {
                     out << "inline " << ctype << " " << upperName << " = " << genExpr(*d.initialValue) << ";\n";
                  } else {
                     out << "inline " << ctype << " " << upperName << "{};\n";
                  }
               }
            }
         }
      }
      out << "\n";
   }

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate GVLs header file (complex structs with FB + extern declarations)
 * 
 * This file contains:
 * - STRUCT types that contain Function Block members
 * - extern declarations for global variables of complex types (FB or struct with FB)
 * 
 * GVLs.hpp depends on FunctionBlocks.hpp (which includes SimpleGVLs.hpp).
 *
 * @param tu Translation unit
 * @return File content as string
 */
std::string CodeGenerator::generateGVLsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";

   m_hasAddresses = false;
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_hasAddresses = true;
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }
   if (m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"FunctionBlocks.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Build cache for structContainsFB
   std::unordered_map<std::string, bool> structContainsFBCache;
   for (const auto& st : tu.structs) {
      std::string structName = normalizeType(st.name);
      structContainsFBCache[structName] = structContainsFB(structName, tu);
   }

   // Generate STRUCTs that contain FB members
   for (const auto& st : tu.structs) {
      std::string structName = normalizeType(st.name);
      if (structContainsFBCache[structName]) { // ← QUI LA CORREZIONE
         std::string upperName = normalizeType(st.name);
         out << "// STRUCT " << upperName << " (contains Function Blocks)\n";
         out << "struct " << upperName << " {\n";
         for (const auto& member : st.members) {
            std::string ctype = mapType(member.type);
            std::string upperMember = normalizeIdent(member.name);
            std::string init = member.initialValue ? "{" + genExpr(*member.initialValue) + "}" : "{}";
            out << "    " << ctype << " " << upperMember << init << ";\n";
         }
         out << "};\n\n";
      }
   }

   // Generate extern declarations for complex global variables (FB types or structs with FB)
   if (!tu.globals.empty()) {
      out << "// GLOBAL VARIABLE DECLARATIONS (defined in GVLs.cpp)\n";
      for (const auto& sec : tu.globals) {
         for (const auto& d : sec.decls) {
            // Check if this global involves a FB type
            bool isComplex = false;

            // Create a copy of the type to analyze
            TypeRef analyzedType = d.type;

            // Strip array dimensions to get to the base element type
            while (!analyzedType.arrayDims.empty()) {
               analyzedType.arrayDims.pop_back();
            }

            // Now check if the base type is a FB
            if (analyzedType.base == BaseType::NAMED) {
               std::string typeName = normalizeType(analyzedType.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isComplex = true;
               }
            }
            // Also check direct FB type (non-array)
            else if (d.type.base == BaseType::NAMED && !isComplex) {
               std::string typeName = normalizeType(d.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Complex globals get extern declaration here
            if (isComplex) {
               std::string ctype = mapType(d.type);
               std::string upperName = normalizeIdent(d.name);
               m_varTypes[upperName] = ctype;
               out << "extern " << ctype << " " << upperName << ";\n";
            }
         }
      }
      out << "\n";
   }

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate GVLs source file (definitions of complex global variables)
 * 
 * This file contains the actual definitions (storage allocation) for
 * complex global variables that were declared as extern in GVLs.hpp.
 *
 * @param tu Translation unit
 * @return File content as string
 */
std::string CodeGenerator::generateGVLsSource(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#include \"GVLs.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Define complex global variables (FB types or structs with FB)
   if (!tu.globals.empty()) {
      out << "// GLOBAL VARIABLE DEFINITIONS\n";
      for (const auto& sec : tu.globals) {
         for (const auto& d : sec.decls) {
            // Check if this global involves a FB type
            bool isComplex = false;
            if (d.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(d.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Also check array of FB
            if (!isComplex && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
               std::string typeName = normalizeType(d.type.name);
               if (m_isFB.find(typeName) != m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Complex globals get definition here
            if (isComplex) {
               std::string ctype = mapType(d.type);
               std::string upperName = normalizeIdent(d.name);
               m_varTypes[upperName] = ctype;
               if (d.initialValue) {
                  out << ctype << " " << upperName << " = " << genExpr(*d.initialValue) << ";\n";
               } else {
                  out << ctype << " " << upperName << "{};\n";
               }
            }
         }
      }
      out << "\n";
   }

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate master FunctionBlocks.hpp aggregator
 * @param fbNames List of all FB names in dependency order
 * @return File content as string
 */
std::string CodeGenerator::generateFunctionBlocksMaster(const std::vector<std::string>& fbNames)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";
   out << "#include \"SimpleGVLs.hpp\"\n\n";

   // Include all FB headers
   for (const auto& fbName : fbNames) {
      out << "#include \"FunctionBlocks/" << fbName << ".hpp\"\n";
   }

   return out.str();
}

/**
 * @brief Generate single function block header
 * @param pou FB POU
 * @param dependencies Set of FB names this FB depends on
 * @return File content as string
 */
std::string CodeGenerator::generateFBHeader(const POU& pou, const std::unordered_set<std::string>& dependencies)
{
   std::ostringstream out;
   std::string fbName = normalizeType(pou.name);
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";

   m_hasAddresses = false;
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_hasAddresses = true;
            break;
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }
   if (m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"SimpleGVLs.hpp\"\n";

   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);
         m_varTypes[varName] = varType;
      }
   }

   // Include dependencies (other FBs used as members)
   for (const auto& dep : dependencies) {
      out << "#include \"FunctionBlocks/" << dep << ".hpp\"\n";
   }

   if (!dependencies.empty()) {
      out << "\n";
   }

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Generate FB struct (reuse header part from genFunctionBlock)
   out << "// FUNCTION BLOCK " << fbName << "\n";

   if (!pou.extends.empty()) {
      out << "// Extends: " << normalizeType(pou.extends) << "\n";
   }
   for (const auto& iface : pou.implements) {
      out << "// Implements: " << normalizeType(iface) << "\n";
   }

   out << "struct " << fbName;

   // Base struct (EXTENDS)
   if (!pou.extends.empty()) {
      out << " : public " << normalizeType(pou.extends);
   }

   // Interfacce (IMPLEMENTS)
   for (size_t i = 0; i < pou.implements.size(); ++i) {
      if (i == 0 && pou.extends.empty()) {
         out << " : public " << normalizeType(pou.implements[i]);
      } else {
         out << ", public " << normalizeType(pou.implements[i]);
      }
   }

   out << " {\n";
   out << "public:\n";

   // Variables
   for (const auto& sec : pou.varSections) {
      std::string secLabel;
      switch (sec.kind) {
      case VarKind::INPUT:
         secLabel = "    // VAR_INPUT";
         break;
      case VarKind::OUTPUT:
         secLabel = "    // VAR_OUTPUT";
         break;
      case VarKind::IN_OUT:
         secLabel = "    // VAR_IN_OUT";
         break;
      case VarKind::EXTERNAL:
         secLabel = "    // VAR_EXTERNAL";
         break;
      case VarKind::GLOBAL:
         secLabel = "    // VAR_GLOBAL";
         break;
      case VarKind::TEMP:
         secLabel = "    // VAR_TEMP (local)";
         break;
      default:
         secLabel = "    // VAR";
         break;
      }
      out << secLabel << "\n";

      for (const auto& d : sec.decls) {
         std::string ctype = mapType(d.type);
         std::string varName = normalizeIdent(d.name);
         std::string init = d.initialValue ? "{" + genExpr(*d.initialValue) + "}" : "{}";

         if (sec.kind == VarKind::IN_OUT) {
            out << "    VAR_INOUT<" << ctype << "> " << varName << init << ";\n";
         } else {
            out << "    " << memberDecl(d) << ";\n";
         }
      }
   }

   // Setters/Getters
   out << "\n    // SETTER/GETTER\n";
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string ctype = mapType(d.type);
         std::string varName = normalizeIdent(d.name);
         if (sec.kind == VarKind::IN_OUT) {
            out << "    inline void set_" << varName << "(" << ctype << "& refVal) { " << varName << " = &refVal; }\n";
         } else if (sec.kind == VarKind::INPUT) {
            out << "    inline void set_" << varName << "(" << ctype << " val) { " << varName << " = val; }\n";
         } else if (sec.kind == VarKind::OUTPUT) {
            out << "    inline " << ctype << " get_" << varName << "() const { return " << varName << "; }\n";
         }
      }
   }
   out << "    // End SETTER/GETTER\n\n";

   // Constructor and methods
   out << "    " << fbName << "();\n";
   out << "    void operator()();\n";
   for (const auto& method : pou.methods) {
      std::string methodName = normalizeIdent(method.name);
      std::string retType = "void";
      if (!isVoidType(method.returnType)) {
         retType = mapType(method.returnType);
      }

      out << "    virtual " << retType << " " << methodName << "(";
      bool first = true;
      for (const auto& param : method.parameters) {
         if (!first) {
            out << ", ";
         }
         first = false;
         std::string type = mapType(param.type);
         if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
            out << type << "& " << normalizeIdent(param.name);
         } else {
            out << type << " " << normalizeIdent(param.name);
         }
      }
      out << ")";
      if (method.isOverride) {
         out << " override";
      }
      if (method.isFinal) {
         out << " final";
      }
      if (method.isAbstract) {
         out << " = 0";
      }
      out << ";\n";
   }
   out << "    virtual ~" << fbName << "() = default;\n";
   out << "};\n\n";

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Collect all function blocks from translation unit
 * @param tu Translation unit
 * @return Map of FB name -> POU
 */
std::unordered_map<std::string, POU> CodeGenerator::collectFunctionBlocks(const TranslationUnit& tu)
{
   std::unordered_map<std::string, POU> fbMap;

   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         std::string name = normalizeType(pou.name);
         fbMap[name] = pou;
      }
   }

   return fbMap;
}

/**
 * @brief Generate Functions header file
 * @param tu Translation unit
 * @return File content as string
 */
std::string CodeGenerator::generateFunctionsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";

   m_hasAddresses = false;
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               if (!d.atAddress.empty()) {
                  m_hasAddresses = true;
                  break;
               }
            }
            if (m_hasAddresses) {
               break;
            }
         }
      }
      if (m_hasAddresses) {
         break;
      }
   }
   if (m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"GVLs.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Generate function declarations
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         emitFunctionDecl(pou, out);
         out << ";\n";
      }
   }

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate Functions source file
 * @param tu Translation unit
 * @return File content as string
 */
std::string CodeGenerator::generateFunctionsSource(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#include \"Functions.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         std::string upperName = normalizeIdent(pou.name);
         bool hasReturn = !isVoidType(pou.returnType);

         emitFunctionDecl(pou, out);
         out << " {\n";

         if (hasReturn) { // CAMBIATO: usa hasReturn
            out << "    " << mapType(pou.returnType) << " " << upperName << "_ret{};\n";
         }

         for (const auto& sec : pou.varSections) {
            if (sec.kind == VarKind::VAR || sec.kind == VarKind::TEMP) {
               for (const auto& d : sec.decls) {
                  out << "    " << memberDecl(d) << ";\n";
               }
            }
         }

         std::string body = generateFunctionBody(pou);
         out << body; // body already contains indented statements

         if (hasReturn) { // CAMBIATO: usa hasReturn
            out << "    return " << upperName << "_ret;\n";
         }

         out << "}\n\n";
      }
   }

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate single function block source
 * @param pou FB POU
 * @return File content as string
 */
/**
 * @brief Generate single function block source
 * @param pou FB POU
 * @return File content as string
 */
std::string CodeGenerator::generateFBSource(const POU& pou)
{
   std::ostringstream out;
   std::string fbName = normalizeType(pou.name);

   // register FB variables (fundamental for methods)
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         m_varTypes[normalizeIdent(d.name)] = mapType(d.type);
      }
   }

   out << generateHeaderComment();
   out << "#include \"FunctionBlocks/" << fbName << ".hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // Constructor
   out << fbName << "::" << fbName << "() {}\n\n";

   // operator()
   out << "void " << fbName << "::operator()() {\n";
   std::string opBody = generateFBOperatorBody(pou);
   out << opBody;
   out << "}\n\n";

   // Base Function blocks for calls with SUPER^
   if (!pou.extends.empty()) {
      m_currentBaseClass = normalizeType(pou.extends);
   } else {
      m_currentBaseClass.clear();
   }

   // Methods
   for (const auto& method : pou.methods) {
      std::string methodName = normalizeIdent(method.name);
      std::string retType = "void";

      bool hasReturn = !isVoidType(method.returnType);
      if (hasReturn) {
         retType = mapType(method.returnType);
      }

      out << retType << " " << fbName << "::" << methodName << "(";
      bool first = true;
      for (const auto& param : method.parameters) {
         if (!first) {
            out << ", ";
         }
         first = false;
         std::string type = mapType(param.type);
         if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
            out << type << "& " << normalizeIdent(param.name);
         } else {
            out << type << " " << normalizeIdent(param.name);
         }
      }
      out << ") {\n";

      // Return variable
      if (hasReturn) {
         out << "    " << retType << " " << methodName << "_ret{};\n";
      }

      // Method body
      std::string body = generateMethodBody(method);
      out << body;

      if (hasReturn) {
         out << "    return " << methodName << "_ret;\n";
      }
      out << "}\n\n";
   }

   // Reset classe base
   m_currentBaseClass.clear();

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate master Programs.hpp aggregator
 * @param progNames List of all program names
 * @return File content as string
 */
std::string CodeGenerator::generateProgramsMaster(const std::vector<std::string>& progNames)
{
   std::ostringstream out;
   out << generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";
   out << "#include \"FunctionBlocks.hpp\"\n\n";

   // Include all program headers
   for (const auto& progName : progNames) {
      out << "#include \"Programs/" << progName << ".hpp\"\n";
   }

   return out.str();
}

/**
 * @brief Generate single program header
 * @param pou The program POU
 * @return File content as string
 */
std::string CodeGenerator::generateProgramHeader(const POU& pou)
{
   std::ostringstream out;
   out << generateHeaderComment();
   std::string progName = normalizeType(pou.name);

   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_namespace << "\n";
   out << "#include \"" << m_runtimeHeader << "\"\n";

   if (m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"FunctionBlocks.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   out << "// PROGRAM " << progName << "\n";
   out << "struct " << progName << " {\n";

   // Member variables - only non-AT variables
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);

         // Register as local variable
         m_localVarTypes[varName] = varType;

         if (d.atAddress.empty()) {
            out << "    " << memberDecl(d) << ";\n";
         } else {
            // Register as local AT variable
            m_localAtVariables[varName] = d.atAddress;
         }
      }
   }

   // Generate getter/setter for AT addresses
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            std::string varName = normalizeIdent(d.name);
            std::string ctype = mapType(d.type);

            AddressExpr addr;
            if (parseAddressString(d.atAddress, addr)) {
               // Generate getter
               out << "    // AT " << d.atAddress << "\n";
               out << "    inline " << ctype << " getPi_" << varName << "() const {\n";
               out << "        return " << generateAddressAccess(addr) << ";\n";
               out << "    }\n";
               // Generate setter
               out << "    inline void setPi_" << varName << "(" << ctype << " value) {\n";
               out << "        " << generateAddressWrite(addr, "value") << ";\n";
               out << "    }\n";
            }
         }
      }
   }

   out << "\n";
   out << "    void run();\n";
   out << "};\n\n";

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Generate single program source
 * @param pou The program POU
 * @return File content as string
 */
std::string CodeGenerator::generateProgramSource(const POU& pou)
{
   std::ostringstream out;
   out << generateHeaderComment();
   std::string progName = normalizeType(pou.name);

   out << "#include \"Programs/" << progName << ".hpp\"\n";
   out << "#include \"Functions.hpp\"\n\n";

   if (!m_namespace.empty()) {
      out << "namespace " << m_namespace << " {\n\n";
   }

   // run() method
   out << "void " << progName << "::run() {\n";

   m_localVarTypes.clear();
   m_localAtVariables.clear();
   // Register variable types and AT variables for expression generation
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         m_localVarTypes[varName] = mapType(d.type);
         if (!d.atAddress.empty()) {
            m_localAtVariables[varName] = d.atAddress;
         }
      }
   }

   // Generate body using generateProgramBody
   std::string body = generateProgramBody(pou);
   out << body;
   out << "}\n\n";

   if (!m_namespace.empty()) {
      out << "} // namespace " << m_namespace << "\n";
   }

   return out.str();
}

/**
 * @brief Build dependency graph between structs
 * @param tu Translation unit  
 * @return Map of struct name -> set of struct names it depends on
 */
BuildStructDepType CodeGenerator::buildStructDependencies(const TranslationUnit& tu)
{
   BuildStructDepType deps;

   for (const auto& st : tu.structs) {
      std::string structName = normalizeType(st.name);
      deps[structName]; // Ensure entry exists

      for (const auto& member : st.members) {
         if (member.type.base == BaseType::NAMED) {
            std::string memberType = normalizeType(member.type.name);
            // Check if member type is another struct
            bool isStruct = false;
            for (const auto& otherStruct : tu.structs) {
               if (normalizeType(otherStruct.name) == memberType) {
                  isStruct = true;
                  break;
               }
            }
            if (isStruct && memberType != structName) {
               deps[structName].insert(memberType);
            }
         }
      }
   }

   return deps;
}

/**
 * @brief Topological sort of structs (Kahn's algorithm)
 * @param dependencies Dependency map (struct -> set of structs it depends on)
 * @return Ordered list of struct names (no cycles)
 * @throws std::runtime_error if circular dependency detected
 */
std::vector<std::string> CodeGenerator::topologicalSortStructs(const BuildStructDepType& dependencies)
{
   std::unordered_map<std::string, int> inDegree;
   BuildStructDepType reverseAdj;

   // Initialize
   for (const auto& [node, deps] : dependencies) {
      inDegree[node] = deps.size();
      for (const auto& dep : deps) {
         reverseAdj[dep].insert(node);
      }
   }

   // Queue for nodes with zero in-degree
   std::queue<std::string> q;
   for (const auto& [node, degree] : inDegree) {
      if (degree == 0) {
         q.push(node);
      }
   }

   std::vector<std::string> result;
   while (!q.empty()) {
      std::string node = q.front();
      q.pop();
      result.push_back(node);

      for (const auto& dependent : reverseAdj[node]) {
         inDegree[dependent]--;
         if (inDegree[dependent] == 0) {
            q.push(dependent);
         }
      }
   }

   // Check for cycles
   if (result.size() != dependencies.size()) {
      throw std::runtime_error("Circular dependency detected among structs!");
   }

   return result;
}

/**
 * @brief Generate function body as string
 * @param pou The function POU
 * @return Function body code as string
 */
std::string CodeGenerator::generateFunctionBody(const POU& pou)
{
   std::ostringstream out;
   std::string upperName = normalizeIdent(pou.name);

   // Save return type
   bool hasReturn = !isVoidType(pou.returnType);
   std::string savedReturnType = m_currentFunctionReturnType;
   m_currentFunctionReturnType = hasReturn ? mapType(pou.returnType) : "";

   // Save current state
   std::string savedFunctionName = m_currentFunctionName;
   m_currentFunctionName = upperName;

   // Save original stream and create new one
   std::ostringstream originalSrc;
   std::swap(m_src, originalSrc);

   // Create a new stream for capturing output
   std::ostringstream captureStream;
   m_src = std::move(captureStream);

   // Reset indent for body generation (starts at 1 because function body is indented)
   int savedIndent = m_indent;
   m_indent = 1; // Function body starts with one level of indentation

   pushTempScope();

   // Generate body using existing genStmt (now writes to m_src)
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }

   popTempScope();

   // Restore indent
   m_indent = savedIndent;

   // Capture the generated code
   std::string body = m_src.str();

   // Restore original stream
   std::swap(m_src, originalSrc);

   m_currentFunctionName = savedFunctionName;
   m_currentFunctionReturnType = savedReturnType;

   return body;
}

/**
 * @brief Generate method body as string
 * @param method The method
 * @return Method body code as string
 */
std::string CodeGenerator::generateMethodBody(const Method& method)
{
   std::ostringstream out;
   std::string methodName = normalizeIdent(method.name);

   // Save return type
   bool hasReturn = !isVoidType(method.returnType);
   std::string savedReturnType = m_currentFunctionReturnType;
   m_currentFunctionReturnType = hasReturn ? mapType(method.returnType) : "";

   // Save current state
   std::string savedFunctionName = m_currentFunctionName;
   m_currentFunctionName = methodName;

   // Save original stream and create new one
   std::ostringstream originalSrc;
   std::swap(m_src, originalSrc);

   std::ostringstream captureStream;
   m_src = std::move(captureStream);

   // Reset indent for body generation
   int savedIndent = m_indent;
   m_indent = 1; // Method body starts with one level of indentation

   pushTempScope();

   // Generate local variables with proper indentation
   for (const auto& local : method.localVars) {
      out << ind() << memberDecl(local) << ";\n";
   }

   // Generate body statements
   for (const auto& stmt : method.body) {
      genStmt(*stmt);
   }

   popTempScope();

   // Restore indent
   m_indent = savedIndent;

   // Capture the generated code
   std::string body = m_src.str();

   // Restore original stream
   std::swap(m_src, originalSrc);

   m_currentFunctionName = savedFunctionName;
   m_currentFunctionReturnType = savedReturnType;

   // Combine local vars and body
   return out.str() + body;
}

/**
 * @brief Generate FB operator() body as string
 * @param pou The FB POU
 * @return Operator body code as string
 */
std::string CodeGenerator::generateFBOperatorBody(const POU& pou)
{
   std::ostringstream out;

   // Save original stream and create new one
   std::ostringstream originalSrc;
   std::swap(m_src, originalSrc);

   std::ostringstream captureStream;
   m_src = std::move(captureStream);

   // Reset indent for body generation
   int savedIndent = m_indent;
   m_indent = 1; // operator() body starts with one level of indentation

   // Generate VAR_TEMP local variables
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            std::string ctype = mapType(d.type);
            std::string varName = normalizeIdent(d.name);
            std::string init = d.initialValue ? "{" + genExpr(*d.initialValue) + "}" : "{}";
            out << ind() << ctype << " " << varName << init << ";\n";
         }
      }
   }

   // Generate body statements
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }

   // Restore indent
   m_indent = savedIndent;

   // Capture the generated code
   std::string body = m_src.str();

   // Restore original stream
   std::swap(m_src, originalSrc);

   // Combine VAR_TEMP declarations and body
   return out.str() + body;
}

/**
 * @brief Generate program run() body as string
 * @param pou The program POU
 * @return Run method body code as string
 */
std::string CodeGenerator::generateProgramBody(const POU& pou)
{
   std::ostringstream out;

   // Register variable types for expression generation
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = normalizeIdent(d.name);
         std::string varType = mapType(d.type);
         m_varTypes[varName] = varType;
         m_localVarTypes[varName] = varType;
         if (!d.atAddress.empty()) {
            m_localAtVariables[varName] = d.atAddress;
         }
      }
   }

   // Save the original stream
   std::ostringstream originalSrc;
   std::swap(m_src, originalSrc);

   // Create a new stream to save the output
   std::ostringstream captureStream;
   m_src = std::move(captureStream);

   // Right indent (level 1 for the method body)
   int savedIndent = m_indent;
   m_indent = 1;

   pushTempScope();

   // Generate VAR_TEMP locals
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            if (d.atAddress.empty()) {
               std::string ctype = mapType(d.type);
               std::string varName = normalizeIdent(d.name);
               std::string init = d.initialValue ? "{" + genExpr(*d.initialValue) + "}" : "{}";
               m_src << ind() << ctype << " " << varName << init << ";\n";
            }
         }
      }
   }

   // Generate body by using genStmt
   for (const auto& stmt : pou.body) {
      genStmt(*stmt);
   }

   popTempScope();

   // Restore indent
   m_indent = savedIndent;

   // Save generated code
   std::string body = m_src.str();

   // Restore the original stream
   std::swap(m_src, originalSrc);

   return body;
}

/**
 * @brief Generate the header comment for all generated files
 * @return String containing the header comment
 */
std::string CodeGenerator::generateHeaderComment() const
{
   std::ostringstream out;
   out << "/**\n";
   out << " * @file GENERATED FILE - DO NOT EDIT MANUALLY\n";
   out << " * @brief Automatically generated from Structured Text source\n";
   out << " *\n";
   out << " * This file was generated by st2cpp, the Structured Text to C++ compiler.\n";
   out << " * Any manual changes will be overwritten the next time the source is processed.\n";
   out << " *\n";
   out << " * @copyright Copyright (c) 2026 Salvatore Bamundo\n";
   out << " * @license SPDX-License-Identifier: GPL-3.0-or-later\n";
   out << " *\n";
   out << " * st2cpp - Structured Text to C++ Compiler\n";
   out << " * This is free software; see the source for copying conditions.\n";
   out << " */\n\n";
   return out.str();
}

// ============================================================================
//  ProcessImageAnalyzer Implementation
// ============================================================================

void ProcessImageAnalyzer::analyze(const TranslationUnit& tu)
{
   // Reset state
   m_addressInfos.clear();
   m_typeMap.clear();

   // Initialize type map
   m_typeMap[AddressExpr::AddressType::INPUT] = {AddressExpr::AddressType::INPUT, 0, 0, false};
   m_typeMap[AddressExpr::AddressType::OUTPUT] = {AddressExpr::AddressType::OUTPUT, 0, 0, false};
   m_typeMap[AddressExpr::AddressType::MARKER] = {AddressExpr::AddressType::MARKER, 0, 0, false};

   // Scan all POUs
   for (const auto& pou : tu.pous) {
      for (const auto& stmt : pou.body) {
         findAddresses(stmt);
      }
   }

   // Scan global variables for AT addresses
   for (const auto& sec : tu.globals) {
      for (const auto& decl : sec.decls) {
         if (!decl.atAddress.empty()) {
            // Parse the AT address string
            AddressExpr addr;
            if (parseAddressString(decl.atAddress, addr)) {
               updateMaxOffset(addr);
            }
         }
      }
   }
}

void ProcessImageAnalyzer::findAddresses(const std::shared_ptr<Stmt>& stmt)
{
   if (!stmt) {
      return;
   }

   std::visit(
      [this](const auto& s) {
         using T = std::decay_t<decltype(s)>;

         if constexpr (std::is_same_v<T, AssignStmt>) {
            findAddressesInExpr(s.lhs);
            findAddressesInExpr(s.rhs);
         } else if constexpr (std::is_same_v<T, ExprStmt>) {
            findAddressesInExpr(s.expr);
         } else if constexpr (std::is_same_v<T, IfStmt>) {
            for (const auto& branch : s.branches) {
               if (branch.condition) {
                  findAddressesInExpr(branch.condition);
               }
               for (const auto& st : branch.body) {
                  findAddresses(st);
               }
            }
         } else if constexpr (std::is_same_v<T, ForStmt>) {
            findAddressesInExpr(s.from);
            findAddressesInExpr(s.to);
            if (s.by) {
               findAddressesInExpr(s.by);
            }
            for (const auto& st : s.body) {
               findAddresses(st);
            }
         } else if constexpr (std::is_same_v<T, WhileStmt>) {
            findAddressesInExpr(s.condition);
            for (const auto& st : s.body) {
               findAddresses(st);
            }
         } else if constexpr (std::is_same_v<T, RepeatStmt>) {
            for (const auto& st : s.body) {
               findAddresses(st);
            }
            findAddressesInExpr(s.condition);
         } else if constexpr (std::is_same_v<T, CaseStmt>) {
            findAddressesInExpr(s.selector);
            for (const auto& branch : s.branches) {
               for (const auto& cv : branch.values) {
                  findAddressesInExpr(cv.low);
                  if (cv.high) {
                     findAddressesInExpr(cv.high);
                  }
               }
               for (const auto& st : branch.body) {
                  findAddresses(st);
               }
            }
         }
         // ReturnStmt, ExitStmt, EmptyStmt have no expressions
      },
      stmt->node);
}

void ProcessImageAnalyzer::findAddressesInExpr(const std::shared_ptr<Expr>& expr)
{
   if (!expr) {
      return;
   }

   std::visit(
      [this](const auto& e) {
         using T = std::decay_t<decltype(e)>;

         if constexpr (std::is_same_v<T, AddressExpr>) {
            updateMaxOffset(e);
         } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            findAddressesInExpr(e.operand);
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            findAddressesInExpr(e.left);
            findAddressesInExpr(e.right);
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            findAddressesInExpr(e.object);
         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            findAddressesInExpr(e.array);
            for (const auto& idx : e.indices) {
               findAddressesInExpr(idx);
            }
         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            findAddressesInExpr(e.pointer);
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            findAddressesInExpr(e.callee);
            for (const auto& arg : e.args) {
               findAddressesInExpr(arg.value);
            }
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            findAddressesInExpr(e.operand);
         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            for (const auto& elem : e.elements) {
               findAddressesInExpr(elem);
            }
         }
         // LiteralExpr, BoolLitExpr, IdentExpr, SuperCallExpr, AdrExpr, SizeofExpr have no nested addresses
      },
      expr->node);
}

void ProcessImageAnalyzer::updateMaxOffset(const AddressExpr& addr)
{
   auto it = m_typeMap.find(addr.type);
   if (it == m_typeMap.end()) {
      return;
   }

   AddressInfo& info = it->second;

   // Update byte offset (considering the size of the access)
   int sizeInBytes = 1;
   switch (addr.qualifier) {
   case AddressExpr::AddressQualifier::WORD:
      sizeInBytes = 2;
      break;
   case AddressExpr::AddressQualifier::DWORD:
      sizeInBytes = 4;
      break;
   case AddressExpr::AddressQualifier::LWORD:
      sizeInBytes = 8;
      break;
   default:
      sizeInBytes = 1;
      break;
   }

   int endOffset = addr.byteOffset + sizeInBytes - 1;
   if (endOffset > info.maxByteOffset) {
      info.maxByteOffset = endOffset;
   }

   if (addr.qualifier == AddressExpr::AddressQualifier::BIT) {
      info.hasBitAccess = true;
      if (addr.bitOffset > info.maxBitOffset) {
         info.maxBitOffset = addr.bitOffset;
      }
   }
}

ProcessImageConfig ProcessImageAnalyzer::getRecommendedConfig() const
{
   ProcessImageConfig config;
   config.autoDetect = true;

   if (m_addressInfos.empty()) {
      // No addresses used - use minimum sizes
      config.inputBytes = 1024;
      config.outputBytes = 1024;
      config.markerBytes = 1024;
      return config;
   }

   // Calculate sizes from type map
   for (const auto& [type, info] : m_typeMap) {
      size_t needed = info.maxByteOffset + 1;
      if (info.hasBitAccess) {
         // Ensure at least one byte for bit access
         needed = std::max(needed, size_t(1));
      }

      switch (type) {
      case AddressExpr::AddressType::INPUT:
         config.inputBytes = nextPowerOfTwo(needed);
         break;
      case AddressExpr::AddressType::OUTPUT:
         config.outputBytes = nextPowerOfTwo(needed);
         break;
      case AddressExpr::AddressType::MARKER:
         config.markerBytes = nextPowerOfTwo(needed);
         break;
      default:
         break;
      }
   }

   // Minimum sizes (like Siemens S7-1200)
   config.inputBytes = std::max(config.inputBytes, size_t(1024));
   config.outputBytes = std::max(config.outputBytes, size_t(1024));
   config.markerBytes = std::max(config.markerBytes, size_t(1024));

   return config;
}

size_t ProcessImageAnalyzer::nextPowerOfTwo(size_t n) const
{
   if (n <= 1) {
      return 1;
   }
   size_t power = 1;
   while (power < n) {
      power <<= 1;
   }
   return power;
}