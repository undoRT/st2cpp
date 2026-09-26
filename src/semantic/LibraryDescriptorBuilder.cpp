/**
 * @file LibraryDescriptorBuilder.cpp
 * @brief Implementation of the semantic-to-descriptor bridge
 *
 * The builder converts a decorated AST + semantic model into a
 * semantic-only LibraryDescriptor (no C++ bindings). See LibraryDescriptorBuilder.h.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/LibraryDescriptorBuilder.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace st2cpp::semantic {

namespace lib = st2cpp::library;

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const std::string kSchemaVersion = "1.1";

/**
 * @brief IEC 61131-3 case-insensitive key (same normalization everywhere).
 */
std::string keyOf(const std::string& name)
{
   std::string key;
   key.reserve(name.size());
   for (unsigned char c : name) {
      key.push_back(static_cast<char>(std::toupper(c)));
   }
   return key;
}

/**
 * @brief Strip the surrounding quotes of a string literal.
 * @details Lexer-kept literals include their delimiters ("'Hello World'",
 * "WSTR#\"hi\""); the loader's scalar text carries the raw string contents,
 * so quotes must be removed here (content only, inner quotes preserved).
 */
std::string scalarStringText(const std::string& raw)
{
   if (raw.size() >= 2) {
      const char f = raw.front();
      const char b = raw.back();
      if ((f == '\'' && b == '\'') || (f == '"' && b == '"')) {
         return raw.substr(1, raw.size() - 2);
      }
   }
   return raw;
}

/**
 * @brief Evaluate an integer literal expression (decimal, optional unary minus).
 * @details Constant graph positions where IEC allows a compile-time integer
 * (explicit enum values) are folded here: LiteralExpr directly, or a UnaryExpr
 * "-" applied to one. Typed literals ("UDINT#5") and hexadecimal forms are
 * accepted by stripping the base prefix; anything else is not a constant.
 */
std::optional<long long> evalIntLiteral(const Expr& e)
{
   bool negate = false;
   const Expr* cur = &e;
   if (const auto* unary = std::get_if<UnaryExpr>(&e.node)) {
      if (unary->op == "-") {
         negate = true;
         cur = unary->operand.get();
      } else {
         return std::nullopt;
      }
   }
   const auto* lit = std::get_if<LiteralExpr>(&cur->node);
   if (!lit) {
      return std::nullopt;
   }
   std::string text = lit->value;
   const size_t hash = text.find('#');
   std::string digits = (hash == std::string::npos) ? text : text.substr(hash + 1);
   if (digits.empty()) {
      return std::nullopt;
   }
   char* end = nullptr;
   long long value = std::strtoll(digits.c_str(), &end, 10);
   if (end == nullptr || *end != '\0') {
      return std::nullopt;
   }
   return negate ? -value : value;
}

lib::ParamDirection paramDirectionOf(ParamDir d)
{
   switch (d) {
   case ParamDir::Output: return lib::ParamDirection::Out;
   case ParamDir::InOut: return lib::ParamDirection::InOut;
   case ParamDir::Input:
   default: return lib::ParamDirection::In;
   }
}

} // namespace

// ---------------------------------------------------------------------------
// LibraryDescriptorBuilder
// ---------------------------------------------------------------------------

/**
 * @brief Internal state shared by every export step.
 */
struct LibraryDescriptorBuilder::Impl {
    const TranslationUnit& tu;
    const SemanticInfo& info;
    const SymbolTable& st;
    const LibraryExportOptions& options;
    LibraryExportResult& result;

    // Collected dependencies: normalized key -> {original id, constraint}
    std::map<std::string, std::pair<std::string, lib::VersionConstraint>> deps;

    // Currently emitted descriptor
    lib::LibraryDescriptor desc;

    Impl(const TranslationUnit& tu_, const SemanticInfo& info_, const LibraryExportOptions& options_,
         LibraryExportResult& result_)
        : tu(tu_), info(info_), st(*info_.symbolTable), options(options_), result(result_)
    {
    }

    // ---- error helpers ----
    void error(const std::string& entity, const std::string& message)
    {
        result.errors.push_back(LibraryExportError{entity, message});
    }

    // ---- dependencies ----
    void addDependency(const std::string& libId)
    {
        if (libId.empty()) {
            return;
        }
        const std::string key = keyOf(libId);
        if (key == keyOf(options.id)) {
            error("dependencies", "library '" + libId + "' cannot be a dependency of itself");
            return;
        }
        if (deps.count(key) != 0) {
            return;
        }
        // Policy lookup is case-insensitive (the dependency's id is normalized
        // just like the descriptor keys, but the caller's map is not).
        lib::VersionConstraint constraint;
        const std::string* rawConstraint = nullptr;
        for (const auto& [k, v] : options.dependencyVersions) {
            if (keyOf(k) == key) {
                rawConstraint = &v;
                break;
            }
        }
        if (rawConstraint == nullptr) {
            error("dependencies", "missing version policy for dependency '" + libId
                  + "' (add it to export options dependencyVersions)");
        } else {
            constraint = lib::VersionConstraint::parse(*rawConstraint);
            if (!constraint.valid) {
                error("dependencies", "invalid version constraint '" + *rawConstraint
                      + "' for dependency '" + libId + "'");
            }
        }
        deps.emplace(key, std::make_pair(libId, std::move(constraint)));
    }

