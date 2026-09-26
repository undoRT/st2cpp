/**
 * @file LibrarySymbolImporter.cpp
 * @brief LibrarySymbolImporter implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/LibrarySymbolImporter.h"

namespace st2cpp::semantic {

namespace {

/**
 * @brief Canonical IEC name of an elementary base type.
 * @param bt The base type enumerator
 * @return The uppercase type name used by the built-in type table
 */
const char* baseTypeName(BaseType bt)
{
   switch (bt) {
   case BaseType::BOOL: return "BOOL";
   case BaseType::SINT: return "SINT";
   case BaseType::INT: return "INT";
   case BaseType::DINT: return "DINT";
   case BaseType::LINT: return "LINT";
   case BaseType::USINT: return "USINT";
   case BaseType::UINT: return "UINT";
   case BaseType::UDINT: return "UDINT";
   case BaseType::ULINT: return "ULINT";
   case BaseType::REAL: return "REAL";
   case BaseType::LREAL: return "LREAL";
   case BaseType::BYTE: return "BYTE";
   case BaseType::WORD: return "WORD";
   case BaseType::DWORD: return "DWORD";
   case BaseType::LWORD: return "LWORD";
   case BaseType::STRING: return "STRING";
   case BaseType::WSTRING: return "WSTRING";
   case BaseType::TIME: return "TIME";
   case BaseType::DATE: return "DATE";
   case BaseType::DT: return "DT";
   case BaseType::TOD: return "TOD";
   case BaseType::VOID: return "VOID";
   case BaseType::NAMED: return "VOID"; // unreachable for primitives
   }
   return "UNKNOWN";
}

} // namespace

SourceLocation LibrarySymbolImporter::makeLocation() const
{
   SourceLocation loc;
   loc.line = 0;
   loc.column = 0;
   loc.fileName = "<library>";
   return loc;
}

std::string LibrarySymbolImporter::cacheKey(const std::string& libId, const std::string& name)
{
   return st2cpp::library::LibraryDescriptor::makeKey(libId) + "::"
        + st2cpp::library::LibraryDescriptor::makeKey(name);
}

ParamDir LibrarySymbolImporter::paramDirection(st2cpp::library::ParamDirection dir) const
{
   switch (dir) {
   case st2cpp::library::ParamDirection::In: return ParamDir::Input;
   case st2cpp::library::ParamDirection::Out: return ParamDir::Output;
   case st2cpp::library::ParamDirection::InOut: return ParamDir::InOut;
   }
   return ParamDir::Input;
}

const st2cpp::library::LibraryDescriptor* LibrarySymbolImporter::findLibrary(const std::string& id) const
{
   return registry_ ? registry_->get(id) : nullptr;
}

void LibrarySymbolImporter::reportCollision(const std::string& name, const st2cpp::library::LibraryDescriptor& lib)
{
   diag_.addWarning(DiagnosticCode::ExternalSymbolCollision,
      "external symbol '" + name + "' from library '" + lib.id
         + "' is ignored: a symbol with the same name was already imported from an earlier library",
      makeLocation());
}

void LibrarySymbolImporter::import(const st2cpp::library::LibraryRegistry& registry)
{
   registry_ = &registry;
   for (const st2cpp::library::LibraryDescriptor* lib : registry.allOrdered()) {
      if (lib) {
         importLibrary(*lib);
      }
   }
   registry_ = nullptr;
}

void LibrarySymbolImporter::importLibrary(const st2cpp::library::LibraryDescriptor& lib)
{
   for (const auto& e : lib.enums) {
      importEnum(e, lib);
   }
   for (const auto& s : lib.types) {
      importStruct(s, lib);
   }
   for (const auto& f : lib.functions) {
      importFunction(f, lib);
   }
   for (const auto& fb : lib.functionBlocks) {
      importFunctionBlock(fb, lib);
   }
   for (const auto& c : lib.constants) {
      importConstant(c, lib);
   }
   for (const auto& g : lib.globalVariables) {
      importGlobalVariable(g, lib);
   }
}

void LibrarySymbolImporter::importEnum(const st2cpp::library::EnumTypeDef& e, const st2cpp::library::LibraryDescriptor& owner)
{
   const std::string key = cacheKey(owner.id, e.name);
   if (importedTypes_.find(key) != importedTypes_.end()) {
      return;
   }
const SymbolId symId = symTab_.declareExternal(e.name, SymbolKind::Type, 0, owner.id);
    if (symId == 0) {
       reportCollision(e.name, owner);
       return;
    }
    Symbol* sym = symTab_.get(symId);

   TypeInfo typeInfo;
   typeInfo.kind = TypeKind::Enum;
   typeInfo.name = e.name;
   typeInfo.symbolId = symId;
   const TypeId typeId = symTab_.registerType(typeInfo);
   if (sym) {
      sym->typeId = typeId;
   }
   importedTypes_[key] = typeId;

   for (const auto& member : e.members) {
      const SymbolId valSymId = symTab_.declareExternal(member.name, SymbolKind::Enumerator, 0, owner.id);
      if (valSymId == 0) {
         reportCollision(member.name, owner);
         continue;
      }
      if (Symbol* valSym = symTab_.get(valSymId)) {
         valSym->typeId = typeId;
      }
      if (sym) {
         sym->enumerators.push_back(valSymId);
      }
   }
}

