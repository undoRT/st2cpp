/**
 * @file ProjectEmitter.cpp
 * @brief File assembly: the flat translation unit and the modular project
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/ProjectEmitter.h"
#include "semantic/IecTime.h"
#include <algorithm>
#include <queue>


namespace st2cpp::codegen {


CodegenResult ProjectEmitter::generate(const TranslationUnit& tu,
                                      const std::string& headerName,
                                      const std::string& namespaceName,
                                      const std::string& runtimeHeader,
                                      bool caseSensitive)
{
m_ctx.m_hdrName = headerName;
    m_ctx.m_namespace = namespaceName;
    m_ctx.m_runtimeHeader = runtimeHeader;
    m_ctx.m_caseSensitive = caseSensitive;
   m_ctx.rebuildSemanticComponents();

    // Pre-scan the TU for the libraries actually used (types + symbol refs)
    // so their descriptor includes can be emitted right after the runtime header.
    m_ctx.computeLibraryIncludeLines(tu);

   // Build header guard from filename
   std::string guard = headerName;
   std::transform(guard.begin(), guard.end(), guard.begin(), ::toupper);
   for (auto& c : guard) {
      if (c == '.' || c == '/' || c == '-') {
         c = '_';
      }
   }
   guard += "_HPP";

   // ======================================================================
   //  PHASE 1: COLLECT AND ALLOCATE AT ADDRESSES
   // ======================================================================

   // Clear resolved addresses map
   m_ctx.m_resolvedATAddresses.clear();

   // Collect all AT declarations
   struct ATDeclaration
   {
      std::string varName;
      std::string pouName; // Empty for globals
      AddressExpr addr;
      TypeRef type;
      bool isPlaceholder;
   };
   std::vector<ATDeclaration> atDeclarations;

   // Collect from global variables
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            AddressExpr addr;
            if (parseAddressString(d.atAddress, addr)) {
               // If the address qualifier was inferred (e.g., %I*), deduce it from
               // the declared variable type.
               if (addr.qualifierInferred) {
                  addr.qualifier = m_decl.deduceQualifierFromType(d.type);
                  addr.qualifierInferred = false;
               }
               ATDeclaration decl;
               decl.varName = m_ctx.normalizeIdent(d.name);
               decl.pouName = "";
               decl.addr = addr;
               decl.type = d.type;
               decl.isPlaceholder = addr.isPlaceholder;
               atDeclarations.push_back(decl);
            }
         }
      }
   }

   // Collect from POUs
   for (const auto& pou : tu.pous) {
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               AddressExpr addr;
               if (parseAddressString(d.atAddress, addr)) {
                  // If the address qualifier was inferred (e.g., %I*), deduce it from
                  // the declared variable type.
                  if (addr.qualifierInferred) {
                     addr.qualifier = m_decl.deduceQualifierFromType(d.type);
                     addr.qualifierInferred = false;
                  }
                  ATDeclaration decl;
                  decl.varName = m_ctx.normalizeIdent(d.name);
                  decl.pouName = m_ctx.normalizeType(pou.name);
                  decl.addr = addr;
                  decl.type = d.type;
                  decl.isPlaceholder = addr.isPlaceholder;
                  atDeclarations.push_back(decl);
               }
            }
         }
      }
   }

   // Mark fixed addresses as occupied
   for (auto& decl : atDeclarations) {
      if (!decl.isPlaceholder) {
         int size = m_ctx.getTypeSizeInBytes(decl.type);
         m_ctx.m_addressAllocator.markFixedAddress(decl.addr, size);
      }
   }

   // Allocate placeholder addresses
   for (auto& decl : atDeclarations) {
      if (decl.isPlaceholder) {
         int size = m_ctx.getTypeSizeInBytes(decl.type);
         int alignment = m_ctx.getTypeAlignment(decl.type);
         auto result = m_ctx.m_addressAllocator.allocatePlaceholder(decl.addr.type, decl.addr.qualifier, size, alignment);
         if (result.success) {
            // Update the address with the allocated offset
            AddressExpr resolvedAddr = decl.addr;
            resolvedAddr.byteOffset = result.byteOffset;
            resolvedAddr.bitOffset = result.bitOffset;
            resolvedAddr.isPlaceholder = false;
            // Store the resolved address for later use
            std::string key = decl.pouName.empty() ? decl.varName : decl.pouName + "::" + decl.varName;
            m_ctx.m_resolvedATAddresses[key] = resolvedAddr;
         } else {
            throw std::runtime_error("Failed to allocate AT address for variable: " + decl.varName);
         }
      }
   }

   // Update Process Image configuration with actual region sizes
   for (auto type : {AddressExpr::AddressType::INPUT, AddressExpr::AddressType::OUTPUT, AddressExpr::AddressType::MARKER}) {
      size_t regionSize = m_ctx.m_addressAllocator.getRegionSize(type);
      if (regionSize == 0) {
         regionSize = 1; // minimum 1 byte
      }
      if (regionSize > 0) {
         switch (type) {
         case AddressExpr::AddressType::INPUT:
            m_ctx.m_piConfig.inputBytes = regionSize;
            break;
         case AddressExpr::AddressType::OUTPUT:
            m_ctx.m_piConfig.outputBytes = regionSize;
            break;
         case AddressExpr::AddressType::MARKER:
            m_ctx.m_piConfig.markerBytes = regionSize;
            break;
         default:
            break;
         }
      }
   }

   // Check if any AT addresses are used
   m_ctx.m_hasAddresses = !atDeclarations.empty();

   // Emit header prolog
   m_ctx.m_hdr << m_decl.generateHeaderComment();
   m_ctx.m_hdr << "#pragma once\n";
   m_ctx.m_hdr << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   if (m_ctx.m_runtimeHeader.find('/') != std::string::npos || m_ctx.m_runtimeHeader.find('\\') != std::string::npos) {
      // Path with slashes - include with full path
      m_ctx.m_hdr << "#include \"" << m_ctx.m_runtimeHeader << "\"\n\n";
   } else {
      // Just filename - include with relative path
      m_ctx.m_hdr << "#include \"" << m_ctx.m_runtimeHeader << "\"\n\n";
   }

   // External library descriptor includes (sorted, deduplicated, only the
   // libraries the TU actually uses). Emitted outside the namespace on purpose:
   // the loaded headers own their scoping rules.
   if (!m_ctx.libraryIncludeLines().empty()) {
      m_ctx.m_hdr << m_ctx.libraryIncludeBlock();
      m_ctx.m_hdr << "\n";
   }

   // print comment about auto-generated code
   m_ctx.m_src << m_decl.generateHeaderComment();
   // Open namespace if specified
   if (m_ctx.m_namespace.empty()) {
      m_ctx.m_src << "#include \"" << headerName << "\"\n\n";
   } else {
      m_ctx.m_hdr << "namespace " << m_ctx.m_namespace << " {\n\n";
      m_ctx.m_src << "#include \"" << headerName << "\"\n\n";
      m_ctx.m_src << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Generate Global process image
   if (m_ctx.m_hasAddresses) {
      m_ctx.m_hdr << "// Global Process Image instance\n";
      m_ctx.m_hdr << "inline ProcessImage<" << m_ctx.m_piConfig.inputBytes << ", " << m_ctx.m_piConfig.outputBytes << ", " << m_ctx.m_piConfig.markerBytes << "> "
            << m_ctx.m_piConfig.instanceName << ";\n\n";
   }

// Resolve TYPE aliases before any variable type is mapped.
    m_ctx.registerTypeAliases(tu.typeAliases);

    // Initialize the global scope
    m_ctx.m_scope.pushScope();

   // Register global variables in the global scope
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         std::string varName = m_ctx.normalizeIdent(d.name);
         std::string varType = m_ctx.mapType(d.type);
         m_ctx.m_scope.addVariable(varName, varType);
         if (!d.atAddress.empty()) {
            m_ctx.m_scope.addATVariable(varName, d.atAddress);
         }
      }
   }

   // Generate all AST components in dependency order
   for (const auto& en : tu.enums) {
      m_decl.genEnum(en); // Enums first (always independent)
   }

   // Store all the struct members
   for (const auto& st : tu.structs) {
      std::string name = m_ctx.normalizeType(st.name);
      m_ctx.m_structTypes.insert(name);
      std::vector<std::string> members;
      for (const auto& member : st.members) {
         members.push_back(m_ctx.normalizeIdent(member.name));
      }
      m_ctx.m_structMembers[name] = members;
   }

   // Generate interface
   for (const auto& iface : tu.interfaces) {
      m_decl.genInterface(iface);
   }

   // Collect FBs first to identify them
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         m_ctx.m_isFB[m_ctx.normalizeType(pou.name)] = true;
      }
      m_decl.collectSignature(pou);
   }

   // Separate structs
   std::vector<StructType> simpleStructs;  // Without FB members
   std::vector<StructType> complexStructs; // With FB members

   for (const auto& st : tu.structs) {
      if (m_ctx.structContainsFB(m_ctx.normalizeType(st.name), tu)) {
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
            std::string typeName = m_ctx.normalizeType(d.type.name);
            if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
               isComplex = true;
            }
         }
         // Also check array of FB
         if (!isComplex && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
            std::string typeName = m_ctx.normalizeType(d.type.name);
            if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
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
      m_decl.generateStructsInOrder(simpleStructs);
   }

   // Generate simple globals (without FB)
   if (!simpleGlobals.empty()) {
      m_decl.genGlobals(simpleGlobals);
      m_ctx.m_hdr << "\n";
   }

   // Generate POUs in correct dependency order: FBs first, then Functions, then Programs
   // First pass: generate all Function Blocks
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         m_ctx.m_scope.pushScope(); // scope for this FB
         // Register all variables of this FB
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string varType = m_ctx.mapType(d.type);
               m_ctx.m_scope.addVariable(varName, varType);
               if (!d.atAddress.empty()) {
                  m_ctx.m_scope.addATVariable(varName, d.atAddress);
               }
            }
         }
         // Set base class for SUPER^ if present
         if (!pou.extends.empty()) {
            m_ctx.m_scope.setBaseClass(m_ctx.normalizeType(pou.extends));
         }

         m_decl.genPOU(pou);

         m_ctx.m_scope.popScope();
      }
   }

   // Generate complex structs (with FB members) - after FBs are defined
   if (!complexStructs.empty()) {
      m_ctx.m_hdr << "// Complex STRUCTs (containing Function Blocks)\n";
      m_decl.generateStructsInOrder(complexStructs);
   }

   // Generate complex globals (with FB types) - after FBs are defined
   if (!complexGlobals.empty()) {
      m_ctx.m_hdr << "// Complex GLOBAL VARIABLES (containing Function Blocks)\n";
      m_decl.genGlobals(complexGlobals);
      m_ctx.m_hdr << "\n";
   }

   // Second pass: generate all Functions
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         m_ctx.m_scope.pushScope();
         m_ctx.m_scope.setFunctionScope(true);
         m_ctx.m_scope.setLocalToFunction(true);
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string varType = m_ctx.mapType(d.type);
               m_ctx.m_scope.addVariable(varName, varType);
               if (!d.atAddress.empty()) {
                  m_ctx.m_scope.addATVariable(varName, d.atAddress);
               }
            }
         }
         m_decl.genPOU(pou);
         m_ctx.m_scope.setFunctionScope(false);
         m_ctx.m_scope.setLocalToFunction(false);
         m_ctx.m_scope.popScope();
      }
   }

   // Third pass: generate all Programs
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::PROGRAM) {
         m_ctx.m_scope.pushScope();
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string varType = m_ctx.mapType(d.type);
               m_ctx.m_scope.addVariable(varName, varType);
               if (!d.atAddress.empty()) {
                  m_ctx.m_scope.addATVariable(varName, d.atAddress);
               }
            }
         }
         m_decl.genPOU(pou);
         m_ctx.m_scope.popScope();
      }
   }

   // Close global scope
   m_ctx.m_scope.popScope();

   // Close namespace if it was opened
   if (!m_ctx.m_namespace.empty()) {
      m_ctx.m_hdr << "} // namespace " << m_ctx.m_namespace << "\n";
      m_ctx.m_src << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return {m_ctx.m_hdr.str(), m_ctx.m_src.str()};
}

std::vector<GeneratedFile> ProjectEmitter::generateModularProject(const TranslationUnit& tu, const std::string& outputDir)
{
   m_ctx.m_projectStyle = ProjectStyle::MODULAR;
   m_ctx.m_outputDir = outputDir;

   // Clear internal streams (they won't be used in modular mode)
   m_ctx.m_hdr.str("");
   m_ctx.m_src.str("");
   m_ctx.m_hdr.clear();
   m_ctx.m_src.clear();

   // Reset state
   m_ctx.m_signatures.clear();
   m_ctx.m_methodSignatures.clear();
   m_ctx.m_enumTypes.clear();
   m_ctx.m_enumValues.clear();
   m_ctx.m_enumeratorToEnum.clear();
   m_ctx.m_fbMap.clear();
   m_ctx.m_isFB.clear();
   m_ctx.m_resolvedATAddresses.clear();

   // Pre-scan the TU so the modular headers can include the libraries used.
   m_ctx.computeLibraryIncludeLines(tu);

   return generateModular(tu, outputDir);
}

std::vector<GeneratedFile> ProjectEmitter::generateModular(const TranslationUnit& tu, const std::string& outputDir)
{
   std::vector<GeneratedFile> files;
   m_ctx.m_outputDir = outputDir;

   // Clear resolved addresses map
   m_ctx.m_resolvedATAddresses.clear();

   // Pre-scan the TU so the modular headers can include the libraries used.
   m_ctx.computeLibraryIncludeLines(tu);

// Resolve TYPE aliases before any variable type is mapped.
    m_ctx.registerTypeAliases(tu.typeAliases);

    // Step 1: Collect all signatures FIRST (before generating anything)
    for (const auto& pou : tu.pous) {
       m_decl.collectSignature(pou);
    }

   // Step 2: Register enum types and their enumerators
   for (const auto& et : tu.enums) {
      std::string upperName = m_ctx.normalizeType(et.name);
      m_ctx.m_enumTypes[upperName] = true;
      for (const auto& enumerator : et.enumerators) {
         std::string upperEnumerator = m_ctx.normalizeIdent(enumerator.name);
         m_ctx.m_enumValues[upperName].insert(upperEnumerator);
         m_ctx.m_enumeratorToEnum[upperEnumerator] = upperName;
      }
   }

   // Step 2.5: store all the struct members
   for (const auto& st : tu.structs) {
      std::string name = m_ctx.normalizeType(st.name);
      m_ctx.m_structTypes.insert(name);
      std::vector<std::string> members;
      for (const auto& member : st.members) {
         members.push_back(m_ctx.normalizeIdent(member.name));
      }
      m_ctx.m_structMembers[name] = members;
   }

   // Step 3: Collect all FBs and mark them
   m_ctx.m_fbMap = m_decl.collectFunctionBlocks(tu);
   for (const auto& [name, pou] : m_ctx.m_fbMap) {
      m_ctx.m_isFB[m_ctx.normalizeType(name)] = true;
   }

   // Step 3.5: Collect and allocate AT addresses
   struct ATDeclaration
   {
      std::string varName;
      std::string pouName;
      AddressExpr addr;
      TypeRef type;
      bool isPlaceholder;
   };
   std::vector<ATDeclaration> atDeclarations;

   // Collect from globals
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            AddressExpr addr;
            if (parseAddressString(d.atAddress, addr)) {
               // If the address qualifier was inferred (e.g., %I*), deduce it from
               // the declared variable type.
               if (addr.qualifierInferred) {
                  addr.qualifier = m_decl.deduceQualifierFromType(d.type);
                  addr.qualifierInferred = false;
               }
               ATDeclaration decl;
               decl.varName = m_ctx.normalizeIdent(d.name);
               decl.pouName = "";
               decl.addr = addr;
               decl.type = d.type;
               decl.isPlaceholder = addr.isPlaceholder;
               atDeclarations.push_back(decl);
            }
         }
      }
   }

   // Collect from POUs
   for (const auto& pou : tu.pous) {
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               AddressExpr addr;
               if (parseAddressString(d.atAddress, addr)) {
                  ATDeclaration decl;
                  decl.varName = m_ctx.normalizeIdent(d.name);
                  decl.pouName = m_ctx.normalizeType(pou.name);
                  decl.addr = addr;
                  decl.type = d.type;
                  decl.isPlaceholder = addr.isPlaceholder;
                  atDeclarations.push_back(decl);
               }
            }
         }
      }
   }

   // Mark fixed addresses
   for (auto& decl : atDeclarations) {
      if (!decl.isPlaceholder) {
         int size = m_ctx.getTypeSizeInBytes(decl.type);
         m_ctx.m_addressAllocator.markFixedAddress(decl.addr, size);
      }
   }

   // Allocate placeholders
   for (auto& decl : atDeclarations) {
      if (decl.isPlaceholder) {
         int size = m_ctx.getTypeSizeInBytes(decl.type);
         int alignment = m_ctx.getTypeAlignment(decl.type);
         auto result = m_ctx.m_addressAllocator.allocatePlaceholder(decl.addr.type, decl.addr.qualifier, size, alignment);
         if (result.success) {
            AddressExpr resolvedAddr = decl.addr;
            resolvedAddr.byteOffset = result.byteOffset;
            resolvedAddr.bitOffset = result.bitOffset;
            resolvedAddr.isPlaceholder = false;
            std::string key = decl.pouName.empty() ? decl.varName : decl.pouName + "::" + decl.varName;
            m_ctx.m_resolvedATAddresses[key] = resolvedAddr;
         } else {
            throw std::runtime_error("Failed to allocate AT address for variable: " + decl.varName);
         }
      }
   }

   // Update Process Image configuration
   for (auto type : {AddressExpr::AddressType::INPUT, AddressExpr::AddressType::OUTPUT, AddressExpr::AddressType::MARKER}) {
      size_t regionSize = m_ctx.m_addressAllocator.getRegionSize(type);
      if (regionSize == 0) {
         regionSize = 1; // minimum 1 byte
      }
      if (regionSize > 0) {
         switch (type) {
         case AddressExpr::AddressType::INPUT:
            m_ctx.m_piConfig.inputBytes = regionSize;
            break;
         case AddressExpr::AddressType::OUTPUT:
            m_ctx.m_piConfig.outputBytes = regionSize;
            break;
         case AddressExpr::AddressType::MARKER:
            m_ctx.m_piConfig.markerBytes = regionSize;
            break;
         default:
            break;
         }
      }
   }

   // Step 4: Register variable types for all POUs (for enum resolution)
   // We'll use the scope manager instead of m_varTypes.
   // The scope manager will be filled per POU during generation.

   // Step 4.5: Register global variable m_ctx.types(so they can be resolved later)
   // We'll store them in the global scope.
   m_ctx.m_scope.pushScope(); // global scope
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         std::string varName = m_ctx.normalizeIdent(d.name);
         std::string varType = m_ctx.mapType(d.type);
         m_ctx.m_scope.addVariable(varName, varType);
         if (!d.atAddress.empty()) {
            m_ctx.m_scope.addATVariable(varName, d.atAddress);
         }
      }
   }

   // Step 5: Build dependency graph (legacy, for the per-FB include lists)
   auto dependencies = m_ctx.buildFBDependencies(tu);

   // Step 6: Topological sort of FBs.
   // Semantic path (Fase 3): use fbTopoOrder computed by the semantic analyzer,
   // which covers BOTH inheritance (extends) and composition (FB-typed members
   // and parameters). Legacy fallback: name-graph sort (members/params only).
   std::vector<std::string> orderedFBs;
   if (m_ctx.semanticAvailable() && !m_ctx.m_semanticInfo->empty() && !m_ctx.m_semanticInfo->fbTopoOrder.empty()) {
      orderedFBs = m_ctx.orderedFbNamesFromSemantic();
   } else {
      if (!dependencies.empty()) {
         orderedFBs = m_ctx.topologicalSort(dependencies);
      }
   }

   // Step 7: Collect program names
   std::vector<std::string> programNames;
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::PROGRAM) {
         programNames.push_back(m_ctx.normalizeType(pou.name));
      }
   }

   // Step 7.5: Check if any AT addresses are used
   m_ctx.m_hasAddresses = !atDeclarations.empty();

   // ========== GENERATION ORDER ==========

   // 0. ProcessImage.hpp - Global Process Image (only an header, no source)
   if (m_ctx.m_hasAddresses) {
      std::ostringstream piHeader;
      piHeader << m_decl.generateHeaderComment();
      piHeader << "#pragma once\n";
      piHeader << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
      piHeader << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";

      if (!m_ctx.m_namespace.empty()) {
         piHeader << "namespace " << m_ctx.m_namespace << " {\n\n";
      }

      piHeader << "// Global Process Image instance\n";
      piHeader << "inline ProcessImage<" << m_ctx.m_piConfig.inputBytes << ", " << m_ctx.m_piConfig.outputBytes << ", " << m_ctx.m_piConfig.markerBytes
               << "> " << m_ctx.m_piConfig.instanceName << ";\n\n";

      if (!m_ctx.m_namespace.empty()) {
         piHeader << "} // namespace " << m_ctx.m_namespace << "\n";
      }

      files.push_back({"ProcessImage", piHeader.str(), GenFileType::HEADER, ""});
   }

   // 1. SimpleGVLs.hpp (ENUM + simple STRUCT + simple globals)
   files.push_back({"SimpleGVLs", generateSimpleGVLsHeader(tu), GenFileType::HEADER, ""});

   // 2. Generate each FB in its own file
   for (const auto& fbName : orderedFBs) {
      const auto& pou = m_ctx.m_fbMap[fbName];

      std::unordered_set<std::string> fbDeps;
      auto it = dependencies.find(fbName);
      if (it != dependencies.end()) {
         fbDeps = it->second;
      }

      // Base-class dependency for the per-FB include list.
      // Semantic path (Fase 7): prefers SemanticInfo::fbBaseClass when semantics
      // are attached; the AST syntax (pou.extends) is the legacy fallback, so
      // the degrade-without-semantics output is byte-identical.
      std::string semanticBase = m_ctx.semanticBaseForFb(pou);
      if (!semanticBase.empty() && m_ctx.m_isFB.find(semanticBase) != m_ctx.m_isFB.end()) {
         fbDeps.insert(semanticBase);
      }

      for (const auto& iface : pou.implements) {
         std::string ifaceName = m_ctx.normalizeType(iface);
      }

      // Push a scope for this FB
      m_ctx.m_scope.pushScope();
      // Register variables of the FB
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            std::string varName = m_ctx.normalizeIdent(d.name);
            std::string varType = m_ctx.mapType(d.type);
            m_ctx.m_scope.addVariable(varName, varType);
            if (!d.atAddress.empty()) {
               m_ctx.m_scope.addATVariable(varName, d.atAddress);
            }
         }
      }
      // SUPER^ scope base: canonical semantic name when available, else legacy.
      if (!semanticBase.empty()) {
         m_ctx.m_scope.setBaseClass(semanticBase);
      }

      files.push_back({fbName, generateFBHeader(pou, fbDeps), GenFileType::HEADER, "FunctionBlocks"});
      files.push_back({fbName, generateFBSource(pou), GenFileType::SOURCE, "FunctionBlocks"});

      m_ctx.m_scope.popScope();
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
         std::string progName = m_ctx.normalizeType(pou.name);
         // Push a scope for this program
         m_ctx.m_scope.pushScope();
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string varType = m_ctx.mapType(d.type);
               m_ctx.m_scope.addVariable(varName, varType);
               if (!d.atAddress.empty()) {
                  m_ctx.m_scope.addATVariable(varName, d.atAddress);
               }
            }
         }
         files.push_back({progName, generateProgramHeader(pou), GenFileType::HEADER, "Programs"});
         files.push_back({progName, generateProgramSource(pou), GenFileType::SOURCE, "Programs"});
         m_ctx.m_scope.popScope();
      }
   }

   // 8. Programs.hpp master
   if (!programNames.empty()) {
      files.push_back({"Programs", generateProgramsMaster(programNames), GenFileType::MASTER, ""});
   }

   // Pop global scope
   m_ctx.m_scope.popScope();

   return files;
}

std::string ProjectEmitter::generateSimpleGVLsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n\n";

   if (!m_ctx.libraryIncludeLines().empty()) {
      out << m_ctx.libraryIncludeBlock();
      out << "\n";
   }

   if (m_ctx.m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Generate ENUMs (always, they have no dependencies)
   for (const auto& et : tu.enums) {
      std::string upperName = m_ctx.normalizeType(et.name);
      // Register enum type for special handling
      m_ctx.m_enumTypes[upperName] = true;
      for (const auto& enumerator : et.enumerators) {
         std::string upperEnumerator = m_ctx.normalizeIdent(enumerator.name);
         m_ctx.m_enumValues[upperName].insert(upperEnumerator);
         m_ctx.m_enumeratorToEnum[upperEnumerator] = upperName;
      }

      out << "// ENUM " << upperName << "\n";
      out << "enum class " << upperName << " : int {\n";
      for (size_t i = 0; i < et.enumerators.size(); ++i) {
         const auto& enumerator = et.enumerators[i];
         std::string upperEnumerator = m_ctx.normalizeIdent(enumerator.name);
         out << "    " << upperEnumerator;
         if (enumerator.value) {
            out << " = " << m_body.genExpr(*enumerator.value);
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
      std::string name = m_ctx.normalizeType(iface.name);
      out << "// INTERFACE " << name << "\n";
      out << "struct " << name << " {\n";
      for (const auto& method : iface.methods) {
         std::string retType = "void";
         if (!m_ctx.isVoidType(method.returnType)) {
            retType = m_ctx.mapType(method.returnType);
         }
         out << "    virtual " << retType << " " << m_ctx.normalizeIdent(method.name) << "(";
         bool first = true;
         for (const auto& param : method.parameters) {
            if (!first) {
               out << ", ";
            }
            first = false;
            std::string type = m_ctx.mapType(param.type);
            if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
               out << type << "& " << m_ctx.normalizeIdent(param.name);
            } else {
               out << type << " " << m_ctx.normalizeIdent(param.name);
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
               std::string upperName = m_ctx.normalizeIdent(d.name);
               std::string ctype = m_ctx.mapType(d.type);

               AddressExpr addr;
               // Try to get resolved address
               auto it = m_ctx.m_resolvedATAddresses.find(upperName);
               if (it != m_ctx.m_resolvedATAddresses.end()) {
                  addr = it->second;
               } else if (parseAddressString(d.atAddress, addr)) {
                  // Use original (fixed)
               } else {
                  continue;
               }

               // Getter
               out << "// AT " << d.atAddress << "\n";
               out << "inline " << ctype << " getPi_" << upperName << "() {\n";
               out << "    return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
               out << "}\n";
               // Setter
               out << "inline void setPi_" << upperName << "(" << ctype << " value) {\n";
               out << "    " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
               out << "}\n";
            }
         }
      }
      out << "\n";
   }

   // Build a cache for structContainsFB to avoid repeated recursion
   std::unordered_map<std::string, bool> structContainsFBCache;
   for (const auto& st : tu.structs) {
      std::string structName = m_ctx.normalizeType(st.name);
      structContainsFBCache[structName] = m_ctx.structContainsFB(structName, tu);
   }

   // Separate structs with and without FB members
   std::vector<StructType> simpleStructs;  // Without FB
   std::vector<StructType> complexStructs; // With FB

   for (const auto& st : tu.structs) {
      std::string structName = m_ctx.normalizeType(st.name);
      if (structContainsFBCache[structName]) {
         complexStructs.push_back(st);
      } else {
         simpleStructs.push_back(st);
      }
   }

   // Generate simple structs in dependency order
   if (!simpleStructs.empty()) {
      m_decl.generateStructsInOrder(simpleStructs, &out);
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
               std::string typeName = m_ctx.normalizeType(d.type.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isFBType = true;
               }
            }

            // Also check if struct members contain FB (for array types)
            if (!isFBType && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
               std::string typeName = m_ctx.normalizeType(d.type.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isFBType = true;
               }
            }

            // Only simple globals go in SimpleGVLs
            if (!isFBType) {
               std::string ctype = m_ctx.mapType(d.type);
               std::string upperName = m_ctx.normalizeIdent(d.name);
               // Register in global scope
               m_ctx.m_scope.addVariable(upperName, ctype);
               if (!d.type.arrayDims.empty()) {
                  std::string arrayDecl = ctype + " " + upperName;
                  if (d.initialValue) {
                     std::string initStr = m_body.generateOrderedStructInit(d.type, d.initialValue);
                     out << "inline " << arrayDecl << " = " << initStr << ";\n";
                  } else {
                     out << "inline " << arrayDecl << "{};\n";
                  }
               } else {
                  if (d.initialValue) {
                     std::string initStr = m_body.generateOrderedStructInit(d.type, d.initialValue);
                     out << "inline " << ctype << " " << upperName << " = " << initStr << ";\n";
                  } else {
                     out << "inline " << ctype << " " << upperName << "{};\n";
                  }
               }
            }
         }
      }
      out << "\n";
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateGVLsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";

   if (!m_ctx.libraryIncludeLines().empty()) {
      out << m_ctx.libraryIncludeBlock();
      out << "\n";
   }

   m_ctx.m_hasAddresses = false;
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_ctx.m_hasAddresses = true;
            break;
         }
      }
      if (m_ctx.m_hasAddresses) {
         break;
      }
   }
   if (m_ctx.m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"FunctionBlocks.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Build cache for structContainsFB
   std::unordered_map<std::string, bool> structContainsFBCache;
   for (const auto& st : tu.structs) {
      std::string structName = m_ctx.normalizeType(st.name);
      structContainsFBCache[structName] = m_ctx.structContainsFB(structName, tu);
   }

   // Generate STRUCTs that contain FB members
   for (const auto& st : tu.structs) {
      std::string structName = m_ctx.normalizeType(st.name);
      if (structContainsFBCache[structName]) {
         std::string upperName = m_ctx.normalizeType(st.name);
         out << "// STRUCT " << upperName << " (contains Function Blocks)\n";
         out << "struct " << upperName << " {\n";
         for (const auto& member : st.members) {
            std::string ctype = m_ctx.mapType(member.type);
            std::string upperMember = m_ctx.normalizeIdent(member.name);
            std::string init = member.initialValue ? "{" + m_body.genExpr(*member.initialValue, member.type.base) + "}" : "{}";
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
               std::string typeName = m_ctx.normalizeType(analyzedType.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isComplex = true;
               }
            }
            // Also check direct FB type (non-array)
            else if (d.type.base == BaseType::NAMED && !isComplex) {
               std::string typeName = m_ctx.normalizeType(d.type.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Complex globals get extern declaration here
            if (isComplex) {
               std::string ctype = m_ctx.mapType(d.type);
               std::string upperName = m_ctx.normalizeIdent(d.name);
               // Register in global scope (already done in generateModular)
               out << "extern " << ctype << " " << upperName << ";\n";
            }
         }
      }
      out << "\n";
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateGVLsSource(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#include \"GVLs.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Define complex global variables (FB types or structs with FB)
   if (!tu.globals.empty()) {
      out << "// GLOBAL VARIABLE DEFINITIONS\n";
      for (const auto& sec : tu.globals) {
         for (const auto& d : sec.decls) {
            // Check if this global involves a FB type
            bool isComplex = false;
            if (d.type.base == BaseType::NAMED) {
               std::string typeName = m_ctx.normalizeType(d.type.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Also check array of FB
            if (!isComplex && !d.type.arrayDims.empty() && d.type.base == BaseType::NAMED) {
               std::string typeName = m_ctx.normalizeType(d.type.name);
               if (m_ctx.m_isFB.find(typeName) != m_ctx.m_isFB.end()) {
                  isComplex = true;
               }
            }

            // Complex globals get definition here
            if (isComplex) {
               std::string ctype = m_ctx.mapType(d.type);
               std::string upperName = m_ctx.normalizeIdent(d.name);
               if (d.initialValue) {
                  std::string initStr = m_body.generateOrderedStructInit(d.type, d.initialValue);
                  out << ctype << " " << upperName << " = " << initStr << ";\n";
               } else {
                  out << ctype << " " << upperName << "{};\n";
               }
            }
         }
      }
      out << "\n";
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFunctionBlocksMaster(const std::vector<std::string>& fbNames)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";
   out << "#include \"SimpleGVLs.hpp\"\n\n";

   // Include all FB headers
   for (const auto& fbName : fbNames) {
      out << "#include \"FunctionBlocks/" << fbName << ".hpp\"\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFBHeader(const POU& pou, const std::unordered_set<std::string>& dependencies)
{
   std::ostringstream out;
   std::string fbName = m_ctx.normalizeType(pou.name);

   // Header prologue
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";

   if (!m_ctx.libraryIncludeLines().empty()) {
      out << m_ctx.libraryIncludeBlock();
      out << "\n";
   }

   // Store the base class for SUPER^ calls
   std::string baseClass = pou.extends.empty() ? "" : m_ctx.normalizeType(pou.extends);
   m_ctx.m_currentFBBase = baseClass;

   // Check if this FB uses AT addresses
   m_ctx.m_hasAddresses = false;
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            m_ctx.m_hasAddresses = true;
            break;
         }
      }
      if (m_ctx.m_hasAddresses) {
         break;
      }
   }
   if (m_ctx.m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   // Include core headers
   out << "#include \"SimpleGVLs.hpp\"\n\n";

   // Include dependencies (other FBs used as members)
   for (const auto& dep : dependencies) {
      out << "#include \"FunctionBlocks/" << dep << ".hpp\"\n";
   }

   if (!dependencies.empty()) {
      out << "\n";
   }

   // Open namespace
   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // FB struct declaration
   out << "// FUNCTION BLOCK " << fbName << "\n";

   if (!pou.extends.empty()) {
      out << "// Extends: " << m_ctx.normalizeType(pou.extends) << "\n";
   }
   for (const auto& iface : pou.implements) {
      out << "// Implements: " << m_ctx.normalizeType(iface) << "\n";
   }

   out << "struct " << fbName;

   // Base struct (EXTENDS)
   if (!pou.extends.empty()) {
      out << " : public " << m_ctx.normalizeType(pou.extends);
   }

   // Interfaces (IMPLEMENTS)
   for (size_t i = 0; i < pou.implements.size(); ++i) {
      if (i == 0 && pou.extends.empty()) {
         out << " : public " << m_ctx.normalizeType(pou.implements[i]);
      } else {
         out << ", public " << m_ctx.normalizeType(pou.implements[i]);
      }
   }

   out << " {\n";
   out << "public:\n";

   // Variables (VAR_INPUT, VAR_OUTPUT, VAR_IN_OUT, VAR_EXTERNAL, VAR_GLOBAL)
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
         // Skip AT variables - they are handled separately with getter/setter
         if (!d.atAddress.empty()) {
            continue;
         }
         std::string ctype = m_ctx.mapType(d.type);
         std::string varName = m_ctx.normalizeIdent(d.name);
         std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";

         if (sec.kind == VarKind::IN_OUT) {
            out << "    VAR_INOUT<" << ctype << "> " << varName << init << ";\n";
         } else {
            out << "    " << m_decl.memberDecl(d) << ";\n";
         }
      }
   }

   // Standard Setters/Getters
   out << "\n    // SETTER/GETTER\n";
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string ctype = m_ctx.mapType(d.type);
         std::string varName = m_ctx.normalizeIdent(d.name);
         if (sec.kind == VarKind::IN_OUT) {
            out << "    inline void set_" << varName << "(" << ctype << "& refVal) { " << varName << " = refVal; }\n";
         } else if (sec.kind == VarKind::INPUT) {
            out << "    inline void set_" << varName << "(" << ctype << " val) { " << varName << " = val; }\n";
         } else if (sec.kind == VarKind::OUTPUT) {
            out << "    inline " << ctype << " get_" << varName << "() const { return " << varName << "; }\n";
         }
      }
   }
   out << "    // End SETTER/GETTER\n\n";

   // AT Address Setters/Getters
   bool hasAT = false;
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            hasAT = true;
            break;
         }
      }
      if (hasAT) {
         break;
      }
   }

   if (hasAT) {
      out << "    // AT GETTER/SETTER\n";
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string ctype = m_ctx.mapType(d.type);
               AddressExpr addr;
               std::string key = fbName + "::" + varName;
               auto it = m_ctx.m_resolvedATAddresses.find(key);
               if (it != m_ctx.m_resolvedATAddresses.end()) {
                  addr = it->second;
               } else if (parseAddressString(d.atAddress, addr)) {
                  // fixed address
               } else {
                  continue;
               }
               out << "    inline " << ctype << " getPi_" << varName << "() const {\n";
               out << "        return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
               out << "    }\n";
               out << "    inline void setPi_" << varName << "(" << ctype << " value) {\n";
               out << "        " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
               out << "    }\n";
            }
         }
      }
      out << "    // End AT GETTER/SETTER\n\n";
   }

   // Method declarations grouped by visibility

   // Collect methods by visibility
   std::vector<const Method*> publicMethods;
   std::vector<const Method*> protectedMethods;
   std::vector<const Method*> privateMethods;

   for (const auto& method : pou.methods) {
      switch (method.visibility) {
      case MethodVisibility::PUBLIC:
         publicMethods.push_back(&method);
         break;
      case MethodVisibility::PROTECTED:
         protectedMethods.push_back(&method);
         break;
      case MethodVisibility::PRIVATE:
         privateMethods.push_back(&method);
         break;
      default:
         // Default to PUBLIC if visibility is not set
         publicMethods.push_back(&method);
         break;
      }
   }

   /*
    * Emit a method declaration to the output stream.
    * This lambda generates a single method declaration with proper
    * virtual specifiers, parameters, and override/final/abstract flags.
    */
   auto emitMethodDecl = [&](const Method* method) {
      std::string methodName = m_ctx.normalizeIdent(method->name);
      std::string retType = "void";
      if (!m_ctx.isVoidType(method->returnType)) {
         retType = m_ctx.mapType(method->returnType);
      }

      out << "    virtual " << retType << " " << methodName << "(";
      bool first = true;
      for (const auto& param : method->parameters) {
         if (!first) {
            out << ", ";
         }
         first = false;
         std::string type = m_ctx.mapType(param.type);
         if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
            out << type << "& " << m_ctx.normalizeIdent(param.name);
         } else {
            out << type << " " << m_ctx.normalizeIdent(param.name);
         }
      }
      out << ")";
      if (method->isOverride) {
         out << " override";
      }
      if (method->isFinal) {
         out << " final";
      }
      if (method->isAbstract) {
         out << " = 0";
      }
      out << ";\n";
   };

   /*
    * Emit a visibility section with the given label and methods.
    * This lambda generates a visibility section (public/private/protected)
    * and emits all methods belonging to that visibility group.
    */
   auto emitVisibilitySection = [&](const std::string& visibilityLabel, const std::vector<const Method*>& methods) {
      if (methods.empty()) {
         return;
      }
      out << visibilityLabel << ":\n";
      for (const auto* method : methods) {
         emitMethodDecl(method);
      }
   };

   // Emit PRIVATE methods first (if any)
   emitVisibilitySection("private", privateMethods);

   // Emit PROTECTED methods (if any)
   emitVisibilitySection("protected", protectedMethods);

   // Emit PUBLIC methods (always present)
   out << "public:\n";
   out << "    " << fbName << "();\n";
   out << "    void operator()();\n";
   for (const auto* method : publicMethods) {
      emitMethodDecl(method);
   }
   out << "    virtual ~" << fbName << "() = default;\n";
   out << "};\n\n";

   // Epilogue
   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFunctionsHeader(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";

   if (!m_ctx.libraryIncludeLines().empty()) {
      out << m_ctx.libraryIncludeBlock();
      out << "\n";
   }

   m_ctx.m_hasAddresses = false;
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               if (!d.atAddress.empty()) {
                  m_ctx.m_hasAddresses = true;
                  break;
               }
            }
            if (m_ctx.m_hasAddresses) {
               break;
            }
         }
      }
      if (m_ctx.m_hasAddresses) {
         break;
      }
   }
   if (m_ctx.m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"GVLs.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Generate function declarations
   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         m_decl.emitFunctionDecl(pou, out);
         out << ";\n";
      }
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFunctionsSource(const TranslationUnit& tu)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#include \"Functions.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION) {
         std::string upperName = m_ctx.normalizeIdent(pou.name);
         bool hasReturn = !m_ctx.isVoidType(pou.returnType);

         // Push scope for this function
         m_ctx.m_scope.pushScope();
         m_ctx.m_scope.setFunctionScope(true);
         m_ctx.m_scope.setLocalToFunction(true);

         // Register function parameters and local variables in scope
         for (const auto& sec : pou.varSections) {
            for (const auto& d : sec.decls) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               m_ctx.m_scope.addVariable(varName, m_ctx.mapType(d.type));
               if (!d.atAddress.empty()) {
                  m_ctx.m_scope.addATVariable(varName, d.atAddress);
               }
            }
         }

         // Set current function name for AT resolution in m_body.genExpr()
         std::string savedFunctionName = m_ctx.m_currentFunctionName;
         m_ctx.m_currentFunctionName = upperName;

         m_decl.emitFunctionDecl(pou, out);
         out << " {\n";

         if (hasReturn) {
            out << "    " << m_ctx.mapType(pou.returnType) << " " << upperName << "_ret{};\n";
         }

         // Local variables: skip those with AT address
         for (const auto& sec : pou.varSections) {
            if (sec.kind == VarKind::VAR || sec.kind == VarKind::TEMP) {
               for (const auto& d : sec.decls) {
                  if (!d.atAddress.empty()) {
                     continue; // skip AT variables
                  }
                  out << "    " << m_decl.memberDecl(d) << ";\n";
               }
            }
         }

         // Generate function body (uses m_ctx.m_currentFunctionName)
         std::string body = generateFunctionBody(pou);
         out << body;

         if (hasReturn) {
            out << "    return " << upperName << "_ret;\n";
         }

         out << "}\n\n";

         // Restore previous state
         m_ctx.m_currentFunctionName = savedFunctionName;
         m_ctx.m_scope.setFunctionScope(false);
         m_ctx.m_scope.setLocalToFunction(false);
         m_ctx.m_scope.popScope();
      }
   }

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFBSource(const POU& pou)
{
   std::ostringstream out;
   std::string fbName = m_ctx.normalizeType(pou.name);

   std::string baseClass = pou.extends.empty() ? "" : m_ctx.normalizeType(pou.extends);
   m_ctx.m_currentFBBase = baseClass;

   // Variables are already registered in the scope by the caller.

   out << m_decl.generateHeaderComment();
   out << "#include \"FunctionBlocks/" << fbName << ".hpp\"\n";
   out << "#include \"Functions.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // Constructor
   out << fbName << "::" << fbName << "() {}\n\n";

   // operator()
   out << "void " << fbName << "::operator()() {\n";
   std::string opBody = generateFBOperatorBody(pou);
   out << opBody;
   out << "}\n\n";

   // Generate method definitions - redirect m_ctx.m_src to a local stream
   std::ostringstream originalSrc;
   std::swap(m_ctx.m_src, originalSrc);
   std::ostringstream methodStream;
   m_ctx.m_src = std::move(methodStream);

   // Base Function blocks for calls with SUPER^
   // The base class is stored in the scope manager, we don't need to store it separately.

   // Methods
   for (const auto& method : pou.methods) {
      // Each method will push its own scope in genMethodDefinition
      // We'll call genMethodDefinition which handles scope push/pop
      // But we need to pass the fbName and the method.
      // We'll use a separate function to generate the method definition.
      // Since genMethodDefinition is private, we can call it here.
      // However, we need to ensure that the scope manager is properly set up.
      // The caller (generateModular) already pushed a scope for the FB and registered variables.
      // For method definitions, we need to push a new scope inside genMethodDefinition.
      // So we can just call genMethodDefinition.
      m_decl.genMethodDefinition(fbName, method);
   }

   std::string methodsBody = m_ctx.m_src.str();
   out << methodsBody;
   std::swap(m_ctx.m_src, originalSrc);

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   m_ctx.m_currentFBBase = "";
   return out.str();
}

