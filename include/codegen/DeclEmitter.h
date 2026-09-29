/**
 * @file DeclEmitter.h
 * @brief Declaration emission: POUs, structs, enums, globals and methods
 * @details Walks the translation unit and emits the declarations into the
 * header or source stream: function blocks, functions, programs, structs,
 * interfaces, enums, global variables and methods, and the signature
 * collection they rely on. Statement and expression bodies are delegated to
 * the `BodyEmitter`.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "codegen/BodyEmitter.h"
#include "codegen/EmissionContext.h"

namespace st2cpp::codegen {

/**
 * @class DeclEmitter
 * @brief Emits declarations and the shells that carry a body
 */
class DeclEmitter
{
public:
   DeclEmitter(EmissionContext& ctx, BodyEmitter& body) : m_ctx(ctx), m_body(body) {}

   void collectSignature(const POU& pou);
   std::unordered_map<std::string, POU> collectFunctionBlocks(const TranslationUnit& tu);

   void genPOU(const POU& pou);
   void genFunctionBlock(const POU& pou);
   void genFunction(const POU& pou);
   void genProgram(const POU& pou);
   void emitFunctionDecl(const POU& pou, std::ostringstream& out);

   void genStruct(const StructType& st);
   void genInterface(const Interface& iface);
   void generateStructsInOrder(const std::vector<StructType>& structs, std::ostringstream* out = nullptr);
   void genEnum(const EnumType& et);

   void genGlobals(const std::vector<VarSection>& globals);

   void genMethodDeclaration(const Method& method);
   void genMethodDefinition(const std::string& fbName, const Method& method);
   std::string generateMethodBody(const Method& method);

   std::string memberDecl(const VarDecl& d);
   AddressExpr::AddressQualifier deduceQualifierFromType(const TypeRef& type) const;
   std::string generateHeaderComment() const;

private:
   EmissionContext& m_ctx;
   BodyEmitter& m_body;
};

} // namespace st2cpp::codegen