    // ---- type references ----
    bool isExternal(const TypeInfo& ti, std::string& libraryId) const
    {
        if (ti.symbolId == 0) {
            return false;
        }
        const Symbol* sym = st.get(ti.symbolId);
        if (!sym || !sym->isExternal) {
            return false;
        }
        libraryId = sym->externalLibraryId;
        return true;
    }

    /**
     * @brief Convert a semantic TypeId to a descriptor TypeRef.
     * @returns nullopt with `why` set when the type cannot be represented.
     */
    std::optional<lib::TypeRef> typeRefFromTypeId(TypeId typeId, std::string* why)
    {
        const TypeInfo* ti = st.getType(typeId);
        if (!ti) {
            if (why) *why = "unresolved type";
            return std::nullopt;
        }
        lib::TypeRef ref;
        switch (ti->kind) {
        case TypeKind::Elementary:
            if (ti->baseType == BaseType::VOID) {
                if (why) *why = "VOID type is not representable";
                return std::nullopt;
            }
            ref.kind = lib::TypeRefKind::Primitive;
            ref.primitive = ti->baseType;
            return ref;
        case TypeKind::Enum:
        case TypeKind::Struct: {
            ref.kind = lib::TypeRefKind::Named;
            ref.name = ti->name;
            std::string libraryId;
            if (isExternal(*ti, libraryId)) {
                ref.library = libraryId;
                addDependency(libraryId);
            }
            return ref;
        }
        case TypeKind::FunctionBlock: {
            ref.kind = lib::TypeRefKind::Named;
            ref.name = ti->name;
            std::string libraryId;
            if (isExternal(*ti, libraryId)) {
                ref.library = libraryId;
                addDependency(libraryId);
            }
            return ref;
        }
        case TypeKind::Array: {
            if (ti->dimensions.size() > 1) {
                if (why) *why = "multi-dimensional arrays are not representable";
                return std::nullopt;
            }
            if (ti->dimensions.empty()) {
                if (why) *why = "array without dimensions";
                return std::nullopt;
            }
            if (!ti->dimensions[0].isConstant) {
                if (why) *why = "array bounds are not compile-time constants";
                return std::nullopt;
            }
            std::string elemWhy;
            auto elem = typeRefFromTypeId(ti->elementTypeId, &elemWhy);
            if (!elem) {
                if (why) *why = "array element " + elemWhy;
                return std::nullopt;
            }
            ref.kind = lib::TypeRefKind::Array;
            ref.lowerBound = ti->dimensions[0].low;
            ref.upperBound = ti->dimensions[0].high;
            ref.elementType = std::make_shared<lib::TypeRef>(std::move(*elem));
            return ref;
        }
        case TypeKind::Pointer:
            if (why) *why = "POINTER TO types are not representable";
            return std::nullopt;
        case TypeKind::Reference:
            if (why) *why = "REF_TO types are not representable";
            return std::nullopt;
        case TypeKind::Interface:
            if (why) *why = "interface types are not representable";
            return std::nullopt;
        case TypeKind::Void:
            if (why) *why = "VOID type is not representable";
            return std::nullopt;
        case TypeKind::Unknown:
        default:
            if (why) *why = "unresolved type";
            return std::nullopt;
        }
    }

    // ---- initializers ----
    bool checkAstTypeSupported(const TypeRef& ref, const std::string& entity);

