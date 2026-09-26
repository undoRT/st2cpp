/**
 * @file SymbolTable.h
 * @brief Symbol table with scope management for semantic analysis
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "TypeSystem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cctype>

namespace st2cpp::semantic {

using SymbolId = uint32_t;
using TypeId = uint32_t;
using ScopeId = uint32_t;

enum class SymbolKind {
    Variable,
    Type,
    Function,
    FunctionBlock,
    Program,
    Interface,
    Method,
    Enumerator,
    StructMember,
    Parameter
};

/**
 * @brief Data direction of a POU/method parameter.
 * @details Tells the call checker whether an omitted parameter is legal for
 * FUNCTION calls: VAR_INPUT may only be omitted when it carries an initial
 * value, while VAR_OUTPUT and VAR_IN_OUT parameters never require a caller to
 * supply a value.
 */
enum class ParamDir {
    None,   // not a parameter section (plain VAR / global)
    Input,  // VAR_INPUT
    Output, // VAR_OUTPUT
    InOut   // VAR_IN_OUT
};

struct Symbol {
    SymbolId id = 0;
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    TypeId typeId = 0;
    ScopeId scopeId = 0;
    SymbolId parentScopeId = 0;
    std::vector<SymbolId> params;
    TypeId returnTypeId = 0;
    /// Where the declaration of this symbol starts, so a diagnostic about it can
    /// point at the source. Zero when the declaration carried no position.
    uint32_t line = 0;
    uint32_t col = 0;
    /// Source file the declaration came from. Empty when unknown.
    std::string fileName;
    bool isAbstract = false;
    bool isFinal = false;
    bool isOverride = false;
    std::vector<SymbolId> members;
    std::vector<SymbolId> enumerators;
    SymbolId baseClassId = 0;
    std::vector<SymbolId> implementedInterfaces;
    bool isConstant = false;
    bool isRetain = false;
    std::string atAddress;
    SymbolId containingFbId = 0;
    // Parameter metadata: direction of the section the symbol was declared in
    // and whether it carries an initial value (default).
    ParamDir paramDir = ParamDir::None;
    bool hasDefaultValue = false;
    // True when the symbol was imported from an external library descriptor
    // (lives in the external scope, never in the project global scope).
    bool isExternal = false;
    // Id of the owning library descriptor (isExternal == true), verbatim from
    // the LibraryDescriptor. Empty for project-local symbols. Lets the
    // CodeGenerator recover the C++ binding single source of truth without
    // re-resolving the symbol by name (which would be ambiguous on collisions).
    std::string externalLibraryId;
};

struct Scope {
    ScopeId id = 0;
    ScopeId parentId = 0;
    std::string name;
    std::unordered_map<std::string, SymbolId> symbols;
};

class SymbolTable {
public:
    /**
     * @brief IEC 61131-3 identifiers are case-insensitive
     * Normalizes a name to an uppercase key so declarations and lookups match
     * regardless of spelling (e.g. `Name` vs `name`).
     */
    static std::string normalizeKey(const std::string& name) {
        std::string key;
        key.reserve(name.size());
        for (unsigned char c : name) {
            key.push_back(static_cast<char>(std::toupper(c)));
        }
        return key;
    }

    SymbolTable() {
        scopes_.reserve(256);
        symbols_.reserve(4096);
        types_.reserve(256);
        types_.push_back(TypeInfo{});
        symbols_.push_back(Symbol{});
        scopes_.push_back(Scope{});
        Scope globalScope;
        globalScope.id = 1;
        globalScope.parentId = 0;
        globalScope.name = "global";
        scopes_.push_back(std::move(globalScope));
        globalScopeId_ = 1;
        currentScopeId_ = 1;
        registerBuiltins();
        registerIecConversionFunctions();
        // External library symbols (imported from LibraryDescriptor objects) live
        // in a dedicated scope that is never consulted by the project-local
        // lookups: it is reached explicitly as the final resolution fallback.
        Scope externalScope;
        externalScope.id = static_cast<ScopeId>(scopes_.size());
        externalScope.parentId = 0;
        externalScope.name = "external";
        scopes_.push_back(std::move(externalScope));
        externalScopeId_ = externalScope.id;
    }

