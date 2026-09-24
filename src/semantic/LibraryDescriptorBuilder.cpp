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

namespace st2cpp::semantic {

namespace lib = st2cpp::library;

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const std::string kSchemaVersion = "1.0";

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

void LibraryDescriptorBuilder::Impl::exportPou(const POU& pou)
{
    const std::string entity = pou.kind == POUKind::FUNCTION_BLOCK ? "function block " + pou.name
                                                                   : (pou.kind == POUKind::FUNCTION ? "function " + pou.name
                                                                                                    : "program " + pou.name);

    if (pou.kind == POUKind::PROGRAM) {
        error(entity, "PROGRAM POUs are not representable");
        return;
    }

    std::vector<const VarDecl*> paramDecls;
    std::vector<ParamDir> paramDirs;
    if (!collectParams(pou, entity, paramDecls, paramDirs)) {
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

    if (pou.kind == POUKind::FUNCTION_BLOCK) {
        // Only the interface is exported today; state/implementation is internal.
        // Any of these constructs makes the FB unrepresentable: emit the error
        // and do not export the block.
        bool rejected = false;
        if (!pou.extends.empty()) {
            error(entity, "EXTENDS is not representable");
            rejected = true;
        }
        if (!pou.implements.empty()) {
            error(entity, "IMPLEMENTS is not representable");
            rejected = true;
        }
        if (pou.isAbstract) {
            error(entity, "ABSTRACT function blocks are not representable");
            rejected = true;
        }
        if (pou.isFinal) {
            error(entity, "FINAL function blocks are not representable");
            rejected = true;
        }
        if (!pou.methods.empty()) {
            error(entity, "methods are not representable");
            rejected = true;
        }
        if (rejected) {
            return;
        }
    }

    // Align parameter declarations with their registered symbols by name.
    std::map<std::string, SymbolId> paramById;
    for (SymbolId pid : pouSym->params) {
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
        emitParams(fb.parameters);
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