    /**
     * @brief Convert an initializer expression to a descriptor InitValue.
     * @details Literals, BOOL literals, unary-minus literals, plain identifiers
     * resolving to an enumerator, array init lists and struct init members are
     * supported. External enumerators register a dependency. Returns nullopt
     * when the expression is not a compile-time initializer.
     */
    std::optional<lib::InitValue> initFromExpr(const Expr& expr, const std::string& entity)
    {
        if (const auto* lit = std::get_if<LiteralExpr>(&expr.node)) {
            lib::InitValue v;
            v.kind = lib::InitKind::Scalar;
            v.scalar = scalarStringText(lit->value);
            return v;
        }
        if (const auto* b = std::get_if<BoolLitExpr>(&expr.node)) {
            lib::InitValue v;
            v.kind = lib::InitKind::Scalar;
            v.scalar = b->value ? "true" : "false";
            return v;
        }
        if (const auto* un = std::get_if<UnaryExpr>(&expr.node)) {
            if (un->op == "-") {
                auto inner = initFromExpr(*un->operand, entity);
                if (inner && inner->kind == lib::InitKind::Scalar) {
                    inner->scalar = "-" + inner->scalar;
                    return inner;
                }
            }
            return std::nullopt;
        }
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node)) {
            SymbolId sid = ident->symbolId;
            if (sid == 0) {
                sid = st.lookupGlobal(ident->name);
            }
            const Symbol* sym = st.get(sid);
            if (!sym || sym->kind != SymbolKind::Enumerator) {
                return std::nullopt;
            }
            if (sym->isExternal && !sym->externalLibraryId.empty()) {
                addDependency(sym->externalLibraryId);
            }
            lib::InitValue v;
            v.kind = lib::InitKind::Scalar;
            v.scalar = sym->name;
            return v;
        }
        if (const auto* arr = std::get_if<ArrayInitExpr>(&expr.node)) {
            lib::InitValue v;
            v.kind = lib::InitKind::List;
            for (const auto& el : arr->elements) {
                auto item = initFromExpr(*el, entity);
                if (!item) {
                    return std::nullopt;
                }
                v.list.push_back(std::move(*item));
            }
            return v;
        }
        if (const auto* sInit = std::get_if<StructInitExpr>(&expr.node)) {
            lib::InitValue v;
            v.kind = lib::InitKind::Struct;
            for (const auto& m : sInit->members) {
                auto item = initFromExpr(*m.value, entity);
                if (!item) {
                    return std::nullopt;
                }
                lib::InitValue::StructEntry entry;
                entry.member = m.member;
                entry.value = std::make_shared<lib::InitValue>(std::move(*item));
                v.members.push_back(std::move(entry));
            }
            return v;
        }
        return std::nullopt;
    }

    // ---- sections ----
    // ---- export steps ----
    bool collectParams(const POU& pou, const std::string& entity,
                       std::vector<const VarDecl*>& paramDecls,
                       std::vector<ParamDir>& paramDirs);
    void exportEnum(const EnumType& et);
    void exportStruct(const StructType& st);
    void exportGlobals();
    void exportPou(const POU& pou);
    void exportInterface(const Interface& iface);
    void exportTypeAlias(const TypeAlias& alias);

    // ---- function block members ----
    // The base chain of a function block, root ancestor first, excluding the
    // block itself. Walking it lets a member or method be emitted once, with
    // the block that actually declares it.
    std::vector<const Symbol*> fbAncestors(const Symbol* fbSym) const;
    const POU* fbAstFor(const Symbol* fbSym) const;
    SymbolId lookupInScope(ScopeId scopeId, const std::string& name) const;
    void emitFbMembers(const POU& pou, const Symbol* pouSym, lib::FunctionBlockDef& fb);
    void emitFbMethods(const POU& pou, lib::FunctionBlockDef& fb);
    lib::FbMemberStorage storageOf(const POU& pou, const std::string& memberName) const;
    lib::FbMethodVisibility visibilityOf(const Method& method) const;
    void noteExternalDependency(TypeId typeId);

    void runMetadataValidation()
    {
        if (options.id.empty()) {
            error("options", "library id must not be empty");
        }
        if (options.name.empty()) {
            error("options", "library name must not be empty");
        }
        if (options.version.empty()) {
            error("options", "library version must not be empty");
        } else if (!lib::Version::parse(options.version).valid) {
            error("options", "invalid semantic version '" + options.version + "'");
        }
    }

    LibraryExportResult run()
    {
        desc.schemaVersion = kSchemaVersion;
        desc.id = options.id;
        desc.name = options.name;
        desc.version = options.version;
        desc.description = options.description;
        runMetadataValidation();

        // Types first (enums then structs), in declaration order.
        for (const auto& e : tu.enums) {
            exportEnum(e);
        }
        for (const auto& s : tu.structs) {
            exportStruct(s);
        }
        // Globals, then POUs.
        exportGlobals();
        for (const auto& pou : tu.pous) {
            exportPou(pou);
        }
        // Explicitly rejected constructs (never silent).
        for (const auto& iface : tu.interfaces) {
            exportInterface(iface);
        }
        for (const auto& alias : tu.typeAliases) {
            exportTypeAlias(alias);
        }

        // Dependencies, sorted by normalized id.
        for (const auto& [key, pair] : deps) {
            (void)key;
            lib::Dependency d;
            d.id = pair.first;
            d.version = pair.second;
            desc.dependencies.push_back(std::move(d));
        }

        result.descriptor = std::move(desc);
        return std::move(result);
    }
};

// ---------------------------------------------------------------------------
// Implementation steps
// ---------------------------------------------------------------------------

/**
 * @brief Reject syntactic type forms the descriptor cannot carry.
 * @details STRING[n] / WSTRING[n] lengths and pointers/refs survive only in the
 * AST (the semantic model folds them away), so the AST TypeRef is guarded.
 */
bool LibraryDescriptorBuilder::Impl::checkAstTypeSupported(const TypeRef& ref, const std::string& entity)
{
    bool ok = true;
    if (ref.stringLen.has_value()) {
        error(entity, "STRING[n]/WSTRING[n] lengths are not representable");
        ok = false;
    }
    if (ref.isPointer) {
        error(entity, "POINTER TO types are not representable");
        ok = false;
    }
    if (ref.isRefTo) {
        error(entity, "REF_TO types are not representable");
        ok = false;
    }
    return ok;
}

/**
 * @brief Validate the POU variable sections and collect parameter declarations.
 * @details FIELD-like interface sections (INPUT/OUTPUT/IN_OUT) are collected for
 * the parameter export; VAR/VAR_TEMP are ignored as implementation state;
 * VAR_EXTERNAL/VAR_GLOBAL/AT/RETAIN/attributes are rejected. The direction is
 * captured alongside each declaration, ordered by declaration sequence.
 */