void LibrarySymbolImporter::importStruct(const st2cpp::library::StructTypeDef& s, const st2cpp::library::LibraryDescriptor& owner)
{
   const std::string key = cacheKey(owner.id, s.name);
   if (importedTypes_.find(key) != importedTypes_.end()) {
      return;
   }
const SymbolId symId = symTab_.declareExternal(s.name, SymbolKind::Type, 0, owner.id);
    if (symId == 0) {
       reportCollision(s.name, owner);
       return;
    }
    Symbol* sym = symTab_.get(symId);

    TypeInfo typeInfo;
    typeInfo.kind = TypeKind::Struct;
   typeInfo.name = s.name;
   typeInfo.symbolId = symId;
   const TypeId typeId = symTab_.registerType(typeInfo);
   if (sym) {
      sym->typeId = typeId;
   }
   // Cache before members so that recursive references resolve to this type
   // instead of re-entering the import (also guards descriptor cycles).
   importedTypes_[key] = typeId;

   const ScopeId structScope = symTab_.pushExternalScope("STRUCT_" + s.name);
   std::vector<SymbolId> memberIds;
   for (const auto& field : s.fields) {
      const TypeId fieldTypeId = resolveLibraryType(field.type, owner);
      const SymbolId fieldSymId = symTab_.declare(field.name, SymbolKind::StructMember, fieldTypeId);
      if (fieldSymId == 0) {
         diag_.addError(DiagnosticCode::DuplicateDeclaration,
            "duplicate struct member '" + field.name + "' in type '" + s.name + "'", makeLocation());
         continue;
      }
      if (Symbol* fieldSym = symTab_.get(fieldSymId)) {
         fieldSym->typeId = fieldTypeId;
      }
      memberIds.push_back(fieldSymId);
   }
   symTab_.exitScope();

   if (sym) {
      sym->members = std::move(memberIds);
   }
}

void LibrarySymbolImporter::importFunction(const st2cpp::library::FunctionDef& f, const st2cpp::library::LibraryDescriptor& owner)
{
const SymbolId symId = symTab_.declareExternal(f.name, SymbolKind::Function, 0, owner.id);
    if (symId == 0) {
       reportCollision(f.name, owner);
       return;
    }
    Symbol* sym = symTab_.get(symId);
    sym->returnTypeId = resolveLibraryType(f.returnType, owner);

   const ScopeId funcScope = symTab_.pushExternalScope("FUNC_" + f.name);
   std::vector<SymbolId> paramIds;
   for (const auto& param : f.parameters) {
      const TypeId paramTypeId = resolveLibraryType(param.type, owner);
      const SymbolId paramSymId = symTab_.declare(param.name, SymbolKind::Parameter, paramTypeId);
      if (paramSymId == 0) {
         diag_.addError(DiagnosticCode::DuplicateDeclaration,
            "duplicate parameter '" + param.name + "' in function '" + f.name + "'", makeLocation());
         continue;
      }
      if (Symbol* paramSym = symTab_.get(paramSymId)) {
         paramSym->typeId = paramTypeId;
         paramSym->paramDir = paramDirection(param.direction);
         paramSym->hasDefaultValue = param.initValue.kind != st2cpp::library::InitKind::None;
      }
      paramIds.push_back(paramSymId);
   }
   symTab_.exitScope();
   sym->params = std::move(paramIds);
}