    // ===== Scope Management =====
    ScopeId pushScope(const std::string& name) {
        ScopeId newId = static_cast<ScopeId>(scopes_.size());
        Scope scope;
        scope.id = newId;
        scope.parentId = currentScopeId_;
        scope.name = name;
        scopes_.push_back(std::move(scope));
        currentScopeId_ = newId;
        return newId;
    }
    void popScope() {
        if (currentScopeId_ != globalScopeId_) {
            currentScopeId_ = scopes_[currentScopeId_].parentId;
        }
    }
    /**
     * @brief Enter an already-created scope so declarations can be added later.
     * @details Used by the declaration visitor's second pass to re-enter scopes
     * opened during header registration (e.g. STRUCT_<name>, INTERFACE_<name>).
     * The previously active scope is saved on an internal stack and restored by
     * exitScope(). Unlike pushScope, the scope is not created anew and keeps its
     * stable ScopeId.
     * @param id The scope to enter
     * @return the entered scope id, or 0 when invalid
     */
    ScopeId enterScope(ScopeId id) {
        if (id == 0 || id >= scopes_.size()) return 0;
        scopeStack_.push_back(currentScopeId_);
        currentScopeId_ = id;
        return id;
    }
    /**
     * @brief Restore the scope active before the matching enterScope.
     * @return true when a scope was restored
     */
    bool exitScope() {
        if (scopeStack_.empty()) return false;
        currentScopeId_ = scopeStack_.back();
        scopeStack_.pop_back();
        return true;
    }
    /**
     * @brief Look up a name only in the global scope (the type namespace).
     * @details Declares the intended namespace for user type names: resolution
     * is not shadowed by variables or struct members carrying the same
     * (case-insensitive, IEC 61131-3) name, e.g. a member `inner : Inner`.
     * @param name The name to look up
     * @return the resolved SymbolId, or 0 when not found
     */
    SymbolId lookupGlobal(const std::string& name) const {
        const Scope* scope = getScope(globalScopeId_);
        if (!scope) return 0;
        auto it = scope->symbols.find(normalizeKey(name));
        return (it != scope->symbols.end()) ? it->second : 0;
    }
    /**
     * @brief Id of the external scope that holds library-imported symbols.
     * @details The external scope is never part of the local/global lookup
     * chain; it is consulted explicitly as the final fallback so that
     * project-local symbols always shadow external ones.
     */
    ScopeId externalScope() const { return externalScopeId_; }
    /**
     * @brief Declare a symbol in the external (library) scope.
     * @details Used by the library-symbol importer. The symbol is flagged
     * isExternal and never shadows a project-local declaration (local lookups
     * do not reach this scope). Returns 0 when the name is already present
     * (two libraries exporting the same symbol), leaving the existing entry
     * untouched.
     * @param name The symbol name
     * @param kind The symbol kind
     * @param typeId The symbol type
     * @param libraryId The id of the owning library descriptor
     * @return The new SymbolId, or 0 on duplicate
     */
    SymbolId declareExternal(const std::string& name, SymbolKind kind, TypeId typeId = 0,
        const std::string& libraryId = "") {
        Scope* scope = getScope(externalScopeId_);
        if (!scope) return 0;
        const std::string key = normalizeKey(name);
        if (scope->symbols.find(key) != scope->symbols.end()) {
            return 0;
        }
        SymbolId newId = static_cast<SymbolId>(symbols_.size());
        Symbol sym;
        sym.id = newId;
        sym.name = name;
        sym.kind = kind;
        sym.typeId = typeId;
        sym.scopeId = externalScopeId_;
        sym.parentScopeId = 0;
        sym.isExternal = true;
        sym.externalLibraryId = libraryId;
        scope->symbols[key] = newId;
        symbols_.push_back(std::move(sym));
        return newId;
    }
    /**
     * @brief Look up a name in the external (library) scope only.
     * @return The resolved SymbolId, or 0 when not found
     */
    SymbolId lookupExternal(const std::string& name) const {
        const Scope* scope = getScope(externalScopeId_);
        if (!scope) return 0;
        auto it = scope->symbols.find(normalizeKey(name));
        return (it != scope->symbols.end()) ? it->second : 0;
    }
    /**
     * @brief Open a child scope of the external scope (STRUCT_<n>, FUNC_<n>,
     * FB_<n>) where the current scope is saved and restored by exitScope().
     * @details Mirrors enterScope semantics: the caller must balance it with
     * exitScope(). Members and parameters declared inside such scopes are not
     * reachable from flat lookups, only through their owning symbol.
     * @param name The scope name
     * @return The new ScopeId
     */
    ScopeId pushExternalScope(const std::string& name) {
        ScopeId newId = static_cast<ScopeId>(scopes_.size());
        Scope scope;
        scope.id = newId;
        scope.parentId = externalScopeId_;
        scope.name = name;
        scopes_.push_back(std::move(scope));
        scopeStack_.push_back(currentScopeId_);
        currentScopeId_ = newId;
        return newId;
    }
    ScopeId currentScope() const { return currentScopeId_; }
    ScopeId globalScope() const { return globalScopeId_; }
    const Scope* getScope(ScopeId id) const {
        if (id == 0 || id >= scopes_.size()) return nullptr;
        return &scopes_[id];
    }
    Scope* getScope(ScopeId id) {
        if (id == 0 || id >= scopes_.size()) return nullptr;
        return &scopes_[id];
    }