void LibraryDescriptorBuilder::Impl::exportEnum(const EnumType& et)
{
    const std::string entity = "enum " + et.name;
    const Symbol* enumSym = st.get(st.lookupGlobal(et.name));
    if (!enumSym || enumSym->kind != SymbolKind::Type) {
        error(entity, "semantic symbol not found");
        return;
    }

    lib::EnumTypeDef def;
    def.name = et.name;
    // The descriptor requires an explicit base type; our ST dialect only
    // supports plain ENUM which by IEC 61131-3 defaults to INT.
    def.baseType.kind = lib::TypeRefKind::Primitive;
    def.baseType.primitive = BaseType::INT;

    long long next = 0;
    for (const auto& enumerator : et.enumerators) {
        long long value = next;
        if (enumerator.value) {
            auto folded = evalIntLiteral(*enumerator.value);
            if (!folded) {
                error(entity + "." + enumerator.name, "explicit value is not a decimal integer literal");
            } else {
                value = *folded;
            }
        }
        lib::EnumMember member;
        member.name = enumerator.name;
        member.value = static_cast<int>(value);
        def.members.push_back(std::move(member));
        next = value + 1;
    }

    if (!def.members.empty()) {
        lib::InitValue init;
        init.kind = lib::InitKind::Scalar;
        init.scalar = def.members.front().name;
        def.initValue = std::move(init);
    }

    desc.enums.push_back(std::move(def));
}

void LibraryDescriptorBuilder::Impl::exportStruct(const StructType& stt)
{
    const std::string entity = "struct " + stt.name;
    const Symbol* structSym = st.get(st.lookupGlobal(stt.name));
    if (!structSym || structSym->kind != SymbolKind::Type) {
        error(entity, "semantic symbol not found");
        return;
    }

    lib::StructTypeDef def;
    def.name = stt.name;

    std::map<std::string, SymbolId> memberByName;
    for (SymbolId mid : structSym->members) {
        if (const Symbol* ms = st.get(mid)) {
            memberByName[keyOf(ms->name)] = mid;
        }
    }

    for (const auto& member : stt.members) {
        const std::string memberEntity = entity + "." + member.name;
        bool ok = checkAstTypeSupported(member.type, memberEntity);
        lib::StructField field;
        field.name = member.name;
        std::string why;
        auto symIt = memberByName.find(keyOf(member.name));
        const Symbol* memberSym = (symIt != memberByName.end()) ? st.get(symIt->second) : nullptr;
        if (!memberSym) {
            error(memberEntity, "semantic member not found");
            continue;
        }
        auto ref = typeRefFromTypeId(memberSym->typeId, &why);
        if (!ref) {
            error(memberEntity, why);
            continue;
        }
        field.type = std::move(*ref);
        if (member.initialValue) {
            auto init = initFromExpr(*member.initialValue, memberEntity);
            if (!init) {
                error(memberEntity, "member initializer is not a compile-time initializer");
            } else {
                field.initValue = std::move(*init);
            }
        }
        (void)ok;
        def.fields.push_back(std::move(field));
    }

    desc.types.push_back(std::move(def));
}

void LibraryDescriptorBuilder::Impl::exportGlobals()
{
    for (const auto& sec : tu.globals) {
        if (!sec.attributes.empty()) {
            error("globals", "variable section attributes are not representable");
        }
        for (const auto& decl : sec.decls) {
            const std::string entity = "global " + decl.name;
            if (decl.isRetain) {
                error(entity, "RETAIN variables are not representable");
            }
            if (!decl.atAddress.empty()) {
                error(entity, "AT (externally mapped) variables are not representable");
                continue; // cannot type-resolve an address-mapped decl reliably
            }
            const Symbol* gsym = st.get(st.lookupGlobal(decl.name));
            if (!gsym || gsym->kind != SymbolKind::Variable) {
                error(entity, "semantic symbol not found");
                continue;
            }
            bool ok = checkAstTypeSupported(decl.type, entity);
            std::string why;
            auto ref = typeRefFromTypeId(gsym->typeId, &why);
            if (!ref) {
                error(entity, why);
                continue;
            }
            (void)ok;

            if (decl.isConstant) {
                lib::Constant c;
                c.name = decl.name;
                c.type = std::move(*ref);
                if (!decl.initialValue) {
                    error(entity, "a constant requires an explicit value");
                    continue;
                }
                auto value = initFromExpr(*decl.initialValue, entity);
                if (!value) {
                    error(entity, "constant value is not a compile-time literal");
                    continue;
                }
                if (value->kind != lib::InitKind::Scalar) {
                    error(entity, "constant value must be a scalar");
                    continue;
                }
                c.value = std::move(*value);
                desc.constants.push_back(std::move(c));
                continue;
            }

            lib::GlobalVariable g;
            g.name = decl.name;
            g.scope = "global";
            g.type = std::move(*ref);
            if (decl.initialValue) {
                auto init = initFromExpr(*decl.initialValue, entity);
                if (!init) {
                    error(entity, "initializer is not a compile-time initializer");
                } else {
                    g.initValue = std::move(*init);
                }
            }
            desc.globalVariables.push_back(std::move(g));
        }
    }
}