std::string ProjectEmitter::generateProgramsMaster(const std::vector<std::string>& progNames)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";
   out << "#include \"FunctionBlocks.hpp\"\n\n";

   // Include all program headers
   for (const auto& progName : progNames) {
      out << "#include \"Programs/" << progName << ".hpp\"\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateProgramHeader(const POU& pou)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   std::string progName = m_ctx.normalizeType(pou.name);

   out << "#pragma once\n";
   out << "#define ST2CPP_RUNTIME_NAMESPACE " << m_ctx.m_namespace << "\n";
   out << "#include \"" << m_ctx.m_runtimeHeader << "\"\n";

   if (!m_ctx.libraryIncludeLines().empty()) {
      out << m_ctx.libraryIncludeBlock();
      out << "\n";
   }

   if (m_ctx.m_hasAddresses) {
      out << "#include \"ProcessImage.hpp\"\n";
   }

   out << "#include \"FunctionBlocks.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   out << "// PROGRAM " << progName << "\n";
   out << "struct " << progName << " {\n";

   // Member variables - only non-AT variables
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string varName = m_ctx.normalizeIdent(d.name);
         std::string varType = m_ctx.mapType(d.type);

         // Register as local variable (scope already pushed by caller)
         // No need to register here, caller did it.

         if (d.atAddress.empty()) {
            out << "    " << m_decl.memberDecl(d) << ";\n";
         }
      }
   }

   // Generate getter/setter for AT addresses
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            std::string varName = m_ctx.normalizeIdent(d.name);
            std::string ctype = m_ctx.mapType(d.type);

            AddressExpr addr;
            // Try to get resolved address
            std::string key = progName + "::" + varName;
            auto it = m_ctx.m_resolvedATAddresses.find(key);
            if (it != m_ctx.m_resolvedATAddresses.end()) {
               addr = it->second;
            } else if (parseAddressString(d.atAddress, addr)) {
               // Use original (fixed)
            } else {
               continue;
            }

            // Generate getter
            out << "    // AT " << d.atAddress << "\n";
            out << "    inline " << ctype << " getPi_" << varName << "() const {\n";
            out << "        return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
            out << "    }\n";
            // Generate setter
            out << "    inline void setPi_" << varName << "(" << ctype << " value) {\n";
            out << "        " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
            out << "    }\n";
         }
      }
   }

   out << "\n";
   out << "    void run();\n";
   out << "};\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateProgramSource(const POU& pou)
{
   std::ostringstream out;
   out << m_decl.generateHeaderComment();
   std::string progName = m_ctx.normalizeType(pou.name);

   out << "#include \"Programs/" << progName << ".hpp\"\n";
   out << "#include \"Functions.hpp\"\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "namespace " << m_ctx.m_namespace << " {\n\n";
   }

   // run() method
   out << "void " << progName << "::run() {\n";

   // Generate body using generateProgramBody (which uses the scope already set)
   std::string body = generateProgramBody(pou);
   out << body;
   out << "}\n\n";

   if (!m_ctx.m_namespace.empty()) {
      out << "} // namespace " << m_ctx.m_namespace << "\n";
   }

   return out.str();
}