    // ===== Symbol Management =====
    SymbolId declare(const std::string& name, SymbolKind kind, TypeId typeId = 0) {
        Scope* scope = getScope(currentScopeId_);
        if (!scope) return 0;
        std::string key = normalizeKey(name);
        auto it = scope->symbols.find(key);
        if (it != scope->symbols.end()) {
            return 0;
        }
        SymbolId newId = static_cast<SymbolId>(symbols_.size());
        Symbol sym;
        sym.id = newId;
        sym.name = name;
        sym.kind = kind;
        sym.typeId = typeId;
        sym.scopeId = currentScopeId_;
        sym.parentScopeId = scope->parentId;
        scope->symbols[key] = newId;
        symbols_.push_back(std::move(sym));
        return newId;
    }
    SymbolId lookup(const std::string& name) const {
        const Scope* scope = getScope(currentScopeId_);
        if (!scope) return 0;
        auto it = scope->symbols.find(normalizeKey(name));
        return (it != scope->symbols.end()) ? it->second : 0;
    }
    SymbolId lookupRecursive(const std::string& name) const {
        std::string key = normalizeKey(name);
        ScopeId scopeId = currentScopeId_;
        while (scopeId != 0) {
            const Scope* scope = getScope(scopeId);
            if (!scope) break;
            auto it = scope->symbols.find(key);
            if (it != scope->symbols.end()) {
                return it->second;
            }
            scopeId = scope->parentId;
        }
        return 0;
    }

