/**
 * @file SemanticBridge.cpp
 * @brief Read-only queries over the semantic analysis, for the code generator
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/SemanticBridge.h"
#include "codegen/CodegenTypes.h"
#include "semantic/Diagnostics.h"
#include "semantic/TypeSystem.h"

namespace st2cpp::codegen {

// The symbol and type names are used unqualified throughout: they are part of
// the vocabulary of this component, and spelling them out at every use would
// bury the logic that is worth reading.
using namespace st2cpp::semantic;
using st2cpp::library::LibraryDescriptor;

/**
 * @brief Check whether semantic analysis data is available
 *
 * @return true if SemanticInfo is attached and contains a symbol table, false otherwise
 */
bool SemanticBridge::semanticAvailable() const
{
   return m_semanticInfo != nullptr && m_semanticInfo->symbolTable != nullptr;
}

/**
 * @brief Get the semantic symbol table
 *
 * @return Pointer to the symbol table, or nullptr when no SemanticInfo is attached
 */
const st2cpp::semantic::SymbolTable* SemanticBridge::semanticSymTab() const
{
   if (!semanticAvailable()) {
      return nullptr;
   }
   return m_semanticInfo->symbolTable.get();
}

/**
 * @brief Library descriptor owning an external symbol, when resolvable.
 *
 * The generator never re-parses bindings: it walks back from the canonical
 * Symbol::externalLibraryId to the descriptor inside the SemanticInfo's
 * LibraryRegistry (the single source of truth of the C++ bindings).
 *
 * @param sym Semantic symbol to locate
 * @return Owning descriptor, or nullptr when local / semantics unavailable
 */
const st2cpp::library::LibraryDescriptor* SemanticBridge::semanticDescriptorFor(const st2cpp::semantic::Symbol& sym) const
{
   if (!semanticAvailable() || !m_semanticInfo->libraryRegistry) {
      return nullptr;
   }
   if (!sym.isExternal || sym.externalLibraryId.empty()) {
      return nullptr;
   }
   return m_semanticInfo->libraryRegistry->get(sym.externalLibraryId);
}

/**
 * @brief Append a clear warning to the attached diagnostics when a C++ binding
 *        field is missing. The generator never aborts for this: callers emit a
 *        controlled fallback (ST spelling) instead.
 */
void SemanticBridge::reportBindingIncomplete(const std::string& entity, const std::string& what) const
{
   if (!m_semanticInfo) {
      return;
   }
   st2cpp::semantic::SourceLocation loc;
   loc.fileName = "<library>";
   loc.line = 0;
   loc.column = 0;
   m_semanticInfo->diagnostics.addWarning(st2cpp::semantic::DiagnosticCode::ExternalBindingIncomplete,
                                          "external symbol '" + entity + "': C++ binding field '" + what
                                             + "' is missing or incomplete; falling back to the ST spelling",
                                          loc);
}

/**
 * @brief Qualified C++ name of a semantic struct/enum/FB type.
 *
 * External types resolve through their owning library descriptor; project-local
 * types keep the normalized ST spelling. Binding rules, in order:
 *  - struct/enum: descriptor symbol when cppBinding.symbol is set, else the
 *    verbatim descriptor name; prefixed with the library namespace when one is
 *    declared (unless the symbol is already qualified);
 *  - FB: instanceType (already qualified); empty instanceType is a warning.
 *
 * @param typeId   Canonical type id
 * @param fallback C++ spelling used for local types / unsolvable bindings
 * @return C++ type name
 */