std::string ProjectEmitter::generateFunctionBody(const POU& pou)
{
   std::ostringstream out;
   std::string upperName = m_ctx.normalizeIdent(pou.name);

   // Save return type
   bool hasReturn = !m_ctx.isVoidType(pou.returnType);
   std::string savedReturnType = m_ctx.m_currentFunctionReturnType;
   m_ctx.m_currentFunctionReturnType = hasReturn ? m_ctx.mapType(pou.returnType) : "";

   // Save original stream and create new one
   std::ostringstream originalSrc;
   std::swap(m_ctx.m_src, originalSrc);

   // Create a new stream for capturing output
   std::ostringstream captureStream;
   m_ctx.m_src = std::move(captureStream);

   // Reset indent for body generation (starts at 1 because function body is indented)
   int savedIndent = m_ctx.m_indent;
   m_ctx.m_indent = 1; // Function body starts with one level of indentation

   // Generate body using existing m_body.genStmt(now writes to m_ctx.m_src)
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }

   // Restore indent
   m_ctx.m_indent = savedIndent;

   // Capture the generated code
   std::string body = m_ctx.m_src.str();

   // Restore original stream
   std::swap(m_ctx.m_src, originalSrc);

   m_ctx.m_currentFunctionReturnType = savedReturnType;

   return body;
}