    // ===== Symbol/Type Access =====
    TypeId registerArrayType(const TypeInfo& type) {
        TypeId id = static_cast<TypeId>(types_.size());
        TypeInfo t = type;
        t.id = id;
        types_.push_back(std::move(t));
        typeNameToId_[normalizeKey(types_.back().name)] = id;
        return id;
    }
    TypeId registerPointerType(const TypeInfo& type) {
        TypeId id = static_cast<TypeId>(types_.size());
        TypeInfo t = type;
        t.id = id;
        types_.push_back(std::move(t));
        typeNameToId_[normalizeKey(types_.back().name)] = id;
        return id;
    }
    TypeId registerReferenceType(const TypeInfo& type) {
        TypeId id = static_cast<TypeId>(types_.size());
        TypeInfo t = type;
        t.id = id;
        types_.push_back(std::move(t));
        typeNameToId_[normalizeKey(types_.back().name)] = id;
        return id;
    }
    TypeId registerType(const TypeInfo& type) {
        TypeId id = static_cast<TypeId>(types_.size());
        TypeInfo t = type;
        t.id = id;
        types_.push_back(std::move(t));
        typeNameToId_[normalizeKey(types_.back().name)] = id;
        return id;
    }
    /**
     * @brief Register a named type alias (TYPE Name : <type>; END_TYPE).
     * @details An alias is not a type of its own: its name resolves to the SAME
     * canonical TypeId of the underlying type, so every semantic check and the
     * code generator treat `Name` exactly as the aliased type. Duplicate alias
     * names simply overwrite this lookup (the duplicate declaration itself is
     * reported by the caller); keep this in sync with the alias Symbol.
     * @param name The alias name
     * @param typeId The canonical TypeId it resolves to
     * @return the aliased TypeId
     */
    TypeId registerTypeAlias(const std::string& name, TypeId typeId) {
        typeNameToId_[normalizeKey(name)] = typeId;
        return typeId;
    }
    const Symbol* get(SymbolId id) const {
        if (id == 0 || id >= symbols_.size()) return nullptr;
        return &symbols_[id];
    }
    Symbol* get(SymbolId id) {
        if (id == 0 || id >= symbols_.size()) return nullptr;
        return &symbols_[id];
    }

    /**
     * @brief The call interface of a function block, inheritance included.
     * @details `Symbol::params` holds only the parameters declared by that very
     * block, but a block that EXTENDS another one is callable with the base's
     * VAR_INPUT/VAR_OUTPUT too. This returns the effective interface: the
     * parameters inherited from the base chain (root ancestor first, so the
     * base interface stays the prefix that positional calls bind against)
     * followed by the block's own.
     *
     * A parameter redeclared by a derived block replaces the inherited one
     * instead of appearing twice. Cycles in the base chain are guarded against.
     */
    std::vector<SymbolId> effectiveParams(SymbolId fbId) const {
        // Base chain, root ancestor first.
        std::vector<const Symbol*> chain;
        std::unordered_set<SymbolId> visited;
        for (const Symbol* cursor = get(fbId);
             cursor != nullptr && visited.insert(cursor->id).second;
             cursor = (cursor->baseClassId != 0) ? get(cursor->baseClassId) : nullptr) {
            chain.push_back(cursor);
        }
        std::reverse(chain.begin(), chain.end());

        // First pass, most derived block first: the winning declaration of each
        // name is the one in the nearest block that redeclares it.
        std::unordered_map<std::string, SymbolId> winner;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (SymbolId paramId : (*it)->params) {
                const Symbol* param = get(paramId);
                if (param == nullptr) continue;
                const std::string key = normalizeKey(param->name);
                if (winner.count(key) != 0) continue; // a derived block already declared it
                winner.emplace(key, paramId);
            }
        }

