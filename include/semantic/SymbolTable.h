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

struct Symbol {
    SymbolId id = 0;
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    TypeId typeId = 0;
    ScopeId scopeId = 0;
    SymbolId parentScopeId = 0;
    std::vector<SymbolId> params;
    TypeId returnTypeId = 0;
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
    const Symbol* get(SymbolId id) const {
        if (id == 0 || id >= symbols_.size()) return nullptr;
        return &symbols_[id];
    }
    Symbol* get(SymbolId id) {
        if (id == 0 || id >= symbols_.size()) return nullptr;
        return &symbols_[id];
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
    std::vector<Scope> scopes_;
    std::vector<Symbol> symbols_;
    std::vector<TypeInfo> types_;
    std::vector<ScopeId> scopeStack_;
    std::unordered_map<std::string, TypeId> typeNameToId_;
    ScopeId globalScopeId_ = 1;
    ScopeId currentScopeId_ = 1;
};

} // namespace st2cpp::semantic