namespace {

} // namespace

/**
 * @brief Collect parameter declarations (INPUT/OUTPUT/IN_OUT) of a POU.
 * @details Returns false when the POU cannot be exported at all (unsupported
 * section kinds, attributes, RETAIN, AT). Interface parameter decls are
 * returned in declaration order together with their section direction.
 */
bool LibraryDescriptorBuilder::Impl::collectParams(const POU& pou, const std::string& entity,
                                                   std::vector<const VarDecl*>& paramDecls,
                                                   std::vector<ParamDir>& paramDirs)
{
    bool ok = true;
    for (const auto& sec : pou.varSections) {
        if (!sec.attributes.empty()) {
            error(entity + " (parameter section)", "variable section attributes are not representable");
            ok = false;
        }
        switch (sec.kind) {
        case VarKind::INPUT:
        case VarKind::OUTPUT:
        case VarKind::IN_OUT:
            for (const auto& decl : sec.decls) {
                paramDecls.push_back(&decl);
                paramDirs.push_back([&]() {
                    switch (sec.kind) {
                    case VarKind::OUTPUT: return ParamDir::Output;
                    case VarKind::IN_OUT: return ParamDir::InOut;
                    default: return ParamDir::Input;
                    }
                }());
            }
            break;
        case VarKind::VAR:
        case VarKind::TEMP:
            // Local implementation state: intentionally not exported.
            break;
        case VarKind::EXTERNAL:
            error(entity, "VAR_EXTERNAL declarations are not representable");
            ok = false;
            break;
        case VarKind::GLOBAL:
            error(entity, "VAR_GLOBAL declarations inside a POU are not representable");
            ok = false;
            break;
        }
    }
    return ok;
}

std::vector<const Symbol*> LibraryDescriptorBuilder::Impl::fbAncestors(const Symbol* fbSym) const
{
   std::vector<const Symbol*> chain;
   if (fbSym == nullptr) {
      return chain;
   }
   // Follow baseClassId upwards, then reverse, so the root ancestor comes
   // first. The visited set guards against a cycle that slipped past the
   // analyzer's topological sort.
   std::set<SymbolId> visited;
   const Symbol* cursor = fbSym;
   while (cursor != nullptr && cursor->baseClassId != 0 && visited.insert(cursor->id).second) {
      const Symbol* base = st.get(cursor->baseClassId);
      if (base == nullptr) {
         break;
      }
      chain.push_back(base);
      cursor = base;
   }
   std::reverse(chain.begin(), chain.end());
   return chain;
}

SymbolId LibraryDescriptorBuilder::Impl::lookupInScope(ScopeId scopeId, const std::string& name) const
{
   if (scopeId == 0) {
      return 0;
   }
   const Scope* scope = st.getScope(scopeId);
   if (scope == nullptr) {
      return 0;
   }
   auto it = scope->symbols.find(SymbolTable::normalizeKey(name));
   return (it != scope->symbols.end()) ? it->second : 0;
}

const POU* LibraryDescriptorBuilder::Impl::fbAstFor(const Symbol* fbSym) const{
   if (fbSym == nullptr) {
      return nullptr;
   }
   for (const auto& candidate : tu.pous) {
      if (candidate.kind == POUKind::FUNCTION_BLOCK && keyOf(candidate.name) == keyOf(fbSym->name)) {
         return &candidate;
      }
   }
   return nullptr;
}

lib::FbMemberStorage LibraryDescriptorBuilder::Impl::storageOf(const POU& pou, const std::string& memberName) const
{
   for (const auto& sec : pou.varSections) {
      // INPUT/OUTPUT/IN_OUT describe the call interface, not state.
      if (sec.kind == VarKind::INPUT || sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
         continue;
      }
      for (const auto& decl : sec.decls) {
         if (keyOf(decl.name) != keyOf(memberName)) {
            continue;
         }
         if (decl.isConstant) {
            return lib::FbMemberStorage::Constant;
         }
         if (decl.isRetain) {
            return lib::FbMemberStorage::Retain;
         }
         return (sec.kind == VarKind::TEMP) ? lib::FbMemberStorage::Temp : lib::FbMemberStorage::Var;
      }
   }
   return lib::FbMemberStorage::Var;
}

lib::FbMethodVisibility LibraryDescriptorBuilder::Impl::visibilityOf(const Method& method) const
{
   switch (method.visibility) {
      case MethodVisibility::PRIVATE: return lib::FbMethodVisibility::Private;
      case MethodVisibility::PROTECTED: return lib::FbMethodVisibility::Protected;
      case MethodVisibility::PUBLIC: break;
   }
   return lib::FbMethodVisibility::Public;
}

void LibraryDescriptorBuilder::Impl::noteExternalDependency(TypeId typeId)
{
   if (typeId == 0) {
      return;
   }
   const TypeInfo* ti = st.getType(typeId);
   if (ti == nullptr) {
      return;
   }
   std::string libId;
   if (isExternal(*ti, libId)) {
      addDependency(libId);
   }
}