        // Second pass, base first, so the base interface stays the prefix that
        // positional calls bind against. Each name is emitted once.
        std::vector<SymbolId> out;
        std::unordered_set<std::string> emitted;
        for (const Symbol* fb : chain) {
            for (SymbolId paramId : fb->params) {
                const Symbol* param = get(paramId);
                if (param == nullptr) continue;
                const std::string key = normalizeKey(param->name);
                // Test the winner first: a shadowed declaration must not mark
                // the name as taken, or the winning one below would be dropped.
                auto win = winner.find(key);
                if (win == winner.end() || win->second != paramId) continue;
                if (!emitted.insert(key).second) continue;
                out.push_back(paramId);
            }
        }
        return out;
    }
    const TypeInfo* getType(TypeId id) const {
        if (id == 0 || id >= types_.size()) return nullptr;
        return &types_[id];
    }
    TypeInfo* getType(TypeId id) {
        if (id == 0 || id >= types_.size()) return nullptr;
        return &types_[id];
    }
    TypeId getTypeIdByName(const std::string& name) const {
        auto it = typeNameToId_.find(normalizeKey(name));
        return (it != typeNameToId_.end()) ? it->second : 0;
    }

    // ===== Iteration =====
    void forEachSymbol(std::function<void(const Symbol&)> fn) const {
        for (const auto& sym : symbols_) {
            if (sym.id != 0) fn(sym);
        }
    }
    void forEachType(std::function<void(TypeId, const TypeInfo&)> fn) const {
        for (TypeId i = 1; i < types_.size(); ++i) {
            fn(i, types_[i]);
        }
    }

    // ===== Built-in Types =====
    void registerBuiltins() {
        registerBuiltin(TypeInfo::makeBool());
        registerBuiltin(TypeInfo::makeInt(BaseType::SINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::INT));
        registerBuiltin(TypeInfo::makeInt(BaseType::DINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::LINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::USINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::UINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::UDINT));
        registerBuiltin(TypeInfo::makeInt(BaseType::ULINT));
        registerBuiltin(TypeInfo::makeReal(BaseType::REAL));
        registerBuiltin(TypeInfo::makeReal(BaseType::LREAL));
        registerBuiltin(TypeInfo::makeBitString(BaseType::BYTE));
        registerBuiltin(TypeInfo::makeBitString(BaseType::WORD));
        registerBuiltin(TypeInfo::makeBitString(BaseType::DWORD));
        registerBuiltin(TypeInfo::makeBitString(BaseType::LWORD));
        registerBuiltin(TypeInfo::makeTime(BaseType::TIME));
        registerBuiltin(TypeInfo::makeTime(BaseType::DATE));
        registerBuiltin(TypeInfo::makeTime(BaseType::TOD));
        registerBuiltin(TypeInfo::makeTime(BaseType::DT));
        registerBuiltin(TypeInfo::makeString(BaseType::STRING));
        registerBuiltin(TypeInfo::makeString(BaseType::WSTRING));
        registerBuiltin(TypeInfo::makeVoid());
    }

    // ===== Accessors =====
    const std::vector<Scope>& getScopes() const { return scopes_; }
    const std::vector<Symbol>& getSymbols() const { return symbols_; }
    const std::vector<TypeInfo>& getTypes() const { return types_; }
    ScopeId globalScopeId() const { return globalScopeId_; }
    ScopeId currentScopeId() const { return currentScopeId_; }

private:
    TypeId registerBuiltin(const TypeInfo& type) {
        TypeId id = registerType(type);
        SymbolId symId = declare(types_[id].name, SymbolKind::Type, id);
        if (Symbol* sym = get(symId)) {
            sym->typeId = id;
        }
        return id;
    }
    /**
     * @brief Register the IEC 61131-3 conversion functions (X_TO_Y).
     * @details Each conversion is a global FUNCTION symbol named after the IEC
     * conversion (e.g. INT_TO_REAL, INT_TO_WORD) with one input parameter and
     * the destination base type as return type. The symbol set mirrors the
     * inline helpers provided by the runtime conversions.hpp header, so the
     * code generator emits the function name as-is.
     */
    void registerIecConversionFunctions();
    std::vector<Scope> scopes_;
    std::vector<Symbol> symbols_;
    std::vector<TypeInfo> types_;
    std::vector<ScopeId> scopeStack_;
    std::unordered_map<std::string, TypeId> typeNameToId_;
    ScopeId globalScopeId_ = 1;
    ScopeId currentScopeId_ = 1;
    ScopeId externalScopeId_ = 2;
};

} // namespace st2cpp::semantic