void LibrarySymbolImporter::importFunctionBlock(const st2cpp::library::FunctionBlockDef& fb, const st2cpp::library::LibraryDescriptor& owner)
{
const SymbolId symId = symTab_.declareExternal(fb.name, SymbolKind::FunctionBlock, 0, owner.id);
    if (symId == 0) {
       reportCollision(fb.name, owner);
       return;
    }
    Symbol* sym = symTab_.get(symId);

   TypeInfo fbType;
   fbType.kind = TypeKind::FunctionBlock;
   fbType.name = fb.name;
   fbType.symbolId = symId;
   const TypeId typeId = symTab_.registerType(fbType);
   sym->typeId = typeId;

   const ScopeId fbScope = symTab_.pushExternalScope("FB_" + fb.name);
   std::vector<SymbolId> paramIds;
   for (const auto& param : fb.parameters) {
      const TypeId paramTypeId = resolveLibraryType(param.type, owner);
      const SymbolId paramSymId = symTab_.declare(param.name, SymbolKind::Parameter, paramTypeId);
      if (paramSymId == 0) {
         diag_.addError(DiagnosticCode::DuplicateDeclaration,
            "duplicate parameter '" + param.name + "' in function block '" + fb.name + "'", makeLocation());
         continue;
      }
      if (Symbol* paramSym = symTab_.get(paramSymId)) {
         paramSym->typeId = paramTypeId;
         paramSym->paramDir = paramDirection(param.direction);
         paramSym->hasDefaultValue = param.initValue.kind != st2cpp::library::InitKind::None;
      }
      paramIds.push_back(paramSymId);
   }

   // Internal state. The descriptor carries it, so restoring it here is what
   // makes a sibling block's members visible to a consumer: without these
   // symbols a caller could see the interface of an imported block but none of
   // what it holds.
   std::vector<SymbolId> stateIds;
   for (const auto& member : fb.members) {
      const TypeId memberTypeId = resolveLibraryType(member.type, owner);
      const SymbolId memberSymId = symTab_.declare(member.name, SymbolKind::Variable, memberTypeId);
      if (memberSymId == 0) {
         diag_.addError(DiagnosticCode::DuplicateDeclaration,
            "duplicate member '" + member.name + "' in function block '" + fb.name + "'", makeLocation());
         continue;
      }
      if (Symbol* memberSym = symTab_.get(memberSymId)) {
         memberSym->typeId = memberTypeId;
         memberSym->isConstant =
            member.storage == st2cpp::library::FbMemberStorage::Constant;
         memberSym->isRetain = member.storage == st2cpp::library::FbMemberStorage::Retain;
         memberSym->hasDefaultValue = member.initValue.kind != st2cpp::library::InitKind::None;
      }
      stateIds.push_back(memberSymId);
   }
   symTab_.exitScope();
   sym->params = std::move(paramIds);
   // DeclVisitor records the scope on the block symbol; doing the same here is
   // what makes the imported members and methods reachable by scope, the way a
   // block declared in source is.
   sym->scopeId = fbScope;

   // Methods, each with its own nested scope holding its parameters, mirroring
   // how DeclVisitor lays out a block declared in source. An override replaces
   // the inherited declaration instead of being imported next to it.
   std::vector<SymbolId> methodIds;
   std::vector<std::string> methodNames;
   for (const auto& method : fb.methods) {
      const std::string key = SymbolTable::normalizeKey(method.name);
      if (std::find(methodNames.begin(), methodNames.end(), key) != methodNames.end()) {
         continue;
      }
      methodNames.push_back(key);

      const SymbolId methodSymId = symTab_.declare(method.name, SymbolKind::Method);
      if (methodSymId == 0) {
         diag_.addError(DiagnosticCode::DuplicateDeclaration,
            "duplicate method '" + method.name + "' in function block '" + fb.name + "'", makeLocation());
         continue;
      }

      const ScopeId methodScope = symTab_.pushExternalScope("METHOD_" + method.name);
      std::vector<SymbolId> methodParamIds;
      for (const auto& param : method.parameters) {
         const TypeId paramTypeId = resolveLibraryType(param.type, owner);
         const SymbolId paramSymId = symTab_.declare(param.name, SymbolKind::Parameter, paramTypeId);
         if (paramSymId == 0) {
            continue;
         }
         if (Symbol* paramSym = symTab_.get(paramSymId)) {
            paramSym->typeId = paramTypeId;
            paramSym->paramDir = paramDirection(param.direction);
            paramSym->hasDefaultValue = param.initValue.kind != st2cpp::library::InitKind::None;
         }
         methodParamIds.push_back(paramSymId);
      }
      symTab_.exitScope();

      if (Symbol* methodSym = symTab_.get(methodSymId)) {
         methodSym->containingFbId = symId;
         methodSym->params = std::move(methodParamIds);
         methodSym->returnTypeId = resolveLibraryType(method.returnType, owner);
         methodSym->isAbstract = method.isAbstract;
         methodSym->isFinal = method.isFinal;
         methodSym->isOverride = method.isOverride;
         methodSym->scopeId = methodScope;
      }
      methodIds.push_back(methodSymId);
   }
   // DeclVisitor stores an FB's methods in `members`; keep the same shape so a
   // consumer walking the symbol table finds them the same way either way.
   sym->members = std::move(methodIds);

   // The base block, when the descriptor named one, so that inherited
   // parameters and state resolve through the same chain as in source.
   if (!fb.baseType.empty()) {
      const SymbolId baseSymId = symTab_.lookupGlobal(fb.baseType);
      if (baseSymId != 0) {
         sym->baseClassId = baseSymId;
      }
   }
   (void)stateIds;

   // Cache the FB type under its library-qualified key so that repeated
   // references to this FB type (params, members, globals) resolve to the
   // same TypeId instead of re-importing and re-registering a new one.
   importedTypes_[cacheKey(owner.id, fb.name)] = typeId;
}