void LibraryDescriptorBuilder::Impl::emitFbMembers(const POU& pou, const Symbol* pouSym, lib::FunctionBlockDef& fb)
{
   if (pouSym == nullptr) {
      return;
   }
   const std::string entity = "function block " + pou.name;

   // Names declared by this block itself: a base member with the same name is
   // shadowed and must not be emitted twice.
   std::set<std::string> ownNames;
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::INPUT || sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
         continue;
      }
      for (const auto& decl : sec.decls) {
         ownNames.insert(keyOf(decl.name));
      }
   }

   // Emit inherited state first (root ancestor downwards) so the resulting
   // order matches the declaration order of the flattened instance.
   std::set<std::string> emitted = ownNames;
   for (const Symbol* ancestor : fbAncestors(pouSym)) {
      const POU* ancestorAst = fbAstFor(ancestor);
      for (const auto& sec : ancestorAst->varSections) {
         if (sec.kind == VarKind::INPUT || sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
            continue;
         }
         for (const auto& decl : sec.decls) {
            if (!emitted.insert(keyOf(decl.name)).second) {
               continue;
            }
            const SymbolId symId = ancestor->scopeId != 0 ? lookupInScope(ancestor->scopeId, decl.name) : 0;
            const Symbol* sym = (symId != 0) ? st.get(symId) : nullptr;
            if (sym == nullptr) {
               continue;
            }
            lib::FbMember member;
            member.name = decl.name;
            member.storage = ancestorAst != nullptr ? storageOf(*ancestorAst, decl.name) : lib::FbMemberStorage::Var;
            member.inherited = true;
            member.declaredIn = ancestor->name;
            std::string why;
            auto ref = typeRefFromTypeId(sym->typeId, &why);
            if (!ref) {
               error(entity + ".member " + decl.name, why);
               continue;
            }
            member.type = std::move(*ref);
            if (decl.initialValue) {
               auto init = initFromExpr(*decl.initialValue, entity + ".member " + decl.name);
               if (init) {
                  member.initValue = std::move(*init);
               }
            }
            noteExternalDependency(sym->typeId);
            fb.members.push_back(std::move(member));
         }
      }
   }

   // Then the block's own state, in declaration order.
   for (const auto& sec : pou.varSections) {
      if (sec.kind == VarKind::INPUT || sec.kind == VarKind::OUTPUT || sec.kind == VarKind::IN_OUT) {
         continue;
      }
      for (const auto& decl : sec.decls) {
         const std::string memberEntity = entity + ".member " + decl.name;
         const SymbolId symId = lookupInScope(pouSym->scopeId, decl.name);
         const Symbol* sym = (symId != 0) ? st.get(symId) : nullptr;
         if (sym == nullptr) {
            error(memberEntity, "semantic member symbol not found");
            continue;
         }
         lib::FbMember member;
         member.name = decl.name;
         member.storage = storageOf(pou, decl.name);
         std::string why;
         auto ref = typeRefFromTypeId(sym->typeId, &why);
         if (!ref) {
            error(memberEntity, why);
            continue;
         }
         member.type = std::move(*ref);
         if (decl.initialValue) {
            auto init = initFromExpr(*decl.initialValue, memberEntity);
            if (!init) {
               error(memberEntity, "default value is not a compile-time literal");
            } else {
               member.initValue = std::move(*init);
            }
         }
         noteExternalDependency(sym->typeId);
         fb.members.push_back(std::move(member));
      }
   }
}

