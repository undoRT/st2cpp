/**
 * @file DiagEmitter.cpp
 * @brief Implementation of the diagnostic table emitter
 *
 * The walk starts from the semantic symbol table, not from the AST, because the
 * table holds the resolved values this emitter needs: canonical TypeIds (an
 * alias resolves to its underlying type), base-class chains for EXTENDS, and the
 * declaration order that fixes the emitted C++ struct layout. The paths and
 * offsets produced here are only as good as that layout, which is why offsets
 * and sizes are spelled with offsetof()/sizeof() and the compiler is the one
 * that computes them.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/DiagEmitter.h"

#include "json/JsonValue.h"
#include "task/TaskConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>

namespace st2cpp::codegen {

// ============================================================================
// Emitted text helpers
// ============================================================================

namespace {

const char* kNodeKindExpr[] = {"undoDiag::NodeKind::Scalar",
                               "undoDiag::NodeKind::Array",
                               "undoDiag::NodeKind::Struct",
                               "undoDiag::NodeKind::Root"};
const char* kNodeKindName[] = {"Scalar", "Array", "Struct", "Root"};

std::string flagsExpr(uint8_t flags)
{
   if (flags == 0) {
      return "(uint8_t)(undoDiag::NodeFlags::None)";
   }
   std::string expr;
   auto append = [&](const char* bit) {
      if (!expr.empty()) {
         expr += " | ";
      }
      expr += "undoDiag::NodeFlags::";
      expr += bit;
   };
   if (flags & 1) {
      append("Readable");
   }
   if (flags & 2) {
      append("Writable");
   }
   if (flags & 4) {
      append("Forceable");
   }
   return "(uint8_t)(" + expr + ")";
}

// -- st2cpp::json constructors -------------------------------------------------

json::JsonValue jsonString(std::string s)
{
   json::JsonValue v;
   v.type = json::JsonType::String;
   v.text = std::move(s);
   return v;
}

json::JsonValue jsonNumber(double n)
{
   json::JsonValue v;
   v.type = json::JsonType::Number;
   v.number = n;
   // node ids and sizes are integers, and the manifest is read by tools: keep the
   // lexical form integral instead of leaking std::to_string's "3.000000".
   if (n == std::floor(n) && n >= -9.007199254740992e15 && n <= 9.007199254740992e15) {
      v.numberRaw = std::to_string(static_cast<int64_t>(n));
   } else {
      v.numberRaw = std::to_string(n);
   }
   return v;
}

json::JsonValue jsonBool(bool b)
{
   json::JsonValue v;
   v.type = json::JsonType::Bool;
   v.boolean = b;
   return v;
}

json::JsonValue jsonNull()
{
   json::JsonValue v;
   v.type = json::JsonType::Null;
   return v;
}

json::JsonValue jsonObject(std::vector<std::pair<std::string, json::JsonValue>> members)
{
   json::JsonValue v;
   v.type = json::JsonType::Object;
   v.members = std::move(members);
   return v;
}

json::JsonValue jsonArray(std::vector<json::JsonValue> elements)
{
   json::JsonValue v;
   v.type = json::JsonType::Array;
   v.array = std::move(elements);
   return v;
}

} // namespace

// ============================================================================
// Query helpers
// ============================================================================

uint32_t DiagEmitter::addNode(StNode node)
{
   m_nodes.push_back(std::move(node));
   return static_cast<uint32_t>(m_nodes.size() - 1);
}

std::string DiagEmitter::foldKey(const std::string& s)
{
   // Must match RuntimeEmitter::foldKey: the runtime binds instance roots by a
   // path built from the same task names this walk matches programs against, and
   // the two would silently disagree if they folded differently.
   std::string key;
   key.reserve(s.size());
   for (unsigned char c : s) {
      if (!std::isspace(c)) {
         key.push_back(static_cast<char>(std::toupper(c)));
      }
   }
   return key;
}

const st2cpp::semantic::Symbol* DiagEmitter::findProgram(const std::string& name) const
{
   const st2cpp::semantic::SymbolTable* symTab = table();
   if (!symTab) {
      return nullptr;
   }
   const std::string key = foldKey(name);
   for (const auto& sym : symTab->getSymbols()) {
      if (sym.kind == st2cpp::semantic::SymbolKind::Program && foldKey(sym.name) == key) {
         return &sym;
      }
   }
   return nullptr;
}

const st2cpp::semantic::Symbol* DiagEmitter::get(st2cpp::semantic::SymbolId id) const
{
   return id != 0 ? table()->get(id) : nullptr;
}

bool DiagEmitter::isAddressableType(const st2cpp::semantic::TypeInfo* t) const
{
   if (!t) {
      return false;
   }
   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
      return !t->isString;
   case st2cpp::semantic::TypeKind::Enum:
   case st2cpp::semantic::TypeKind::Array:
   case st2cpp::semantic::TypeKind::Struct:
   case st2cpp::semantic::TypeKind::FunctionBlock:
      return true;
   default:
      return false;
   }
}

std::string DiagEmitter::cppName(const st2cpp::semantic::TypeInfo* t) const
{
   if (!t) {
      return std::string();
   }
   std::string spelled = m_ctx.mapTypeId(t->id);
   if (spelled.empty()) {
      spelled = m_ctx.semanticTypeCppName(t->id, m_ctx.normalizeType(t->name));
   }
   return spelled;
}

std::optional<uint32_t> DiagEmitter::knownSize(const st2cpp::semantic::TypeInfo* t) const
{
   if (!t) {
      return std::nullopt;
   }
   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
      return !t->isString && t->sizeInBytes > 0 ? std::optional<uint32_t>(static_cast<uint32_t>(t->sizeInBytes)) : std::nullopt;
   case st2cpp::semantic::TypeKind::Enum:
      return 4u; // int32_t in the generated code
   case st2cpp::semantic::TypeKind::Array: {
      const st2cpp::semantic::TypeInfo* elem = table()->getType(t->elementTypeId);
      const std::optional<uint32_t> elemSize = knownSize(elem);
      if (!elemSize) {
         return std::nullopt;
      }
      uint64_t total = *elemSize;
      for (const auto& dim : t->dimensions) {
         total *= static_cast<uint64_t>(dim.high - dim.low + 1);
      }
      return total <= 0xFFFFFFFFu ? std::optional<uint32_t>(static_cast<uint32_t>(total)) : std::nullopt;
   }
   default:
      return std::nullopt;
   }
}

bool DiagEmitter::memcpySafe(const st2cpp::semantic::TypeInfo* t, std::unordered_set<st2cpp::semantic::TypeId>& visited) const
{
   if (!t) {
      return false;
   }
   if (!visited.insert(t->id).second) {
      return true; // cycle guard: a re-visit is not a hazard
   }
   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
      return !t->isString; // std::string owns memory: never memcpy the whole value
   case st2cpp::semantic::TypeKind::Enum:
      return true;
   case st2cpp::semantic::TypeKind::Array:
      return memcpySafe(table()->getType(t->elementTypeId), visited);
   case st2cpp::semantic::TypeKind::Struct: {
      const st2cpp::semantic::Symbol* structSym = get(t->symbolId);
      if (!structSym) {
         return false;
      }
      for (const st2cpp::semantic::SymbolId memberId : structSym->members) {
         const st2cpp::semantic::Symbol* member = get(memberId);
         if (!member || !memcpySafe(table()->getType(member->typeId), visited)) {
            return false;
         }
      }
      return true;
   }
   case st2cpp::semantic::TypeKind::FunctionBlock: {
      // Whole-instance reads cover the whole base chain, so the copy safety of
      // every effective member matters, not just the deriving block's own.
      const st2cpp::semantic::Symbol* fbSym = get(t->symbolId);
      std::unordered_set<st2cpp::semantic::SymbolId> chain;
      for (; fbSym && chain.insert(fbSym->id).second; fbSym = get(fbSym->baseClassId)) {
         for (const auto& candidate : table()->getSymbols()) {
            if (candidate.scopeId != fbSym->scopeId
                || (candidate.kind != st2cpp::semantic::SymbolKind::Variable
                    && candidate.kind != st2cpp::semantic::SymbolKind::Parameter)) {
               continue;
            }
            if (!memcpySafe(table()->getType(candidate.typeId), visited)) {
               return false;
            }
         }
      }
      return true;
   }
   default:
      return false;
   }
}

// ============================================================================
// The walk
// ============================================================================

void DiagEmitter::emitProgramNode(const st2cpp::semantic::Symbol& progSym, const std::string& path)
{
   const std::string progName = m_ctx.normalizeType(progSym.name);

   // A program is a plain data struct in the generated code, so copying it is
   // safe exactly when every one of its members is.
   std::unordered_set<st2cpp::semantic::TypeId> visited;
   bool copyable = true;
   for (const auto& candidate : table()->getSymbols()) {
      if (candidate.scopeId != progSym.scopeId
          || (candidate.kind != st2cpp::semantic::SymbolKind::Variable && candidate.kind != st2cpp::semantic::SymbolKind::Parameter)) {
         continue;
      }
      if (!memcpySafe(table()->getType(candidate.typeId), visited)) {
         copyable = false;
         break;
      }
   }

   StNode root;
   root.name = progName;
   root.path = path;
   root.parent = 0;
   root.root = true;
   root.kindExpr = kNodeKindExpr[3]; // Root
   root.kindName = kNodeKindName[3];
   root.flags = copyable ? FLAG_READ : 0;
   root.offsetExpr = "0";
   root.sizeExpr = "sizeof(" + progName + ")";
   root.elemSizeExpr = "0";
   root.cppType = progName;
   const uint32_t rootId = addNode(std::move(root));

   // A program is a POU, not a STRUCT: its variables live in the program scope,
   // not in Symbol::members (only struct types fill that list). The FB walk
   // enumerates exactly the variables and parameters of a scope, and a program
   // simply has no base-class chain to prepend.
   emitScopedMembers(rootId, progSym, progName, path);
}

bool DiagEmitter::isGlobalReflectable(const st2cpp::semantic::Symbol& sym) const
{
   const st2cpp::semantic::SymbolTable* symTab = table();
   if (!symTab || sym.kind != st2cpp::semantic::SymbolKind::Variable || sym.scopeId != symTab->globalScope() || !sym.atAddress.empty()
       || sym.paramDir == st2cpp::semantic::ParamDir::InOut || sym.isConstant) {
      return false; // AT vars live in the process image, VAR_IN_OUT is a handle, constants are not addressable
   }
   const st2cpp::semantic::TypeInfo* t = table()->getType(sym.typeId);
   if (!isAddressableType(t)) {
      return false; // pointers, interfaces, strings and unresolved types have no row
   }
   if (t->kind == st2cpp::semantic::TypeKind::Array) {
      if (t->dimensions.size() > MAX_DIMS) {
         return false;
      }
      const st2cpp::semantic::TypeInfo* elem = table()->getType(t->elementTypeId);
      if (!isAddressableType(elem)) {
         return false; // an array of strings or pointers has no byte-addressable leaf
      }
   }
   return true;
}

std::vector<DiagEmitter::GlobalRoot> DiagEmitter::globalRoots() const
{
   std::vector<GlobalRoot> roots;
   const st2cpp::semantic::SymbolTable* symTab = table();
   if (!symTab) {
      return roots;
   }
   for (const auto& sym : symTab->getSymbols()) {
      if (!isGlobalReflectable(sym)) {
         continue;
      }
      const std::string name = m_ctx.normalizeIdent(sym.name);
      const std::string cppRef = m_ctx.m_namespace.empty() ? name : m_ctx.m_namespace + "::" + name;
      roots.push_back({name, cppRef, false});
   }
   // The process-image areas are roots too when this TU maps AT variables into
   // them. The runtime binds them by value (the accessor IS the base pointer).
   if (m_ctx.m_hasAddresses && m_ctx.m_piConfig.inputBytes > 0) {
      const std::string ns = m_ctx.m_namespace.empty() ? "" : m_ctx.m_namespace + "::";
      roots.push_back({m_ctx.m_piConfig.instanceName + ".I", ns + m_ctx.m_piConfig.instanceName + ".plcInputPtr()", true});
   }
   if (m_ctx.m_hasAddresses && m_ctx.m_piConfig.outputBytes > 0) {
      const std::string ns = m_ctx.m_namespace.empty() ? "" : m_ctx.m_namespace + "::";
      roots.push_back({m_ctx.m_piConfig.instanceName + ".Q", ns + m_ctx.m_piConfig.instanceName + ".plcOutputPtr()", true});
   }
   if (m_ctx.m_hasAddresses && m_ctx.m_piConfig.markerBytes > 0) {
      const std::string ns = m_ctx.m_namespace.empty() ? "" : m_ctx.m_namespace + "::";
      roots.push_back({m_ctx.m_piConfig.instanceName + ".M", ns + m_ctx.m_piConfig.instanceName + ".plcMarkerPtr()", true});
   }
   return roots;
}

void DiagEmitter::emitProcessImageRoots()
{
   // The three process-image areas are reflected as one Array root each: a flat
   // byte area that a Force or a Read addresses with a single index. This is the
   // path that makes a physical force reach the fieldbus - AT outputs live here,
   // and onBeforeOutputsWrite() runs before copyOut(). Sizes come from the same
   // ProcessImageConfig that sized the generated ProcessImage instance.
   const std::string instance = m_ctx.m_piConfig.instanceName;
   const auto emitArea = [&](const char* suffix, size_t bytes) {
      if (bytes == 0) {
         return;
      }
      StNode node;
      node.name = suffix;
      node.path = instance + "." + suffix;
      node.parent = 0;
      node.root = true;
      node.kindExpr = kNodeKindExpr[1]; // Array
      node.kindName = kNodeKindName[1];
      node.flags = FLAG_READ | FLAG_WRITE | FLAG_FORCE;
      node.dimCount = 1;
      node.low[0] = 0;
      node.high[0] = static_cast<int32_t>(bytes) - 1;
      node.offsetExpr = "0";
      node.sizeExpr = std::to_string(bytes);
      node.elemSizeExpr = "1";
      node.cppType = "uint8_t";
      node.numSize = static_cast<uint32_t>(bytes);
      node.numElemSize = 1;
      addNode(std::move(node));
   };
   emitArea("I", m_ctx.m_piConfig.inputBytes);
   emitArea("Q", m_ctx.m_piConfig.outputBytes);
   emitArea("M", m_ctx.m_piConfig.markerBytes);
}

void DiagEmitter::emitGlobalNode(const st2cpp::semantic::Symbol& sym)
{
   if (!isGlobalReflectable(sym)) {
      return; // the single filter every entry point shares
   }
   const st2cpp::semantic::TypeInfo* t = table()->getType(sym.typeId);

   const std::string name = m_ctx.normalizeIdent(sym.name);
   const std::string path = name;

   StNode node;
   node.name = name;
   node.path = path;
   node.parent = 0;
   node.root = true;
   node.offsetExpr = "0";
   node.cppType = cppName(t);

   const uint8_t leafFlags = FLAG_READ | FLAG_WRITE | FLAG_FORCE;
   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
   case st2cpp::semantic::TypeKind::Enum:
      node.kindExpr = kNodeKindExpr[0]; // Scalar
      node.kindName = kNodeKindName[0];
      node.flags = leafFlags;
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      node.elemSizeExpr = "0";
      if (const std::optional<uint32_t> sz = knownSize(t)) {
         node.numSize = sz;
      }
      addNode(std::move(node));
      return;
   case st2cpp::semantic::TypeKind::Array: {
      if (t->dimensions.size() > MAX_DIMS) {
         return;
      }
      const st2cpp::semantic::TypeInfo* elem = table()->getType(t->elementTypeId);
      if (!isAddressableType(elem)) {
         return; // an array of strings or pointers has no byte-addressable leaf
      }
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      const bool elementCopyable = memcpySafe(elem, visited);
      const bool elementScalar = elem->kind == st2cpp::semantic::TypeKind::Elementary || elem->kind == st2cpp::semantic::TypeKind::Enum;
      node.kindExpr = kNodeKindExpr[1]; // Array
      node.kindName = kNodeKindName[1];
      node.flags = (elementCopyable ? FLAG_READ : 0) | (elementScalar ? FLAG_WRITE | FLAG_FORCE : 0);
      node.dimCount = static_cast<uint8_t>(t->dimensions.size());
      for (size_t i = 0; i < t->dimensions.size(); ++i) {
         node.low[i] = t->dimensions[i].low;
         node.high[i] = t->dimensions[i].high;
      }
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      node.elemSizeExpr = "sizeof(" + cppName(elem) + ")";
      if (const std::optional<uint32_t> sz = knownSize(t)) {
         node.numSize = sz;
      }
      if (const std::optional<uint32_t> es = knownSize(elem)) {
         node.numElemSize = es;
      }
      const uint32_t arrayId = addNode(std::move(node));
      emitArrayElement(arrayId, elem, path);
      return;
   }
   case st2cpp::semantic::TypeKind::Struct: {
      node.kindExpr = kNodeKindExpr[2]; // Struct
      node.kindName = kNodeKindName[2];
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      node.flags = memcpySafe(t, visited) ? FLAG_READ : 0;
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      node.elemSizeExpr = "0";
      // Capture the C++ spelling before the move: the members' offsetof() is
      // measured against it, and a moved-from string would emit "0" instead.
      const std::string containerCpp = node.cppType;
      const uint32_t structId = addNode(std::move(node));
      const st2cpp::semantic::Symbol* structSym = get(t->symbolId);
      if (structSym) {
         emitStructMembers(structId, *structSym, containerCpp, path);
      }
      return;
   }
   case st2cpp::semantic::TypeKind::FunctionBlock: {
      node.kindExpr = kNodeKindExpr[2]; // Struct (an FB instance is an aggregate)
      node.kindName = kNodeKindName[2];
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      node.flags = memcpySafe(t, visited) ? FLAG_READ : 0;
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      node.elemSizeExpr = "0";
      const std::string containerCpp = node.cppType;
      const uint32_t fbId = addNode(std::move(node));
      const st2cpp::semantic::Symbol* fbSym = get(t->symbolId);
      if (fbSym) {
         emitScopedMembers(fbId, *fbSym, containerCpp, path);
      }
      return;
   }
   default:
      return;
   }
}

void DiagEmitter::emitValueNode(const st2cpp::semantic::Symbol& sym,
                                uint32_t parent,
                                const std::string& containerCpp,
                                const std::string& parentPath)
{
   if (sym.paramDir == st2cpp::semantic::ParamDir::InOut || !sym.atAddress.empty()) {
      return; // VAR_IN_OUT is a handle, AT vars have no storage
   }
   const std::string name = m_ctx.normalizeIdent(sym.name);
   const st2cpp::semantic::TypeInfo* t = table()->getType(sym.typeId);
   if (!t || !isAddressableType(t)) {
      return;
   }
   const std::string path = parentPath + "." + name;
   const std::string offsetExpr = containerCpp.empty() ? std::string("0") : "offsetof(" + containerCpp + ", " + name + ")";
   const std::string typeCpp = cppName(t);
   const uint8_t leafFlags = sym.isConstant ? FLAG_READ : FLAG_READ | FLAG_WRITE | FLAG_FORCE;

   StNode node;
   node.name = name;
   node.path = path;
   node.parent = parent;
   node.offsetExpr = offsetExpr;
   node.cppType = typeCpp;

   switch (t->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
   case st2cpp::semantic::TypeKind::Enum: {
      node.kindExpr = kNodeKindExpr[0]; // Scalar
      node.kindName = kNodeKindName[0];
      node.flags = leafFlags;
      node.sizeExpr = "sizeof(" + typeCpp + ")";
      node.elemSizeExpr = "0";
      if (const std::optional<uint32_t> sz = knownSize(t)) {
         node.numSize = sz;
      }
      // Bind the id before touching the children vector: addNode() may grow
      // m_nodes and reallocate it, which would dangle a reference taken from
      // m_nodes[parent] as the push_back argument.
      const uint32_t scalarId = addNode(std::move(node));
      m_nodes[parent].children.push_back(scalarId);
      return;
   }
   case st2cpp::semantic::TypeKind::Array: {
      if (t->dimensions.size() > MAX_DIMS) {
         return;
      }
      const st2cpp::semantic::TypeInfo* elem = table()->getType(t->elementTypeId);
      if (!isAddressableType(elem)) {
         return;
      }
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      const bool elementCopyable = memcpySafe(elem, visited);
      const bool elementScalar = elem->kind == st2cpp::semantic::TypeKind::Elementary || elem->kind == st2cpp::semantic::TypeKind::Enum;
      node.kindExpr = kNodeKindExpr[1]; // Array
      node.kindName = kNodeKindName[1];
      node.flags = (elementCopyable ? FLAG_READ : 0) | (elementScalar ? FLAG_WRITE | FLAG_FORCE : 0);
      node.dimCount = static_cast<uint8_t>(t->dimensions.size());
      for (size_t i = 0; i < t->dimensions.size(); ++i) {
         node.low[i] = t->dimensions[i].low;
         node.high[i] = t->dimensions[i].high;
      }
      node.sizeExpr = "sizeof(" + typeCpp + ")";
      node.elemSizeExpr = "sizeof(" + cppName(elem) + ")";
      if (const std::optional<uint32_t> sz = knownSize(t)) {
         node.numSize = sz;
      }
      if (const std::optional<uint32_t> es = knownSize(elem)) {
         node.numElemSize = es;
      }
      // An array is a member too: link it into the parent's child list exactly like
      // a struct or an FB, so firstChild/childCount describe the whole tree.
      const uint32_t arrayId = addNode(std::move(node));
      m_nodes[parent].children.push_back(arrayId);
      emitArrayElement(arrayId, elem, path);
      return;
   }
   case st2cpp::semantic::TypeKind::Struct: {
      node.kindExpr = kNodeKindExpr[2]; // Struct
      node.kindName = kNodeKindName[2];
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      node.flags = memcpySafe(t, visited) ? FLAG_READ : 0;
      node.sizeExpr = "sizeof(" + typeCpp + ")";
      node.elemSizeExpr = "0";
      const uint32_t structId = addNode(std::move(node));
      m_nodes[parent].children.push_back(structId);
      const st2cpp::semantic::Symbol* structSym = get(t->symbolId);
      if (structSym) {
         emitStructMembers(structId, *structSym, typeCpp, path);
      }
      return;
   }
   case st2cpp::semantic::TypeKind::FunctionBlock: {
      node.kindExpr = kNodeKindExpr[2]; // Struct
      node.kindName = kNodeKindName[2];
      std::unordered_set<st2cpp::semantic::TypeId> visited;
      node.flags = memcpySafe(t, visited) ? FLAG_READ : 0;
      node.sizeExpr = "sizeof(" + typeCpp + ")";
      node.elemSizeExpr = "0";
      const uint32_t fbId = addNode(std::move(node));
      m_nodes[parent].children.push_back(fbId);
      const st2cpp::semantic::Symbol* fbSym = get(t->symbolId);
      if (fbSym) {
         emitScopedMembers(fbId, *fbSym, typeCpp, path);
      }
      return;
   }
   default:
      return;
   }
}

void DiagEmitter::emitStructMembers(uint32_t parent,
                                    const st2cpp::semantic::Symbol& structSym,
                                    const std::string& containerCpp,
                                    const std::string& parentPath)
{
   for (const st2cpp::semantic::SymbolId memberId : structSym.members) {
      const st2cpp::semantic::Symbol* member = get(memberId);
      if (member
          && (member->kind == st2cpp::semantic::SymbolKind::Variable || member->kind == st2cpp::semantic::SymbolKind::Parameter
              || member->kind == st2cpp::semantic::SymbolKind::StructMember)) {
         emitValueNode(*member, parent, containerCpp, parentPath);
      }
   }
}

void DiagEmitter::emitScopedMembers(uint32_t parent,
                                    const st2cpp::semantic::Symbol& pouSym,
                                    const std::string& containerCpp,
                                    const std::string& parentPath)
{
   // The effective member set of a POU is the whole base chain, root ancestor
   // first, which is also the memory order of the generated struct (base
   // subobject first). A program has no base class, so the chain is just
   // itself. Offsets are still measured from the deriving object: the generated
   // row uses offsetof(DERIVED, member) for every member of the chain.
   std::vector<const st2cpp::semantic::Symbol*> chain;
   {
      std::unordered_set<st2cpp::semantic::SymbolId> seen;
      const st2cpp::semantic::Symbol* cursor = &pouSym;
      while (cursor && seen.insert(cursor->id).second) {
         chain.push_back(cursor);
         cursor = get(cursor->baseClassId);
      }
      std::reverse(chain.begin(), chain.end());
   }
   std::unordered_set<std::string> emitted;
   for (const st2cpp::semantic::Symbol* block : chain) {
      for (const auto& candidate : table()->getSymbols()) {
         if (candidate.scopeId != block->scopeId
             || (candidate.kind != st2cpp::semantic::SymbolKind::Variable && candidate.kind != st2cpp::semantic::SymbolKind::Parameter)) {
            continue;
         }
         if (!emitted.insert(table()->normalizeKey(candidate.name)).second) {
            continue; // shadowed by an earlier block in the chain
         }
         emitValueNode(candidate, parent, containerCpp, parentPath);
      }
   }
}

void DiagEmitter::emitArrayElement(uint32_t parent, const st2cpp::semantic::TypeInfo* elementType, const std::string& parentPath)
{
   const std::string path = parentPath + "[]";

   StNode node;
   node.name = "[]";
   node.path = path;
   node.parent = parent;
   node.offsetExpr = "0";
   node.elemSizeExpr = "0";

   switch (elementType->kind) {
   case st2cpp::semantic::TypeKind::Elementary:
   case st2cpp::semantic::TypeKind::Enum: {
      // Pure structure: reads/writes that index the array target the Array
      // node, so the element node only exists to anchor nested dimensions.
      node.kindExpr = kNodeKindExpr[0]; // Scalar
      node.kindName = kNodeKindName[0];
      node.flags = 0;
      node.cppType = cppName(elementType);
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      if (const std::optional<uint32_t> sz = knownSize(elementType)) {
         node.numSize = sz;
      }
      const uint32_t elemId = addNode(std::move(node));
      m_nodes[parent].children.push_back(elemId);
      return;
   }
   case st2cpp::semantic::TypeKind::Array: {
      if (elementType->dimensions.size() > MAX_DIMS) {
         return;
      }
      node.kindExpr = kNodeKindExpr[1]; // Array
      node.kindName = kNodeKindName[1];
      node.flags = 0;
      node.cppType = cppName(elementType);
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      node.dimCount = static_cast<uint8_t>(elementType->dimensions.size());
      for (size_t i = 0; i < elementType->dimensions.size(); ++i) {
         node.low[i] = elementType->dimensions[i].low;
         node.high[i] = elementType->dimensions[i].high;
      }
      const st2cpp::semantic::TypeInfo* inner = table()->getType(elementType->elementTypeId);
      if (inner) {
         node.elemSizeExpr = "sizeof(" + cppName(inner) + ")";
      } else {
         node.elemSizeExpr = "0";
      }
      if (const std::optional<uint32_t> sz = knownSize(elementType)) {
         node.numSize = sz;
      }
      if (const std::optional<uint32_t> es = inner ? knownSize(inner) : std::nullopt) {
         node.numElemSize = es;
      }
      const uint32_t nestedId = addNode(std::move(node));
      m_nodes[parent].children.push_back(nestedId);
      emitArrayElement(nestedId, inner, path);
      return;
   }
   case st2cpp::semantic::TypeKind::Struct: {
      node.kindExpr = kNodeKindExpr[2]; // Struct
      node.kindName = kNodeKindName[2];
      node.flags = 0;
      node.cppType = cppName(elementType);
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      const std::string containerCpp = node.cppType;
      const uint32_t structId = addNode(std::move(node));
      m_nodes[parent].children.push_back(structId);
      const st2cpp::semantic::Symbol* structSym = get(elementType->symbolId);
      if (structSym) {
         emitStructMembers(structId, *structSym, containerCpp, path);
      }
      return;
   }
   case st2cpp::semantic::TypeKind::FunctionBlock: {
      node.kindExpr = kNodeKindExpr[2]; // Struct
      node.kindName = kNodeKindName[2];
      node.flags = 0;
      node.cppType = cppName(elementType);
      node.sizeExpr = "sizeof(" + node.cppType + ")";
      const std::string containerCpp = node.cppType;
      const uint32_t fbId = addNode(std::move(node));
      m_nodes[parent].children.push_back(fbId);
      const st2cpp::semantic::Symbol* fbSym = get(elementType->symbolId);
      if (fbSym) {
         emitScopedMembers(fbId, *fbSym, containerCpp, path);
      }
      return;
   }
   default:
      return;
   }
}

// ============================================================================
// Emission
// ============================================================================

void DiagEmitter::reorderBreadthFirst()
{
   if (m_nodes.size() < 2) {
      return;
   }
   // Breadth-first visit: the direct children of any node become a contiguous
   // block of ids, so the firstChild + [0, childCount) range a consumer indexes
   // is exactly the node's children and nothing else.
   std::vector<uint32_t> order;
   order.reserve(m_nodes.size());
   for (size_t i = 0; i < m_nodes.size(); ++i) {
      if (m_nodes[i].root) {
         order.push_back(static_cast<uint32_t>(i));
      }
   }
   if (order.empty() && !m_nodes.empty()) {
      order.push_back(0); // defensive: nothing marked root cannot happen by construction
   }
   for (size_t cursor = 0; cursor < order.size(); ++cursor) {
      for (uint32_t child : m_nodes[order[cursor]].children) {
         order.push_back(child);
      }
   }

   std::vector<uint32_t> newId(m_nodes.size());
   for (size_t newPos = 0; newPos < order.size(); ++newPos) {
      newId[order[newPos]] = static_cast<uint32_t>(newPos);
   }

   std::vector<StNode> reordered;
   reordered.reserve(m_nodes.size());
   for (uint32_t old : order) {
      StNode node = std::move(m_nodes[old]);
      if (!node.root) {
         node.parent = newId[node.parent];
      }
      for (uint32_t& child : node.children) {
         child = newId[child];
      }
      reordered.push_back(std::move(node));
   }
   m_nodes = std::move(reordered);
}

DiagEmitter::DiagFiles DiagEmitter::generate(const st2cpp::task::TaskConfig* config)
{
   DiagFiles result;
   m_nodes.clear();

   const st2cpp::semantic::SymbolTable* symTab = table();
   if (symTab) {
      const bool haveInstances = config != nullptr && !config->tasks.empty();
      if (haveInstances) {
         // One root per live program instance: the path carries the PLC and the
         // task, so a program bound to two tasks (two independent objects) yields
         // two addressable roots instead of one ambiguous one.
         for (const st2cpp::task::PlcGroup& group : st2cpp::task::groupByPlc(*config)) {
            for (size_t index : group.tasks) {
               const st2cpp::task::TaskEntry& entry = config->tasks[index];
               const std::string prefix = group.plc + "." + entry.name;
               for (const std::string& program : entry.programs) {
                  const st2cpp::semantic::Symbol* progSym = findProgram(program);
                  if (progSym) {
                     emitProgramNode(*progSym, prefix + "." + m_ctx.normalizeType(progSym->name));
                  }
               }
            }
         }
      } else {
         for (const auto& sym : symTab->getSymbols()) {
            if (sym.kind == st2cpp::semantic::SymbolKind::Program) {
               emitProgramNode(sym, m_ctx.normalizeType(sym.name));
            }
         }
      }
      for (const auto& sym : symTab->getSymbols()) {
         if (isGlobalReflectable(sym)) {
            emitGlobalNode(sym);
         }
      }
      // The byte areas a global AT variable maps into are roots of the table as
      // well, so a Force can reach physical I/O - the reason the physical-force
      // hook exists in the first place.
      if (m_ctx.m_hasAddresses) {
         emitProcessImageRoots();
      }
   }

   // Children get consecutive ids only if the table is ordered breadth-first,
   // which is what makes the emitted firstChild/childCount range equal the real
   // child set of every node.
   reorderBreadthFirst();

   result.nodeCount = m_nodes.size();
   result.globalRoots = globalRoots();

   // Every type the table spells (program structs, FB structs, GVL structs) has
   // to be visible from DiagVars.cpp. Programs.hpp pulls in GVLs.hpp, which
   // pulls in FunctionBlocks.hpp and SimpleGVLs.hpp, so that single include is
   // enough whenever a program exists; during the modular pass those masters are
   // always emitted, so the include is unconditionally safe there too.
   bool hasPrograms = false;
   if (symTab) {
      for (const auto& sym : symTab->getSymbols()) {
         if (sym.kind == st2cpp::semantic::SymbolKind::Program) {
            hasPrograms = true;
            break;
         }
      }
   }
   const std::string projectMaster = hasPrograms ? "Programs.hpp" : "GVLs.hpp";

   const std::string comment = R"(/**
 * @file GENERATED FILE - DO NOT EDIT MANUALLY
 * @brief Automatically generated from Structured Text source
 *
 * This file was generated by st2cpp, the Structured Text to C++ compiler.
 * Any manual changes will be overwritten the next time the source is processed.
 *
 * @copyright Copyright (c) 2026 Salvatore Bamundo
 * @license SPDX-License-Identifier: GPL-3.0-or-later
 *
 * st2cpp - Structured Text to C++ Compiler
 * This is free software; see the source for copying conditions.
 */)";

   // ---- Header ----------------------------------------------------------------

   {
      std::ostringstream out;
      out << comment << "\n\n";
      out << "#pragma once\n\n";
      out << "#include \"undoDiag.hpp\"\n";
      out << "#include <cstdint>\n\n";

      const std::string ns = m_ctx.m_namespace;
      const bool hasNs = !ns.empty();
      if (hasNs) {
         out << "namespace " << ns << " {\n\n";
      }
      out << "// One row per variable and member reachable from a program or a global of\n";
      out << "// this translation unit, laid out exactly as undoDiag::Hub walks it. The\n";
      out << "// offsets themselves live in DiagVars.cpp as offsetof()/sizeof() expressions,\n";
      out << "// so the table never drifts from the layout the compiler chose. DiagVars.json\n";
      out << "// is the same structure for tools that do not parse C++.\n";
      out << "extern const undoDiag::Node kDiagNodes[];\n";
      out << "extern const uint32_t kDiagNodeCount;\n";
      if (hasNs) {
         out << "\n} // namespace " << ns << "\n";
      }
      result.header = out.str();
   }

   // ---- Source ----------------------------------------------------------------

   {
      std::ostringstream out;
      out << comment << "\n\n";
      out << "#include \"DiagVars.hpp\"\n";
      out << "#include \"" << projectMaster << "\"\n\n";
      out << "// Offsets feed off the compiler: every row is an offsetof()/sizeof() against\n";
      out << "// the C++ structs the declarations were emitted as, so a change of layout\n";
      out << "// (a realignment, a base class) changes the table instead of invalidating it.\n";
      out << "// GCC warns about offsetof() on classes that are not standard-layout, which\n";
      out << "// EXTENDS and FB-typed members produce exactly because the layout is what we\n";
      out << "// want measured; the region below silences only that diagnostic.\n";
      out << "#if defined(__GNUC__) || defined(__clang__)\n";
      out << "#pragma GCC diagnostic push\n";
      out << "#pragma GCC diagnostic ignored \"-Winvalid-offsetof\"\n";
      out << "#endif\n\n";

      const std::string ns = m_ctx.m_namespace;
      const bool hasNs = !ns.empty();
      if (hasNs) {
         out << "namespace " << ns << " {\n\n";
      }
      out << "const undoDiag::Node kDiagNodes[] = {\n";
      for (size_t i = 0; i < m_nodes.size(); ++i) {
         const StNode& n = m_nodes[i];
         const uint32_t firstChild = n.children.empty() ? 0u : n.children.front();
         const uint16_t childCount = static_cast<uint16_t>(n.children.size());

         out << "   // " << i << ": " << n.path << " (" << n.kindName << ")\n";
         out << "   { \"" << n.name << "\", \"" << n.path << "\", " << (n.root ? "undoDiag::NO_PARENT" : std::to_string(n.parent)) << ", "
             << firstChild << "u, " << childCount << "u, (uint8_t)" << n.kindExpr << ", " << flagsExpr(n.flags) << ", "
             << static_cast<unsigned>(n.dimCount) << "u, 0u, " << n.offsetExpr << ", " << n.sizeExpr << ", " << n.elemSizeExpr << ", { "
             << n.low[0] << ", " << n.low[1] << ", " << n.low[2] << ", " << n.low[3] << " }, { " << n.high[0] << ", " << n.high[1] << ", "
             << n.high[2] << ", " << n.high[3] << " } },";
         out << "\n";
      }
      out << "};\n";
      out << "const uint32_t kDiagNodeCount = sizeof(kDiagNodes) / sizeof(kDiagNodes[0]);\n\n";
      if (hasNs) {
         out << "} // namespace " << ns << "\n\n";
      }
      out << "#if defined(__GNUC__) || defined(__clang__)\n";
      out << "#pragma GCC diagnostic pop\n";
      out << "#endif\n";
      result.source = out.str();
   }

   // ---- Manifest ---------------------------------------------------------------

   {
      std::vector<json::JsonValue> nodes;
      nodes.reserve(m_nodes.size());
      for (size_t i = 0; i < m_nodes.size(); ++i) {
         const StNode& n = m_nodes[i];

         std::vector<json::JsonValue> dims;
         dims.reserve(n.dimCount);
         for (uint8_t d = 0; d < n.dimCount; ++d) {
            dims.push_back(jsonObject({{"low", jsonNumber(n.low[d])}, {"high", jsonNumber(n.high[d])}}));
         }

         std::vector<std::pair<std::string, json::JsonValue>> obj;
         obj.emplace_back("id", jsonNumber(static_cast<double>(i)));
         obj.emplace_back("name", jsonString(n.name));
         obj.emplace_back("path", jsonString(n.path));
         obj.emplace_back("parent", n.root ? jsonNull() : jsonNumber(n.parent));
         obj.emplace_back("kind", jsonString(n.kindName));
         obj.emplace_back("flags",
                          jsonObject({{"readable", jsonBool((n.flags & FLAG_READ) != 0)},
                                      {"writable", jsonBool((n.flags & FLAG_WRITE) != 0)},
                                      {"forceable", jsonBool((n.flags & FLAG_FORCE) != 0)}}));
         obj.emplace_back("dimCount", jsonNumber(n.dimCount));
         obj.emplace_back("dims", jsonArray(std::move(dims)));
         obj.emplace_back("size", n.numSize ? jsonNumber(*n.numSize) : jsonNull());
         obj.emplace_back("elemSize", n.numElemSize ? jsonNumber(*n.numElemSize) : jsonNull());
         obj.emplace_back("offset", n.root ? jsonNumber(0.0) : jsonNull());
         obj.emplace_back("type", jsonString(n.cppType));
         nodes.push_back(jsonObject(std::move(obj)));
      }

      json::JsonValue root;
      root.type = json::JsonType::Object;
      root.members.emplace_back("format", jsonString("st2cpp.diag.node-table"));
      root.members.emplace_back("version", jsonNumber(1.0));
      root.members.emplace_back("nodeCount", jsonNumber(static_cast<double>(m_nodes.size())));
      root.members.emplace_back("nodes", jsonArray(std::move(nodes)));

      result.manifest = json::dump(root, 2);
   }

   return result;
}

} // namespace st2cpp::codegen