std::string SemanticBridge::semanticTypeCppName(st2cpp::semantic::TypeId typeId, const std::string& fallback) const
{
   const auto* st = semanticSymTab();
   if (!st) {
      return fallback;
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   if (!t) {
      return fallback;
   }
   const st2cpp::semantic::Symbol* sym = t->symbolId ? st->get(t->symbolId) : nullptr;
   if (!sym || !sym->isExternal) {
      return fallback;
   }
   const st2cpp::library::LibraryDescriptor* lib = semanticDescriptorFor(*sym);
   if (!lib) {
      return fallback;
   }

   if (t->kind == st2cpp::semantic::TypeKind::FunctionBlock) {
      const st2cpp::library::FunctionBlockDef* fb = lib->findFunctionBlock(sym->name);
      if (!fb) {
         reportBindingIncomplete(sym->name, "functionBlocks");
         return fallback;
      }
      if (!fb->cppBinding.instanceType.empty()) {
         return fb->cppBinding.instanceType;
      }
      reportBindingIncomplete(sym->name, "instanceType");
      return fallback;
   }

   std::string symbol;
   if (t->kind == st2cpp::semantic::TypeKind::Struct) {
      const st2cpp::library::StructTypeDef* s = lib->findStruct(sym->name);
      if (!s) {
         reportBindingIncomplete(sym->name, "structs");
         return fallback;
      }
      symbol = (s->hasCppBinding && !s->cppBinding.symbol.empty()) ? s->cppBinding.symbol : s->name;
   } else if (t->kind == st2cpp::semantic::TypeKind::Enum) {
      const st2cpp::library::EnumTypeDef* e = lib->findEnum(sym->name);
      if (!e) {
         reportBindingIncomplete(sym->name, "enums");
         return fallback;
      }
      symbol = (e->hasCppBinding && !e->cppBinding.symbol.empty()) ? e->cppBinding.symbol : e->name;
   } else {
      return fallback;
   }

   // Namespace prefix from the descriptor, unless the symbol is free of it.
   if (symbol.find("::") != std::string::npos || lib->cppBinding.ns.empty()) {
      return symbol;
   }
   return lib->cppBinding.ns + "::" + symbol;
}

/**
 * @brief C++ name of an external enum member.
 *
 * The descriptor spelling (verbatim, reserved for fully-qualified bindings)
 * is authoritative; membership is matched case-insensitively against the
 * descriptor so the ST spelling and the C++ spelling may differ.
 *
 * @param enumTypeId   Resolved type of the ENUM
 * @param stMemberName ST spelling of the member
 * @return Descriptor member name, or the normalized ST spelling as fallback
 */
std::string SemanticBridge::semanticEnumeratorCppName(st2cpp::semantic::TypeId enumTypeId, const std::string& stMemberName) const
{
   const auto* st = semanticSymTab();
   if (!st) {
      return m_identifiers.ident(stMemberName);
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(enumTypeId);
   if (!t || t->kind != st2cpp::semantic::TypeKind::Enum) {
      return m_identifiers.ident(stMemberName);
   }
   const st2cpp::semantic::Symbol* sym = t->symbolId ? st->get(t->symbolId) : nullptr;
   if (!sym || !sym->isExternal) {
      return m_identifiers.ident(stMemberName);
   }
   const st2cpp::library::LibraryDescriptor* lib = semanticDescriptorFor(*sym);
   if (!lib) {
      return m_identifiers.ident(stMemberName);
   }
   const st2cpp::library::EnumTypeDef* def = lib->findEnum(sym->name);
   if (!def) {
      return m_identifiers.ident(stMemberName);
   }
   for (const auto& member : def->members) {
      if (st2cpp::library::LibraryDescriptor::makeKey(member.name) == st2cpp::library::LibraryDescriptor::makeKey(stMemberName)) {
         return member.name;
      }
   }
   return m_identifiers.ident(stMemberName);
}

/**
 * @brief Qualified enum name for an enumerator symbol
 *
 * Replaces the name-based m_enumeratorToEnum map: walks the system symbols
 * to find the ENUM type symbol that owns the given enumerator.
 *
 * @param enumeratorId Symbol id of the ENUMERATOR
 * @return Normalized enum type name, or empty if not found
 */
std::string SemanticBridge::semanticEnumNameForEnumerator(st2cpp::semantic::SymbolId enumeratorId) const
{
   const auto* st = semanticSymTab();
   if (!st) {
      return "";
   }
   for (const auto& sym : st->getSymbols()) {
      if (sym.id == 0 || sym.kind != st2cpp::semantic::SymbolKind::Type) {
         continue;
      }
      for (st2cpp::semantic::SymbolId e : sym.enumerators) {
         if (e == enumeratorId) {
            return m_identifiers.type(sym.name);
         }
      }
   }
   return "";
}

/**
 * @brief C++ binding of an external function callee.
 *
 * freeFunction binds to cppBinding.symbol verbatim (no namespace prefix,
 * per descriptor spec); staticMethod binds to owner::symbol.
 *
 * @param call Call expression to generate
 * @return C++ call target, or "" when not bindable (caller keeps ST spelling)
 */
std::string SemanticBridge::semanticCallTargetName(const CallExpr& call) const
{
   const auto* st = semanticSymTab();
   if (!st || call.calleeSymbolId == 0) {
      return "";
   }
   const st2cpp::semantic::Symbol* callee = st->get(call.calleeSymbolId);
   if (!callee || callee->kind != st2cpp::semantic::SymbolKind::Function || !callee->isExternal) {
      return "";
   }
   const st2cpp::library::LibraryDescriptor* lib = semanticDescriptorFor(*callee);
   if (!lib) {
      return "";
   }
   const st2cpp::library::FunctionDef* def = lib->findFunction(callee->name);
   if (!def) {
      reportBindingIncomplete(callee->name, "functions");
      return "";
   }
   switch (def->cppBinding.kind) {
   case st2cpp::library::FunctionBindingKind::StaticMethod: {
      if (def->cppBinding.owner.empty() || def->cppBinding.symbol.empty()) {
         reportBindingIncomplete(callee->name, "cppBinding.owner/symbol");
         break;
      }
      return def->cppBinding.owner + "::" + def->cppBinding.symbol;
   }
   case st2cpp::library::FunctionBindingKind::FreeFunction:
   default:
      if (def->cppBinding.symbol.empty()) {
         reportBindingIncomplete(callee->name, "cppBinding.symbol");
         break;
      }
      return def->cppBinding.symbol;
   }
   return "";
}

/**
 * @brief C++ binding of an external global variable or constant.
 *
 * Optional binding: an unbound global keeps its ST spelling, so no warning is
 * emitted and the empty string asks the caller to fall back.
 *
 * @param sym Referenced external symbol
 * @return C++ name, or "" when optional binding is absent
 */
std::string SemanticBridge::semanticVariableBinding(const st2cpp::semantic::Symbol& sym) const
{
   const st2cpp::library::LibraryDescriptor* lib = semanticDescriptorFor(sym);
   if (!lib) {
      return "";
   }
   if (sym.isConstant) {
      const st2cpp::library::Constant* c = lib->findConstant(sym.name);
      if (c && c->hasCppBinding && !c->cppBinding.symbol.empty()) {
         return c->cppBinding.symbol;
      }
      return "";
   }
   const st2cpp::library::GlobalVariable* g = lib->findGlobalVariable(sym.name);
   if (g && g->hasCppBinding && !g->cppBinding.symbol.empty()) {
      return g->cppBinding.symbol;
   }
   return "";
}

/**
 * @brief External FB step info for an FB invocation statement.
 *
 * Detects an invocation of an external-library FB instance (the variable type
 * resolves to an external FB symbol) and recovers the descriptor's
 * cppBinding.call. Project-local FBs keep isFb=false so the legacy binary-style
 * path (set_IN / get_Q / plain "callee()") applies unchanged.
 *
 * @param call FB invocation (callee may be an identifier or array element)
 * @return ExternalFbCallInfo{isFb=false} unless an external FB is invoked
 */
ExternalFbCallInfo SemanticBridge::semanticFbCallInfo(const CallExpr& call) const
{
   const auto* st = semanticSymTab();
   if (!st || !call.callee) {
      return {};
   }

   // Resolve the invoked object to a semantic TypeId (instance variable).
   st2cpp::semantic::TypeId typeId = 0;
   const st2cpp::semantic::Symbol* varSym = nullptr;
   const Expr* obj = call.callee.get();
   if (const auto* ident = std::get_if<IdentExpr>(&obj->node)) {
      varSym = ident->symbolId != 0 ? st->get(ident->symbolId) : nullptr;
   } else if (const auto* idx = std::get_if<IndexExpr>(&obj->node)) {
      if (const auto* arrIdent = std::get_if<IdentExpr>(&idx->array->node)) {
         varSym = arrIdent->symbolId != 0 ? st->get(arrIdent->symbolId) : nullptr;
      }
      obj = idx->array.get();
   }
   if (varSym) {
      typeId = varSym->typeId;
   } else if (obj) {
      typeId = obj->resolvedTypeId;
   }

   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   if (!t) {
      return {};
   }
   if (t->kind == st2cpp::semantic::TypeKind::Array) {
      t = st->getType(t->elementTypeId);
   }
   if (!t || t->kind != st2cpp::semantic::TypeKind::FunctionBlock) {
      return {};
   }
   const st2cpp::semantic::Symbol* fbSym = t->symbolId ? st->get(t->symbolId) : nullptr;
   if (!fbSym || !fbSym->isExternal) {
      return {}; // project FB: binary call retained byte-for-byte
   }

   ExternalFbCallInfo info;
   info.isFb = true;
   const st2cpp::library::LibraryDescriptor* lib = semanticDescriptorFor(*fbSym);
   if (!lib) {
      return info;
   }
   const st2cpp::library::FunctionBlockDef* def = lib->findFunctionBlock(fbSym->name);
   if (!def) {
      reportBindingIncomplete(fbSym->name, "functionBlocks");
      return info;
   }
   info.step = def->cppBinding.call;
   if (info.step.empty()) {
      reportBindingIncomplete(fbSym->name, "cppBinding.call");
   }
   return info;
}

/**
 * @brief Rebuild a call signature from calleeSymbolId + SymbolTable::params
 *
 * Replaces the name-based m_signatures lookup for call emission. The callee
 * may be a FUNCTION/FB symbol (params were registered by DeclVisitor) or an
 * FB instance, whose variable type is resolved back to the FB symbol.
 *
 * @param call The call expression being generated
 * @return Signature with ordered parameters, or nullopt when semantic data
 *         is unavailable / unhelpful (caller falls back to m_signatures)
 */
std::optional<FunctionSignature> SemanticBridge::semanticSignatureForCall(const CallExpr& call) const
{
   const auto* st = semanticSymTab();
   if (!st || call.calleeSymbolId == 0) {
      return std::nullopt;
   }

   const st2cpp::semantic::Symbol* callee = st->get(call.calleeSymbolId);
   if (!callee) {
      return std::nullopt;
   }

   // Resolve the signature symbol: for an FB instance variable, use the FB symbol.
   const st2cpp::semantic::Symbol* sigSym = callee;
   const st2cpp::semantic::TypeInfo* calleeType = st->getType(callee->typeId);
   if (calleeType && calleeType->kind == st2cpp::semantic::TypeKind::FunctionBlock) {
      // Prefer the project-local FB symbol (project declarations always shadow
      // external library symbols); fall back to the external FB when present.
      const st2cpp::semantic::Symbol* localSym = nullptr;
      const st2cpp::semantic::Symbol* externalSym = nullptr;
      for (const auto& sym : st->getSymbols()) {
         if (sym.kind != st2cpp::semantic::SymbolKind::FunctionBlock || sym.name != calleeType->name) {
            continue;
         }
         if (sym.isExternal) {
            if (externalSym == nullptr) {
               externalSym = &sym;
            }
         } else {
            localSym = &sym;
            break;
         }
      }
      if (localSym != nullptr) {
         sigSym = localSym;
      } else if (externalSym != nullptr) {
         sigSym = externalSym;
      }
   }

   if (sigSym->params.empty()) {
      return std::nullopt;
   }

   FunctionSignature sig;
   sig.name = m_identifiers.ident(callee->name);
   sig.returnType.base = BaseType::NAMED;

   for (st2cpp::semantic::SymbolId pid : sigSym->params) {
      const st2cpp::semantic::Symbol* p = st->get(pid);
      if (!p) {
         continue;
      }
      ParameterInfo pi;
      pi.name = m_identifiers.ident(p->name);
      // The declared direction drives how a call site binds the argument, and it
      // must match the mapping collectSignature() builds from the AST: only
      // VAR_INPUT is passed by value, VAR_OUTPUT is read back through a getter
      // (and takes a reference), VAR_IN_OUT is a required by-reference argument.
      // Marking every parameter as an input made an external VAR_OUTPUT receive
      // a setter call, and turned the "OUT without name" diagnostic in the call
      // emitter into dead code.
      pi.isInput = (p->paramDir != st2cpp::semantic::ParamDir::Output && p->paramDir != st2cpp::semantic::ParamDir::InOut);
      pi.isOutputVar = (p->paramDir == st2cpp::semantic::ParamDir::Output);
      const st2cpp::semantic::TypeInfo* pt = st->getType(p->typeId);
      pi.type.base = BaseType::NAMED;
      pi.type.name = pt ? pt->name : "";
      if (pi.isOutputVar) {
         pi.type.isRefTo = true;
      }
      sig.parameters.push_back(pi);
   }
   return sig;
}

/**
 * @brief Resolve a FB's symbol id from the semantic symbol table by name
 *
 * @param pou The FB POU to resolve
 * @return SymbolId of the FB symbol, or 0 when no SemanticInfo is attached
 *         or the FB is not registered (caller degrades to the legacy syntax)
 */
st2cpp::semantic::SymbolId SemanticBridge::semanticFbSymbolId(const POU& pou) const
{
   const auto* st = semanticSymTab();
   if (!st) {
      return 0;
   }
   // Prefer the project-local FB (POUs being generated are always project
   // declarations); external library FBs act as a fallback only.
   st2cpp::semantic::SymbolId externalId = 0;
   for (const auto& sym : st->getSymbols()) {
      if (sym.id == 0 || sym.kind != st2cpp::semantic::SymbolKind::FunctionBlock) {
         continue;
      }
      if (m_identifiers.type(sym.name) != m_identifiers.type(pou.name)) {
         continue;
      }
      if (!sym.isExternal) {
         return sym.id;
      }
      if (externalId == 0) {
         externalId = sym.id;
      }
   }
   return externalId;
}

/**
 * @brief Resolve a FB's base-class name
 *
 * Prefers the canonical semantic record (SemanticInfo::fbBaseClass, keyed by
 * the FB symbol) over the AST syntax `pou.extends`. When the FB is not in the
 * symbol table (no semantics attached), the legacy spelling is used so the
 * degradation path stays byte-identical.
 *
 * @param pou The FB POU
 * @return Normalized base-class name, or empty when the FB has no base
 */
std::string SemanticBridge::semanticBaseForFb(const POU& pou) const
{
   const auto* st = semanticSymTab();
   if (st) {
      st2cpp::semantic::SymbolId fbId = semanticFbSymbolId(pou);
      auto it = m_semanticInfo->fbBaseClass.find(fbId);
      if (it != m_semanticInfo->fbBaseClass.end()) {
         const st2cpp::semantic::Symbol* base = st->get(it->second);
         if (base) {
            return m_identifiers.type(base->name);
         }
      }
   }
   return pou.extends.empty() ? "" : m_identifiers.type(pou.extends);
}

std::string SemanticBridge::declaredIdent(const std::string& written, st2cpp::semantic::SymbolId symbolId) const
{
   if (m_identifiers.caseSensitive && symbolId != 0) {
      if (const st2cpp::semantic::Symbol* sym = semanticSymTab()->get(symbolId)) {
         if (!sym->name.empty() && sym->name != written) {
            return m_identifiers.ident(sym->name);
         }
      }
   }
   return m_identifiers.ident(written);
}

const std::vector<st2cpp::semantic::SymbolId>& SemanticBridge::fbTopoOrder() const
{
   static const std::vector<st2cpp::semantic::SymbolId> kEmpty;
   return m_semanticInfo ? m_semanticInfo->fbTopoOrder : kEmpty;
}

} // namespace st2cpp::codegen