void LibraryDescriptorBuilder::Impl::emitFbMethods(const POU& pou, lib::FunctionBlockDef& fb)
{
   const std::string entity = "function block " + pou.name;
   const Symbol* pouSym = st.get(st.lookupGlobal(pou.name));
   if (pouSym == nullptr) {
      return;
   }

   // A method symbol carries its resolved return type and its parameter symbols
   // in declaration order, so both come from the symbol table rather than from
   // re-resolving the AST type references here.
   auto findMethodSym = [&](const Symbol* fb, const std::string& name) -> const Symbol* {
      if (fb == nullptr) {
         return nullptr;
      }
      for (SymbolId memberId : fb->members) {
         const Symbol* member = st.get(memberId);
         if (member != nullptr && member->kind == SymbolKind::Method && keyOf(member->name) == keyOf(name)) {
            return member;
         }
      }
      return nullptr;
   };

   // Names declared by this block itself. Seeding the set with them makes the
   // base walk skip anything the block redeclares, so an override is emitted
   // once, as the derived declaration.
   std::set<std::string> emitted;
   for (const auto& method : pou.methods) {
      emitted.insert(keyOf(method.name));
   }

   auto emitFrom = [&](const POU& owner, const Symbol* ownerSym, bool inherited) {
      for (const auto& method : owner.methods) {
         // A method the block overrides replaces the inherited one, so the
         // derived declaration is the only one emitted.
         if (!emitted.insert(keyOf(method.name)).second) {
            continue;
         }
         const std::string methodEntity = entity + ".method " + method.name;
         const Symbol* methodSym = findMethodSym(ownerSym, method.name);
         if (methodSym == nullptr) {
            error(methodEntity, "semantic method symbol not found");
            continue;
         }

         lib::FbMethodDef def;
         def.name = method.name;
         def.visibility = visibilityOf(method);
         def.isAbstract = method.isAbstract;
         def.isFinal = method.isFinal;
         def.isOverride = method.isOverride;
         def.inherited = inherited;
         if (inherited) {
            def.declaredIn = owner.name;
         }

         if (method.returnType.base == BaseType::VOID || methodSym->returnTypeId == 0) {
            def.returnType.kind = lib::TypeRefKind::Primitive;
            def.returnType.name = "VOID";
         } else {
            std::string why;
            auto ret = typeRefFromTypeId(methodSym->returnTypeId, &why);
            if (!ret) {
               error(methodEntity, "return type " + why);
               continue;
            }
            def.returnType = std::move(*ret);
            noteExternalDependency(methodSym->returnTypeId);
         }

         for (SymbolId paramId : methodSym->params) {
            const Symbol* paramSym = st.get(paramId);
            if (paramSym == nullptr) {
               continue;
            }
            lib::FunParam param;
            param.name = paramSym->name;
            switch (paramSym->paramDir) {
               case ParamDir::Output: param.direction = lib::ParamDirection::Out; break;
               case ParamDir::InOut: param.direction = lib::ParamDirection::InOut; break;
               case ParamDir::None:
               case ParamDir::Input: param.direction = lib::ParamDirection::In; break;
            }
            std::string why;
            auto paramRef = typeRefFromTypeId(paramSym->typeId, &why);
            if (!paramRef) {
               error(methodEntity + ".parameter " + paramSym->name, why);
               continue;
            }
            param.type = std::move(*paramRef);
            noteExternalDependency(paramSym->typeId);
            def.parameters.push_back(std::move(param));
         }

         // Default values live on the AST parameter, matched by name.
         for (size_t i = 0; i < def.parameters.size() && i < method.parameters.size(); ++i) {
            const auto& astParam = method.parameters[i];
            if (!astParam.initialValue) {
               continue;
            }
            auto init = initFromExpr(*astParam.initialValue, methodEntity);
            if (init) {
               def.parameters[i].initValue = std::move(*init);
            }
         }

         fb.methods.push_back(std::move(def));
      }
   };

   // Base blocks first (root ancestor downwards), then the block's own.
   // Seeding `emitted` with the block's own method names makes the base walk
   // skip anything the block overrides or redeclares, so an override is
   // emitted once, as the derived declaration. The set is cleared afterwards so
   // the block's own methods are emitted unconditionally.
   for (const Symbol* ancestor : fbAncestors(pouSym)) {
      if (const POU* ancestorAst = fbAstFor(ancestor)) {
         emitFrom(*ancestorAst, ancestor, true);
      }
   }
   emitted.clear();
   emitFrom(pou, pouSym, false);
}