void LibrarySymbolImporter::importConstant(const st2cpp::library::Constant& c, const st2cpp::library::LibraryDescriptor& owner)
{
const SymbolId symId = symTab_.declareExternal(c.name, SymbolKind::Variable, 0, owner.id);
    if (symId == 0) {
       reportCollision(c.name, owner);
       return;
    }
    if (Symbol* sym = symTab_.get(symId)) {
       sym->typeId = resolveLibraryType(c.type, owner);
       sym->isConstant = true;
    }
}

void LibrarySymbolImporter::importGlobalVariable(const st2cpp::library::GlobalVariable& g, const st2cpp::library::LibraryDescriptor& owner)
{
const SymbolId symId = symTab_.declareExternal(g.name, SymbolKind::Variable, 0, owner.id);
    if (symId == 0) {
       reportCollision(g.name, owner);
       return;
    }
    if (Symbol* sym = symTab_.get(symId)) {
       sym->typeId = resolveLibraryType(g.type, owner);
       sym->isConstant = g.constant;
    }
}

TypeId LibrarySymbolImporter::resolveLibraryType(const st2cpp::library::TypeRef& ref, const st2cpp::library::LibraryDescriptor& owner)
{
   switch (ref.kind) {
   case st2cpp::library::TypeRefKind::Primitive: {
      return resolvePrimitiveType(ref.kind, ref.primitive, owner);
   }
   case st2cpp::library::TypeRefKind::Array: {
      if (!ref.elementType) {
         return 0;
      }
      const TypeId elementTypeId = resolveLibraryType(*ref.elementType, owner);
      TypeInfo arrayType;
      arrayType.kind = TypeKind::Array;
      arrayType.name = "ARRAY";
      arrayType.elementTypeId = elementTypeId;
      arrayType.isNumeric = false;
      ArrayDimInfo dim;
      dim.low = ref.lowerBound;
      dim.high = ref.upperBound;
      dim.isConstant = true;
      arrayType.dimensions.push_back(dim);
      const int count = dim.high - dim.low + 1;
      if (const TypeInfo* elem = symTab_.getType(elementTypeId)) {
         arrayType.sizeInBytes = elem->sizeInBytes * (count > 0 ? count : 0);
      }
      return symTab_.registerArrayType(arrayType);
   }
   case st2cpp::library::TypeRefKind::Named: {
      // A named type belongs either to the declaring library or, when the
      // descriptor pinpoints one, to that specific library.
      const st2cpp::library::LibraryDescriptor* lib = &owner;
      if (!ref.library.empty()) {
         lib = findLibrary(ref.library);
         if (!lib) {
            diag_.addError(DiagnosticCode::InvalidTypeName,
               "unknown external library '" + ref.library + "' referenced by type '" + ref.name
                  + "' of library '" + owner.id + "'", makeLocation());
            return 0;
         }
      }
      const std::string key = cacheKey(lib->id, ref.name);
      auto it = importedTypes_.find(key);
      if (it != importedTypes_.end()) {
         return it->second;
      }
      if (const st2cpp::library::EnumTypeDef* e = lib->findEnum(ref.name)) {
         importEnum(*e, *lib);
         return importedTypes_[key];
      }
      if (const st2cpp::library::StructTypeDef* s = lib->findStruct(ref.name)) {
         importStruct(*s, *lib);
         return importedTypes_[key];
      }
      if (const st2cpp::library::FunctionBlockDef* fb = lib->findFunctionBlock(ref.name)) {
         importFunctionBlock(*fb, *lib);
         return importedTypes_[key];
      }
      diag_.addError(DiagnosticCode::InvalidTypeName,
         "unknown type '" + ref.name + "' in library '" + lib->id + "'",
         makeLocation());
      return 0;
   }
   }
   return 0;
}

TypeId LibrarySymbolImporter::resolvePrimitiveType(st2cpp::library::TypeRefKind kind, BaseType primitive, const st2cpp::library::LibraryDescriptor& owner)
{
   if (kind != st2cpp::library::TypeRefKind::Primitive) {
      return 0;
   }
   (void)owner;
   return symTab_.getTypeIdByName(baseTypeName(primitive));
}

} // namespace st2cpp::semantic