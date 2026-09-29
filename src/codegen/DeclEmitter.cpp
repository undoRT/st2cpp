/**
 * @file DeclEmitter.cpp
 * @brief Declaration emission: POUs, structs, enums, globals and methods
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/DeclEmitter.h"
#include "semantic/IecTime.h"
#include <algorithm>
#include <queue>


namespace st2cpp::codegen {


void DeclEmitter::collectSignature(const POU& pou)
{
   FunctionSignature sig;
   sig.name = m_ctx.normalizeIdent(pou.name);

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
            sig.parameters.push_back({m_ctx.normalizeIdent(d.name), d.type, true, d.initialValue, false});
         }
      } else if (sec.kind == VarKind::OUTPUT) {
         // Output parameters: passed by reference
         for (const auto& d : sec.decls) {
            TypeRef refType = d.type;
            refType.isRefTo = true;
            sig.parameters.push_back({m_ctx.normalizeIdent(d.name), refType, false, d.initialValue, true});
         }
      } else if (sec.kind == VarKind::IN_OUT) {
         // In-out parameters: passed by reference, required (no default)
         for (const auto& d : sec.decls) {
            sig.parameters.push_back({m_ctx.normalizeIdent(d.name), d.type, false, nullptr, false});
         }
      }
   }

   m_ctx.m_signatures[sig.name] = sig;

   // For FUNCTION_BLOCK, collect method signatures as well
   if (pou.kind == POUKind::FUNCTION_BLOCK) {
      for (const auto& method : pou.methods) {
         FunctionSignature methodSig;
         methodSig.name = m_ctx.normalizeIdent(pou.name) + "::" + m_ctx.normalizeIdent(method.name);
         methodSig.returnType = method.returnType;

         // Collect method parameters
         for (const auto& param : method.parameters) {
            bool isOutputVar = (param.kind == VarKind::OUTPUT);
            methodSig.parameters.push_back(
               {m_ctx.normalizeIdent(param.name), param.type, param.kind == VarKind::INPUT, param.initialValue, isOutputVar});
         }

         m_ctx.m_methodSignatures[methodSig.name] = methodSig;
      }
   }
}

void DeclEmitter::genPOU(const POU& pou)
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

void DeclEmitter::genFunctionBlock(const POU& pou)
{
   std::string upperName = m_ctx.normalizeType(pou.name);

   std::string baseClass = pou.extends.empty() ? "" : m_ctx.normalizeType(pou.extends);
   m_ctx.m_currentFBBase = baseClass;

   // ========== HEADER GENERATION ==========
   m_ctx.m_hdr << "// FUNCTION BLOCK " << upperName << "\n";
   m_ctx.m_hdr << "struct " << upperName;

   // Base struct (EXTENDS)
   if (!pou.extends.empty()) {
      m_ctx.m_hdr << " : public " << m_ctx.normalizeType(pou.extends);
   }

   // Interfacce (IMPLEMENTS)
   for (size_t i = 0; i < pou.implements.size(); ++i) {
      if (i == 0 && pou.extends.empty()) {
         m_ctx.m_hdr << " : public " << m_ctx.normalizeType(pou.implements[i]);
      } else {
         m_ctx.m_hdr << ", public " << m_ctx.normalizeType(pou.implements[i]);
      }
   }

   m_ctx.m_hdr << " {\n";
   m_ctx.m_hdr << "public:\n";
   m_ctx.push();

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
      m_ctx.m_hdr << m_ctx.ind() << secLabel << "\n";

      for (const auto& d : sec.decls) {
         // Skip AT address variables - they are handled with getter/setter
         if (!d.atAddress.empty()) {
            continue;
         }
         std::string ctype = m_ctx.mapType(d.type);
         std::string upperName_inst = m_ctx.normalizeIdent(d.name);
         std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";

         if (sec.kind == VarKind::IN_OUT) {
            // IN_OUT variables are wrapped in VAR_INOUT template
            m_ctx.m_hdr << m_ctx.ind() << "VAR_INOUT<" << ctype << "> " << upperName_inst << init << ";\n";
         } else {
            m_ctx.m_hdr << m_ctx.ind() << memberDecl(d) << ";\n";
         }
      }
   }

   // Generate setter/getter methods for each variable
   m_ctx.m_hdr << "\n";
   m_ctx.m_hdr << "// SETTER/GETTER\n";
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         std::string ctype = m_ctx.mapType(d.type);
         std::string upperName_inst = m_ctx.normalizeIdent(d.name);
         if (sec.kind == VarKind::IN_OUT) {
            m_ctx.m_hdr << m_ctx.ind() << "inline void set_" << upperName_inst << "(" << ctype << "& refVal) { " << upperName_inst
                  << " = refVal; }\n";
         } else if (sec.kind == VarKind::INPUT) {
            m_ctx.m_hdr << m_ctx.ind() << "inline void set_" << upperName_inst << "(" << ctype << " val) { " << upperName_inst << " = val; }\n";
         } else if (sec.kind == VarKind::OUTPUT) {
            m_ctx.m_hdr << m_ctx.ind() << "inline " << ctype << " get_" << upperName_inst << "() const { return " << upperName_inst << "; }\n";
         }
      }
   }
   m_ctx.m_hdr << "// End SETTER/GETTER\n\n";

   // Generate AT getter/setter for variables with AT address
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
      m_ctx.m_hdr << "// AT GETTER/SETTER\n";
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               std::string varName = m_ctx.normalizeIdent(d.name);
               std::string ctype = m_ctx.mapType(d.type);
               AddressExpr addr;
               std::string key = upperName + "::" + varName;
               auto it = m_ctx.m_resolvedATAddresses.find(key);
               if (it != m_ctx.m_resolvedATAddresses.end()) {
                  addr = it->second;
               } else if (parseAddressString(d.atAddress, addr)) {
                  // fixed address
               } else {
                  continue;
               }
               m_ctx.m_hdr << m_ctx.ind() << "inline " << ctype << " getPi_" << varName << "() const {\n";
               m_ctx.m_hdr << m_ctx.ind() << "    return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
               m_ctx.m_hdr << m_ctx.ind() << "}\n";
               m_ctx.m_hdr << m_ctx.ind() << "inline void setPi_" << varName << "(" << ctype << " value) {\n";
               m_ctx.m_hdr << m_ctx.ind() << "    " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
               m_ctx.m_hdr << m_ctx.ind() << "}\n";
            }
         }
      }
      m_ctx.m_hdr << "// End AT GETTER/SETTER\n\n";
   }

   // Constructor, execution operator, and method declarations
   m_ctx.m_hdr << m_ctx.ind() << upperName << "();\n";
   m_ctx.m_hdr << m_ctx.ind() << "void operator()();\n";

   // Collect all the methods visbility
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
      }
   }

   // Generate private methods (if present)
   if (!privateMethods.empty()) {
      m_ctx.m_hdr << "\nprivate:\n";
      for (const auto* method : privateMethods) {
         genMethodDeclaration(*method);
      }
   }

   // Generate protected methods (if present)
   if (!protectedMethods.empty()) {
      m_ctx.m_hdr << "protected:\n";
      for (const auto* method : protectedMethods) {
         genMethodDeclaration(*method);
      }
   }

   // Generate public methods (if present)
   m_ctx.m_hdr << "public:\n";
   for (const auto* method : publicMethods) {
      genMethodDeclaration(*method);
   }

   m_ctx.m_hdr << m_ctx.ind() << "virtual ~" << upperName << "() = default;\n";
   m_ctx.pop();
   m_ctx.m_hdr << "};\n\n";

   // ========== SOURCE GENERATION ==========
   // Constructor implementation
   m_ctx.m_src << "// FUNCTION BLOCK " << upperName << "\n";
   m_ctx.m_src << upperName << "::" << upperName << "() {}\n\n";

   // operator() implementation
   m_ctx.m_src << "void " << upperName << "::operator()() {\n";
   m_ctx.push();

   // Emit VAR_TEMP as local variables within operator()
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               continue;
            }
            std::string ctype = m_ctx.mapType(d.type);
            std::string upperName_inst = m_ctx.normalizeIdent(d.name);
            std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";
            m_ctx.m_src << m_ctx.ind() << ctype << " " << upperName_inst << init << ";\n";
         }
      }
   }

   // Generate the FB body statements
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }
   m_ctx.pop();
   m_ctx.m_src << "}\n";

   // Generate method definitions
   for (const auto& method : pou.methods) {
      genMethodDefinition(upperName, method);
   }
   m_ctx.m_currentFBBase = "";
   m_ctx.m_src << "// END FUNCTION BLOCK " << upperName << "\n\n";
}

void DeclEmitter::emitFunctionDecl(const POU& pou, std::ostringstream& out)
{
   std::string retType = "void";
   if (!m_ctx.isVoidType(pou.returnType)) {
      retType = m_ctx.mapType(pou.returnType);
   }

   out << retType << " " << m_ctx.normalizeIdent(pou.name) << "(";
   bool first = true;

   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::INPUT) {
         // Input parameters: pass by value
         for (const auto& d : sec.decls) {
            if (!first) {
               out << ", ";
            }
            first = false;
            out << m_ctx.mapType(d.type) << " " << m_ctx.normalizeIdent(d.name);
         }
      } else if (sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
         // Output and In-Out parameters: pass by reference
         for (const auto& d : sec.decls) {
            if (!first) {
               out << ", ";
            }
            first = false;
            out << m_ctx.mapType(d.type) << "& " << m_ctx.normalizeIdent(d.name);
         }
      }
   }
   out << ")";
}

void DeclEmitter::genFunction(const POU& pou)
{
   std::string upperName = m_ctx.normalizeIdent(pou.name);
   m_ctx.m_currentFunctionName = upperName; // Track for _ret translation

   // Save return type
   bool hasReturn = !m_ctx.isVoidType(pou.returnType);
   m_ctx.m_currentFunctionReturnType = hasReturn ? m_ctx.mapType(pou.returnType) : "";

   // Header declaration
   m_ctx.m_hdr << "// FUNCTION " << upperName << "\n";
   emitFunctionDecl(pou, m_ctx.m_hdr);
   m_ctx.m_hdr << ";\n\n";

   // Source definition
   m_ctx.m_src << "// FUNCTION " << upperName << "\n";
   emitFunctionDecl(pou, m_ctx.m_src);
   m_ctx.m_src << " {\n";
   m_ctx.push();

   // Return variable (IEC 61131-3: function name acts as return variable)
   if (hasReturn) {
      m_ctx.m_src << m_ctx.ind() << m_ctx.mapType(pou.returnType) << " " << upperName << "_ret{};\n";
   }

   // Local variables from VAR and VAR_TEMP sections
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::VAR || sec.kind == VarKind::TEMP) {
         for (const auto& d : sec.decls) {
            if (!d.atAddress.empty()) {
               continue;
            }
            std::string ctype = m_ctx.mapType(d.type);
            std::string varName = m_ctx.normalizeIdent(d.name);
            std::string init = d.initialValue ? "{" + m_body.genExpr(*d.initialValue, d.type.base) + "}" : "{}";
            m_ctx.m_src << m_ctx.ind() << ctype << " " << varName << init << ";\n";
         }
      }
   }

   // Generate function body
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }

   m_ctx.m_currentFunctionName.clear();

   // Return the return variable
   if (hasReturn) {
      m_ctx.m_src << m_ctx.ind() << "return " << upperName << "_ret;\n";
   }

   // Clear return type
   m_ctx.m_currentFunctionReturnType.clear();

   m_ctx.pop();
   m_ctx.m_src << "}\n";
   m_ctx.m_src << "// END FUNCTION " << upperName << "\n\n";
}

void DeclEmitter::genProgram(const POU& pou)
{
   std::string upperName = m_ctx.normalizeType(pou.name);
   m_ctx.m_hdr << "// PROGRAM " << upperName << "\n";
   m_ctx.m_hdr << "struct " << upperName << " {\n";
   m_ctx.push();

   // Generate all member variables
   for (const auto& sec : pou.varSections) {
      for (const auto& d : sec.decls) {
         if (!d.atAddress.empty()) {
            std::string varName = m_ctx.normalizeIdent(d.name);
            std::string ctype = m_ctx.mapType(d.type);

            AddressExpr addr;
            // Try to get resolved address first
            std::string key = upperName + "::" + varName;
            auto it = m_ctx.m_resolvedATAddresses.find(key);
            if (it != m_ctx.m_resolvedATAddresses.end()) {
               addr = it->second;
            } else if (parseAddressString(d.atAddress, addr)) {
               // Use the original address (fixed)
            } else {
               // Fallback: generate error
               m_ctx.m_hdr << m_ctx.ind() << "// ERROR: Invalid AT address: " << d.atAddress << "\n";
               continue;
            }

            m_ctx.m_hdr << m_ctx.ind() << "// AT " << d.atAddress << "\n";
            // Getter (const and non-const)
            m_ctx.m_hdr << m_ctx.ind() << "inline " << ctype << " getPi_" << varName << "() const {\n";
            m_ctx.m_hdr << m_ctx.ind() << "    return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
            m_ctx.m_hdr << m_ctx.ind() << "}\n";
            // Setter
            m_ctx.m_hdr << m_ctx.ind() << "inline void setPi_" << varName << "(" << ctype << " value) {\n";
            m_ctx.m_hdr << m_ctx.ind() << "    " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
            m_ctx.m_hdr << m_ctx.ind() << "}\n";
         } else {
            m_ctx.m_hdr << m_ctx.ind() << memberDecl(d) << ";\n";
         }
      }
   }

   m_ctx.m_hdr << "\n";
   m_ctx.m_hdr << m_ctx.ind() << "void run();\n";
   m_ctx.pop();
   m_ctx.m_hdr << "};\n\n";

   // Source implementation of run() method
   m_ctx.m_src << "void " << upperName << "::run() {\n";
   m_ctx.push();
   for (const auto& stmt : pou.body) {
      m_body.genStmt(*stmt);
   }
   m_ctx.pop();
   m_ctx.m_src << "}\n\n";
}

void DeclEmitter::genStruct(const StructType& st)
{
   std::string upperName = m_ctx.normalizeType(st.name);
   m_ctx.m_hdr << "// STRUCT " << upperName << "\n";
   m_ctx.m_hdr << "struct " << upperName << " {\n";
   m_ctx.push();
   for (const auto& member : st.members) {
      std::string ctype = m_ctx.mapType(member.type);
      std::string upperMember = m_ctx.normalizeIdent(member.name);
      std::string init = member.initialValue ? "{" + m_body.genExpr(*member.initialValue, member.type.base) + "}" : "{}";
      m_ctx.m_hdr << m_ctx.ind() << ctype << " " << upperMember << init << ";\n";
   }
   m_ctx.pop();
   m_ctx.m_hdr << "};\n\n";
}

void DeclEmitter::genInterface(const Interface& iface)
{
   std::string name = m_ctx.normalizeType(iface.name);
   m_ctx.m_hdr << "// INTERFACE " << name << "\n";
   m_ctx.m_hdr << "struct " << name << " {\n";
   m_ctx.push();
   for (const auto& method : iface.methods) {
      genMethodDeclaration(method);
   }
   m_ctx.m_hdr << m_ctx.ind() << "virtual ~" << name << "() = default;\n";
   m_ctx.pop();
   m_ctx.m_hdr << "};\n\n";
}

void DeclEmitter::generateStructsInOrder(const std::vector<StructType>& structs, std::ostringstream* out)
{
   if (structs.empty()) {
      return;
   }
   // If out is nullptr, use m_ctx.m_hdr (FLAT mode) otherwise use out (MODULAR mode)
   // Build dependency graph
   auto structDeps = m_ctx.buildStructDependenciesForStructs(structs);

   // Topological sort
   std::vector<std::string> orderedStructNames;
   try {
      orderedStructNames = m_ctx.topologicalSortStructs(structDeps);
   } catch (const std::runtime_error& e) {
      // Circular dependency detected - fall back to original order
      for (const auto& st : structs) {
         if (out) {
            // Genera struct su out stream
            std::string upperName = m_ctx.normalizeType(st.name);
            *out << "// STRUCT " << upperName << "\n";
            *out << "struct " << upperName << " {\n";
            for (const auto& member : st.members) {
               std::string ctype = m_ctx.mapType(member.type);
               std::string upperMember = m_ctx.normalizeIdent(member.name);
               std::string init = member.initialValue ? "{" + m_body.genExpr(*member.initialValue, member.type.base) + "}" : "{}";
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
         if (m_ctx.normalizeType(st.name) == structName) {
            if (out) {
               // Genera struct su out stream
               std::string upperName = m_ctx.normalizeType(st.name);
               *out << "// STRUCT " << upperName << "\n";
               *out << "struct " << upperName << " {\n";
               for (const auto& member : st.members) {
                  std::string ctype = m_ctx.mapType(member.type);
                  std::string upperMember = m_ctx.normalizeIdent(member.name);
                  std::string init = member.initialValue ? "{" + m_body.genExpr(*member.initialValue, member.type.base) + "}" : "{}";
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

void DeclEmitter::genEnum(const EnumType& et)
{
   std::string upperName = m_ctx.normalizeType(et.name);
   // Register enum type for special handling in expression generation
   m_ctx.m_enumTypes[upperName] = true;

   // Register each enumerator with its enum name for qualified access
   for (const auto& enumerator : et.enumerators) {
      std::string upperEnumerator = m_ctx.normalizeIdent(enumerator.name);
      m_ctx.m_enumValues[upperName].insert(upperEnumerator);
      m_ctx.m_enumeratorToEnum[upperEnumerator] = upperName;
   }

   m_ctx.m_hdr << "// ENUM " << upperName << "\n";
   m_ctx.m_hdr << "enum class " << upperName << " : int {\n";
   m_ctx.push();
   for (size_t i = 0; i < et.enumerators.size(); ++i) {
      const auto& enumerator = et.enumerators[i];
      std::string upperEnumerator = m_ctx.normalizeIdent(enumerator.name);
      m_ctx.m_hdr << m_ctx.ind() << upperEnumerator;

      // Emit explicit value if present
      if (enumerator.value) {
         m_ctx.m_hdr << " = " << m_body.genExpr(*enumerator.value);
      }

      // Add comma for all but last enumerator
      if (i < et.enumerators.size() - 1) {
         m_ctx.m_hdr << ",";
      }
      m_ctx.m_hdr << "\n";
   }
   m_ctx.pop();
   m_ctx.m_hdr << "};\n";
   m_ctx.m_hdr << "// END ENUM " << upperName << "\n\n";
}

void DeclEmitter::genGlobals(const std::vector<VarSection>& globals)
{
   if (globals.empty()) {
      return;
   }

   m_ctx.m_hdr << "// GLOBAL VARIABLES\n";

   for (const auto& sec : globals) {
      for (const auto& d : sec.decls) {
         std::string ctype = m_ctx.mapType(d.type);
         std::string upperName = m_ctx.normalizeIdent(d.name);

         // Global AT address variable - generate getter and setter
         if (!d.atAddress.empty()) {
            AddressExpr addr;
            // Try to get resolved address first
            auto it = m_ctx.m_resolvedATAddresses.find(upperName);
            if (it != m_ctx.m_resolvedATAddresses.end()) {
               addr = it->second;
            } else if (parseAddressString(d.atAddress, addr)) {
               // Use the original address (fixed)
            } else {
               // Fallback: generate error comment
               m_ctx.m_hdr << "// ERROR: Invalid AT address: " << d.atAddress << "\n";
               continue;
            }

            // Register as AT variable in global scope
            m_ctx.m_scope.addATVariable(upperName, d.atAddress);

            // Getter
            m_ctx.m_hdr << "// AT " << d.atAddress << "\n";
            m_ctx.m_hdr << "inline " << ctype << " getPi_" << upperName << "() {\n";
            m_ctx.m_hdr << "    return " << m_body.generateAddressAccess(addr, &d.type) << ";\n";
            m_ctx.m_hdr << "}\n";
            // Setter
            m_ctx.m_hdr << "inline void setPi_" << upperName << "(" << ctype << " value) {\n";
            m_ctx.m_hdr << "    " << m_body.generateAddressWrite(addr, "value", &d.type) << ";\n";
            m_ctx.m_hdr << "}\n";
            continue;
         }

         // Normal global variable
         if (!d.type.arrayDims.empty()) {
            // Array global - STArray type already includes bounds
            std::string arrayDecl = ctype + " " + upperName;

            if (d.initialValue) {
               m_ctx.m_hdr << "inline " << arrayDecl << " = " << m_body.genExpr(*d.initialValue, d.type.base) << ";\n";
            } else {
               m_ctx.m_hdr << "inline " << arrayDecl << "{};\n";
            }
         } else {
            // Scalar global
            if (d.initialValue) {
               std::string initStr = m_body.generateOrderedStructInit(d.type, d.initialValue);
               m_ctx.m_hdr << "inline " << ctype << " " << upperName << " = " << initStr << ";\n";
            } else {
               m_ctx.m_hdr << "inline " << ctype << " " << upperName << "{};\n";
            }
         }
      }
   }
}

void DeclEmitter::genMethodDeclaration(const Method& method)
{
   std::string upperMethodName = m_ctx.normalizeIdent(method.name);
   std::string returnType = "void";

   if (!m_ctx.isVoidType(method.returnType)) {
      returnType = m_ctx.mapType(method.returnType);
   }

   std::string visibility;
   switch (method.visibility) {
   case MethodVisibility::PRIVATE:
      visibility = "private";
      break;
   case MethodVisibility::PROTECTED:
      visibility = "protected";
      break;
   case MethodVisibility::PUBLIC:
   default:
      visibility = "public";
      break;
   }

   m_ctx.m_hdr << m_ctx.ind() << "virtual " << returnType << " " << upperMethodName << "(";

   bool first = true;

   for (const auto& param : method.parameters) {
      if (!first) {
         m_ctx.m_hdr << ", ";
      }
      first = false;

      std::string type = m_ctx.mapType(param.type);

      if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
         m_ctx.m_hdr << type << "& " << m_ctx.normalizeIdent(param.name);
      } else {
         m_ctx.m_hdr << type << " " << m_ctx.normalizeIdent(param.name);
      }
   }

   m_ctx.m_hdr << ")";

   // If present, override
   if (method.isOverride) {
      m_ctx.m_hdr << " override";
   }

   // If present, final
   if (method.isFinal) {
      m_ctx.m_hdr << " final";
   }

   // If present, abstract
   if (method.isAbstract) {
      m_ctx.m_hdr << " = 0";
   }

   m_ctx.m_hdr << ";\n";
}

void DeclEmitter::genMethodDefinition(const std::string& fbName, const Method& method)
{
   // Get base class from parent scope (FB scope) BEFORE pushing method scope
   std::string baseClass = m_ctx.m_scope.getBaseClass();

   // Push a scope for this method
   m_ctx.m_scope.pushScope();
   m_ctx.m_scope.setFunctionScope(true); // method = function
   m_ctx.m_scope.setLocalToFunction(true);

   // Set base class in method scope for SUPER^ calls
   if (!baseClass.empty()) {
      m_ctx.m_scope.setBaseClass(baseClass);
   }

   // Register parameters and local variables in the method scope
   for (const auto& param : method.parameters) {
      std::string paramName = m_ctx.normalizeIdent(param.name);
      m_ctx.m_scope.addVariable(paramName, m_ctx.mapType(param.type));
      // No AT addresses for parameters
   }
   for (const auto& local : method.localVars) {
      // Skip local variables with AT address (they are not declared as local variables)
      if (!local.atAddress.empty()) {
         continue;
      }
      std::string varName = m_ctx.normalizeIdent(local.name);
      m_ctx.m_scope.addVariable(varName, m_ctx.mapType(local.type));
      // AT address for local variable is already registered separately
   }

   std::string upperMethodName = m_ctx.normalizeIdent(method.name);
   std::string returnType = "void";

   // Save the RETURN type if it is not void
   if (!m_ctx.isVoidType(method.returnType)) {
      returnType = m_ctx.mapType(method.returnType);
   }
   bool hasReturn = !m_ctx.isVoidType(method.returnType);
   m_ctx.m_currentFunctionReturnType = hasReturn ? returnType : "";

   // Method signature
   m_ctx.m_src << returnType << " " << fbName << "::" << upperMethodName << "(";
   bool first = true;

   for (const auto& param : method.parameters) {
      if (!first) {
         m_ctx.m_src << ", ";
      }
      first = false;

      std::string type = m_ctx.mapType(param.type);

      if (param.kind == VarKind::OUTPUT || param.kind == VarKind::IN_OUT) {
         m_ctx.m_src << type << "& " << m_ctx.normalizeIdent(param.name);
      } else {
         m_ctx.m_src << type << " " << m_ctx.normalizeIdent(param.name);
      }
   }

   m_ctx.m_src << ") {\n";
   m_ctx.push();

   // Return variable (if the method returns a value)
   if (hasReturn) {
      m_ctx.m_src << m_ctx.ind() << returnType << " " << upperMethodName << "_ret{};\n";
   }

   // Local variables within the method (only those without AT address)
   for (const auto& local : method.localVars) {
      if (!local.atAddress.empty()) {
         continue; // skip AT variables
      }
      m_ctx.m_src << m_ctx.ind() << memberDecl(local) << ";\n";
   }

   // Generate method body, tracking function name for _ret translation
   std::string savedFunctionName = m_ctx.m_currentFunctionName;
   m_ctx.m_currentFunctionName = upperMethodName;

   for (const auto& stmt : method.body) {
      m_body.genStmt(*stmt);
   }

   m_ctx.m_currentFunctionName = savedFunctionName;

   // Return statement if the method has a return value
   if (hasReturn) {
      m_ctx.m_src << m_ctx.ind() << "return " << upperMethodName << "_ret;\n";
   }

   // Clear return type
   m_ctx.m_currentFunctionReturnType.clear();

   m_ctx.pop();
   m_ctx.m_src << "}\n\n";

   // Pop the method scope
   m_ctx.m_scope.setLocalToFunction(false);
   m_ctx.m_scope.setFunctionScope(false);
   m_ctx.m_scope.popScope();
}

std::string DeclEmitter::memberDecl(const VarDecl& d)
{
   std::string ctype = m_ctx.mapType(d.type);
   std::string upperName = m_ctx.normalizeIdent(d.name);
   std::string init;

   // Handle array members
   if (!d.type.arrayDims.empty()) {
      std::string result = ctype + " " + upperName;
      if (d.initialValue) {
         init = " = " + m_body.genExpr(*d.initialValue, d.type.base);
      } else {
         init = "{}";
      }
      return result + init;
   }

   // Handle scalar members with initializers
   if (d.initialValue) {
      // If the type is a user-defined struct, reorder the initializer members
      // to match the struct's declaration order.
      if (d.type.base == BaseType::NAMED) {
         std::string typeName = m_ctx.normalizeType(d.type.name);
         // Check if this is a known struct type
         if (m_ctx.m_structTypes.find(typeName) != m_ctx.m_structTypes.end()) {
            // Try to cast the initializer to StructInitExpr
            if (auto* initExpr = std::get_if<StructInitExpr>(&d.initialValue->node)) {
               // Reorder members and update the AST node
               initExpr->members = m_ctx.orderStructMembers(initExpr->members, typeName);
            }
         }
      }

      // Generate the initializer expression string
      std::string initExpr = m_body.genExpr(*d.initialValue, d.type.base);

      // Avoid double braces: if the initializer is already a braced-init-list,
      // use it directly; otherwise wrap it in braces.
      if (!initExpr.empty() && initExpr.front() == '{' && initExpr.back() == '}') {
         init = " = " + initExpr;
      } else {
         init = "{" + initExpr + "}";
      }
   } else if (d.isConstant) {
      init = " = {}";
   } else {
      init = "{}";
   }

   return ctype + " " + upperName + init;
}

AddressExpr::AddressQualifier DeclEmitter::deduceQualifierFromType(const TypeRef& type) const
{
   TypeRef baseType = type;
   while (!baseType.arrayDims.empty()) {
      baseType.arrayDims.pop_back();
   }

   switch (baseType.base) {
   case BaseType::BOOL:
      return AddressExpr::AddressQualifier::BIT;
   case BaseType::BYTE:
   case BaseType::SINT:
   case BaseType::USINT:
      return AddressExpr::AddressQualifier::BYTE;
   case BaseType::WORD:
   case BaseType::INT:
   case BaseType::UINT:
      return AddressExpr::AddressQualifier::WORD;
   case BaseType::DWORD:
   case BaseType::DINT:
   case BaseType::UDINT:
   case BaseType::REAL:
   case BaseType::TIME:
      return AddressExpr::AddressQualifier::DWORD;
   case BaseType::LWORD:
   case BaseType::LINT:
   case BaseType::ULINT:
   case BaseType::LREAL:
   case BaseType::DT:
   case BaseType::TOD:
      return AddressExpr::AddressQualifier::LWORD;
   default:
      return AddressExpr::AddressQualifier::BYTE;
   }
}

std::unordered_map<std::string, POU> DeclEmitter::collectFunctionBlocks(const TranslationUnit& tu)
{
   std::unordered_map<std::string, POU> fbMap;

   for (const auto& pou : tu.pous) {
      if (pou.kind == POUKind::FUNCTION_BLOCK) {
         std::string name = m_ctx.normalizeType(pou.name);
         fbMap[name] = pou;
      }
   }

   return fbMap;
}

std::string DeclEmitter::generateMethodBody(const Method& method)
{
   // Not used directly; genMethodDefinition handles everything.
   // This function is kept for potential future use but is not needed.
   return "";
}

std::string DeclEmitter::generateHeaderComment() const
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

} // namespace st2cpp::codegen