void LibraryDescriptorBuilder::Impl::exportPou(const POU& pou)
{
    const std::string entity = pou.kind == POUKind::FUNCTION_BLOCK ? "function block " + pou.name
                                                                   : (pou.kind == POUKind::FUNCTION ? "function " + pou.name
                                                                                                    : "program " + pou.name);

    if (pou.kind == POUKind::PROGRAM) {
        error(entity, "PROGRAM POUs are not representable");
        return;
    }

    const Symbol* pouSym = st.get(st.lookupGlobal(pou.name));
    if (!pouSym) {
        error(entity, "semantic symbol not found");
        return;
    }
    if (pouSym->kind != (pou.kind == POUKind::FUNCTION ? SymbolKind::Function : SymbolKind::FunctionBlock)) {
        error(entity, "semantic symbol kind mismatch");
        return;
    }

    // A function block is callable with the parameters it inherits from its
    // base blocks, so the exported interface must include them. Bases come
    // first, keeping the base interface as the prefix positional calls bind to.
    std::vector<const POU*> paramSources;
    if (pou.kind == POUKind::FUNCTION_BLOCK) {
        for (const Symbol* ancestor : fbAncestors(pouSym)) {
            if (const POU* ancestorAst = fbAstFor(ancestor)) {
                paramSources.push_back(ancestorAst);
            }
        }
    }
    paramSources.push_back(&pou);

    std::vector<const VarDecl*> paramDecls;
    std::vector<ParamDir> paramDirs;
    for (const POU* source : paramSources) {
        if (!collectParams(*source, entity, paramDecls, paramDirs)) {
            return;
        }
    }
    // Which block declares each parameter name, so an inherited parameter can
    // be marked with the block it comes from. A redeclaration by this block
    // overwrites the base, matching the dedup below.
    std::map<std::string, const POU*> paramOwnerByName;
    for (const POU* source : paramSources) {
        for (const auto& sec : source->varSections) {
            if (sec.kind != VarKind::INPUT && sec.kind != VarKind::OUTPUT && sec.kind != VarKind::IN_OUT) {
                continue;
            }
            for (const auto& decl : sec.decls) {
                paramOwnerByName[keyOf(decl.name)] = source;
            }
        }
    }

    // A parameter redeclared by the block replaces the inherited declaration.
    {
        std::set<std::string> seen;
        std::vector<const VarDecl*> keptDecls;
        std::vector<ParamDir> keptDirs;
        for (size_t i = paramDecls.size(); i-- > 0;) {
            if (seen.insert(keyOf(paramDecls[i]->name)).second) {
                keptDecls.push_back(paramDecls[i]);
                keptDirs.push_back(paramDirs[i]);
            }
        }
        std::reverse(keptDecls.begin(), keptDecls.end());
        std::reverse(keptDirs.begin(), keptDirs.end());
        paramDecls = std::move(keptDecls);
        paramDirs = std::move(keptDirs);
    }

    // Align parameter declarations with their registered symbols by name. The
    // lookup spans the whole inheritance chain, so an inherited declaration
    // finds the symbol the base block registered.
    std::map<std::string, SymbolId> paramById;
    const std::vector<SymbolId> declaredParams =
        (pou.kind == POUKind::FUNCTION_BLOCK) ? st.effectiveParams(pouSym->id) : pouSym->params;
    for (SymbolId pid : declaredParams) {
        if (const Symbol* ps = st.get(pid)) {
            paramById[keyOf(ps->name)] = pid;
        }
    }

    auto emitParams = [&](std::vector<lib::FunParam>& out) {
        for (size_t i = 0; i < paramDecls.size(); ++i) {
            const VarDecl& decl = *paramDecls[i];
            const std::string paramEntity = entity + ".parameter " + decl.name;
            bool typeOk = checkAstTypeSupported(decl.type, paramEntity);
            auto symIt = paramById.find(keyOf(decl.name));
            const Symbol* ps = (symIt != paramById.end()) ? st.get(symIt->second) : nullptr;
            if (!ps) {
                error(paramEntity, "semantic parameter symbol not found");
                continue;
            }
            lib::FunParam param;
            param.name = decl.name;
            std::string why;
            auto ref = typeRefFromTypeId(ps->typeId, &why);
            if (!ref) {
                error(paramEntity, why);
                continue;
            }
            param.type = std::move(*ref);
            param.direction = paramDirectionOf(ps->paramDir != ParamDir::None ? ps->paramDir : paramDirs[i]);
            // Mark the parameters that come from a base block, and say which.
            auto ownerIt = paramOwnerByName.find(keyOf(decl.name));
            if (ownerIt != paramOwnerByName.end() && ownerIt->second != &pou) {
                param.inherited = true;
                param.declaredIn = ownerIt->second->name;
            }
            if (decl.initialValue) {
                auto init = initFromExpr(*decl.initialValue, paramEntity);
                if (!init) {
                    error(paramEntity, "default value is not a compile-time literal");
                } else {
                    param.initValue = std::move(*init);
                }
            }
            (void)typeOk;
            out.push_back(std::move(param));
        }
    };

    if (pou.kind == POUKind::FUNCTION) {
        if (pouSym->returnTypeId == 0) {
            error(entity, "a function requires a return type");
            return;
        }
        bool retOk = checkAstTypeSupported(pou.returnType, entity + ".return type");
        std::string why;
        auto retRef = typeRefFromTypeId(pouSym->returnTypeId, &why);
        if (!retRef) {
            error(entity, "return type " + why);
            return;
        }
        (void)retOk;
        lib::FunctionDef f;
        f.name = pou.name;
        f.returnType = std::move(*retRef);
        emitParams(f.parameters);
        desc.functions.push_back(std::move(f));
    } else {
        lib::FunctionBlockDef fb;
        fb.name = pou.name;
        fb.isAbstract = pou.isAbstract;
        fb.isFinal = pou.isFinal;
        emitParams(fb.parameters);

        // The base block and the implemented interfaces are references by name:
        // they are described by their own entries, so a consumer can resolve
        // them against the same descriptor.
        if (!pou.extends.empty()) {
            fb.baseType = pou.extends;
            // A base block declared in another library is a real dependency
            // edge; one declared in this same library is not.
            if (const Symbol* baseSym = st.get(st.lookupGlobal(pou.extends))) {
               noteExternalDependency(baseSym->typeId);
            }
        }
        for (const auto& iface : pou.implements) {
            fb.interfaces.push_back(iface);
        }

        emitFbMembers(pou, pouSym, fb);
        emitFbMethods(pou, fb);

        desc.functionBlocks.push_back(std::move(fb));
    }
}

void LibraryDescriptorBuilder::Impl::exportInterface(const Interface& iface)
{
    error("interface " + iface.name, "INTERFACE declarations are not representable");
}

void LibraryDescriptorBuilder::Impl::exportTypeAlias(const TypeAlias& alias)
{
    error("type alias " + alias.name, "named type aliases are not representable");
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

LibraryExportResult LibraryDescriptorBuilder::build(const TranslationUnit& tu,
                                                     const SemanticInfo& info,
                                                     const LibraryExportOptions& options)
{
    LibraryExportResult result;
    if (!info.symbolTable) {
        result.errors.push_back(LibraryExportError{"options", "semantic analysis produced no symbol table"});
        return result;
    }
    Impl impl(tu, info, options, result);
    return impl.run();
}

} // namespace st2cpp::semantic