std::string ProjectEmitter::generateFBOperatorBody(const POU& pou)
{
   std::ostringstream out;

   // Save original stream and create new one
   std::ostringstream originalSrc;
   std::swap(m_ctx.m_src, originalSrc);

   std::ostringstream captureStream;
   m_ctx.m_src = std::move(captureStream);

   // Reset indent for body generation
   int savedIndent = m_ctx.m_indent;
   m_ctx.m_indent = 1; // operator() body starts with one level of indentation

   // Generate VAR_TEMP local variables
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            std::string ctype = m_ctx.mapType(d.type);
            std::string varName = m_ctx.normalizeIdent(d.name);
            std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";
            out << m_ctx.ind() << ctype << " " << varName << init << ";\n";
         }
      }
   }

   // Generate body statements
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }

   // Restore indent
   m_ctx.m_indent = savedIndent;

   // Capture the generated code
   std::string body = m_ctx.m_src.str();

   // Restore original stream
   std::swap(m_ctx.m_src, originalSrc);

   // Combine VAR_TEMP declarations and body
   return out.str() + body;
}

std::string ProjectEmitter::generateProgramBody(const POU& pou)
{
   std::ostringstream out;

   // The scope is already pushed and variables registered by the caller.
   // We just need to generate the body statements.

   // Save the original stream
   std::ostringstream originalSrc;
   std::swap(m_ctx.m_src, originalSrc);

   // Create a new stream to save the output
   std::ostringstream captureStream;
   m_ctx.m_src = std::move(captureStream);

   // Right indent (level 1 for the method body)
   int savedIndent = m_ctx.m_indent;
   m_ctx.m_indent = 1;

   // Generate VAR_TEMP locals
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            if (d.atAddress.empty()) {
               std::string ctype = m_ctx.mapType(d.type);
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";
               m_ctx.m_src << m_ctx.ind() << ctype << " " << varName << init << ";\n";
            }
         }
      }
   }

   // Generate body by using genStmt
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }

   // Restore indent
   m_ctx.m_indent = savedIndent;

   // Save generated code
   std::string body = m_ctx.m_src.str();

   // Restore the original stream
   std::swap(m_ctx.m_src, originalSrc);

   return body;
}

} // namespace st2cpp::codegen
