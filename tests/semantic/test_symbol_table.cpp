/**
 * @file test_symbol_table.cpp
 * @brief Tests for SymbolTable
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "semantic/SymbolTable.h"
#include "semantic/TypeSystem.h"

using namespace st2cpp::semantic;

class SymbolTableTest : public ::testing::Test {
protected:
    SymbolTable symTab;
    
    void SetUp() override {
        // SymbolTable constructor already creates global scope and registers built-in types
        // currentScopeId_ is 1 (global scope)
    }
};

// ============================================================================
// Scope Management Tests
// ============================================================================

TEST_F(SymbolTableTest, GlobalScopeExists) {
    EXPECT_EQ(symTab.globalScope(), 1u);
    EXPECT_EQ(symTab.currentScope(), 1u);
    EXPECT_EQ(symTab.globalScopeId(), 1u);
}

TEST_F(SymbolTableTest, PushPopScope) {
    ScopeId scope1 = symTab.pushScope("FB_MyBlock");
    EXPECT_NE(scope1, 0u);
    EXPECT_EQ(symTab.currentScope(), scope1);
    
    ScopeId scope2 = symTab.pushScope("METHOD_MyMethod");
    EXPECT_NE(scope2, 0u);
    EXPECT_EQ(symTab.currentScope(), scope2);
    
    symTab.popScope();
    EXPECT_EQ(symTab.currentScope(), scope1);
    
    symTab.popScope();
    EXPECT_EQ(symTab.currentScope(), symTab.globalScope());
}

TEST_F(SymbolTableTest, PopScopeKeepsGlobal) {
    // Global scope should never be popped
    for (int i = 0; i < 5; ++i) {
        symTab.pushScope("scope_" + std::to_string(i));
    }
    
    for (int i = 0; i < 6; ++i) {
        symTab.popScope();
    }
    
    EXPECT_EQ(symTab.currentScope(), symTab.globalScope());
}

TEST_F(SymbolTableTest, ScopeParentChain) {
    ScopeId scope1 = symTab.pushScope("level1");
    ScopeId scope2 = symTab.pushScope("level2");
    
    const Scope* s2 = symTab.getScope(scope2);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->parentId, scope1);
    
    const Scope* s1 = symTab.getScope(scope1);
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->parentId, symTab.globalScope());
}

TEST_F(SymbolTableTest, ScopeName) {
    ScopeId scope1 = symTab.pushScope("FB_MyBlock");
    const Scope* s = symTab.getScope(scope1);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name, "FB_MyBlock");
}

// ============================================================================
// Symbol Declaration and Lookup Tests
// ============================================================================

TEST_F(SymbolTableTest, DeclareVariableInGlobalScope) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId varId = symTab.declare("myVar", SymbolKind::Variable, intTypeId);
    
    EXPECT_NE(varId, 0u);
    
    const Symbol* sym = symTab.get(varId);
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "myVar");
    EXPECT_EQ(sym->kind, SymbolKind::Variable);
    EXPECT_EQ(sym->typeId, intTypeId);
    EXPECT_EQ(sym->scopeId, symTab.globalScope());
}

TEST_F(SymbolTableTest, DeclareDuplicateInSameScopeReturnsZero) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId varId1 = symTab.declare("myVar", SymbolKind::Variable, intTypeId);
    SymbolId varId2 = symTab.declare("myVar", SymbolKind::Variable, intTypeId);
    
    EXPECT_NE(varId1, 0u);
    EXPECT_EQ(varId2, 0u); // Duplicate in same scope
}

TEST_F(SymbolTableTest, ShadowingInNestedScope) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    TypeId boolTypeId = symTab.getTypeIdByName("BOOL");
    
    // Declare in global scope
    SymbolId globalVar = symTab.declare("x", SymbolKind::Variable, intTypeId);
    EXPECT_NE(globalVar, 0u);
    
    // Push new scope and declare same name
    symTab.pushScope("inner");
    SymbolId innerVar = symTab.declare("x", SymbolKind::Variable, boolTypeId);
    EXPECT_NE(innerVar, 0u);
    
    // Lookup in current scope should find inner
    SymbolId found = symTab.lookup("x");
    EXPECT_EQ(found, innerVar);
    
    // Pop scope and lookup should find global
    symTab.popScope();
    found = symTab.lookup("x");
    EXPECT_EQ(found, globalVar);
}

TEST_F(SymbolTableTest, LookupCurrentScopeOnly) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    symTab.declare("globalVar", SymbolKind::Variable, intTypeId);
    
    symTab.pushScope("inner");
    // Lookup in inner scope should not find global
    SymbolId found = symTab.lookup("globalVar");
    EXPECT_EQ(found, 0u);
    
    // But lookupRecursive should find it
    found = symTab.lookupRecursive("globalVar");
    EXPECT_NE(found, 0u);
}

TEST_F(SymbolTableTest, LookupRecursiveFindsInParent) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId globalVar = symTab.declare("globalVar", SymbolKind::Variable, intTypeId);
    
    symTab.pushScope("level1");
    symTab.pushScope("level2");
    
    SymbolId found = symTab.lookupRecursive("globalVar");
    EXPECT_EQ(found, globalVar);
}

TEST_F(SymbolTableTest, LookupNonExistentReturnsZero) {
    SymbolId found = symTab.lookup("nonExistent");
    EXPECT_EQ(found, 0u);
    
    found = symTab.lookupRecursive("nonExistent");
    EXPECT_EQ(found, 0u);
}

// ============================================================================
// Symbol Access Tests
// ============================================================================

TEST_F(SymbolTableTest, GetSymbolById) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId varId = symTab.declare("testVar", SymbolKind::Variable, intTypeId);
    
    const Symbol* sym = symTab.get(varId);
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->id, varId);
    EXPECT_EQ(sym->name, "testVar");
}

TEST_F(SymbolTableTest, GetInvalidSymbolIdReturnsNull) {
    const Symbol* sym = symTab.get(999999);
    EXPECT_EQ(sym, nullptr);
}

TEST_F(SymbolTableTest, SymbolHasCorrectScopeId) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    
    SymbolId globalVar = symTab.declare("global", SymbolKind::Variable, intTypeId);
    const Symbol* globalSym = symTab.get(globalVar);
    EXPECT_EQ(globalSym->scopeId, symTab.globalScope());
    
    symTab.pushScope("inner");
    SymbolId innerVar = symTab.declare("inner", SymbolKind::Variable, intTypeId);
    const Symbol* innerSym = symTab.get(innerVar);
    EXPECT_EQ(innerSym->scopeId, symTab.currentScope());
}

// ============================================================================
// Type Registration and Lookup Tests
// ============================================================================

TEST_F(SymbolTableTest, BuiltInTypesRegistered) {
    // All built-in types should be registered
    EXPECT_NE(symTab.getTypeIdByName("BOOL"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("INT"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("DINT"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("REAL"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("STRING"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("TIME"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("BYTE"), 0u);
    EXPECT_NE(symTab.getTypeIdByName("VOID"), 0u);
}

TEST_F(SymbolTableTest, GetTypeById) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    const TypeInfo* typeInfo = symTab.getType(intTypeId);
    
    ASSERT_NE(typeInfo, nullptr);
    EXPECT_EQ(typeInfo->id, intTypeId);
    EXPECT_EQ(typeInfo->name, "INT");
    EXPECT_EQ(typeInfo->kind, TypeKind::Elementary);
}

TEST_F(SymbolTableTest, GetInvalidTypeIdReturnsNull) {
    const TypeInfo* typeInfo = symTab.getType(999999);
    EXPECT_EQ(typeInfo, nullptr);
}

TEST_F(SymbolTableTest, BuiltInTypesHaveSymbols) {
    // Built-in types should also have Symbol entries with kind=Type
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId intSymId = symTab.lookupRecursive("INT");
    
    EXPECT_NE(intSymId, 0u);
    const Symbol* sym = symTab.get(intSymId);
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->kind, SymbolKind::Type);
    EXPECT_EQ(sym->typeId, intTypeId);
}

// ============================================================================
// Symbol Properties Tests
// ============================================================================

TEST_F(SymbolTableTest, VariableProperties) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    SymbolId varId = symTab.declare("myVar", SymbolKind::Variable, intTypeId);
    
    Symbol* sym = symTab.get(varId);
    ASSERT_NE(sym, nullptr);
    
    // Default values
    EXPECT_FALSE(sym->isConstant);
    EXPECT_FALSE(sym->isRetain);
    EXPECT_TRUE(sym->atAddress.empty());
    
    // Modify properties
    sym->isConstant = true;
    sym->isRetain = true;
    sym->atAddress = "%MW10";
    
    EXPECT_TRUE(sym->isConstant);
    EXPECT_TRUE(sym->isRetain);
    EXPECT_EQ(sym->atAddress, "%MW10");
}

TEST_F(SymbolTableTest, FunctionBlockProperties) {
    SymbolId fbId = symTab.declare("MyFB", SymbolKind::FunctionBlock);
    
    Symbol* fb = symTab.get(fbId);
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->kind, SymbolKind::FunctionBlock);
    EXPECT_FALSE(fb->isAbstract);
    EXPECT_FALSE(fb->isFinal);
    
    fb->isAbstract = true;
    EXPECT_TRUE(fb->isAbstract);
}

TEST_F(SymbolTableTest, InheritanceProperties) {
    SymbolId baseId = symTab.declare("BaseFB", SymbolKind::FunctionBlock);
    SymbolId derivedId = symTab.declare("DerivedFB", SymbolKind::FunctionBlock);
    
    Symbol* derived = symTab.get(derivedId);
    derived->baseClassId = baseId;
    
    EXPECT_EQ(derived->baseClassId, baseId);
}

// ============================================================================
// Iteration Tests
// ============================================================================

TEST_F(SymbolTableTest, ForEachSymbol) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    symTab.declare("var1", SymbolKind::Variable, intTypeId);
    symTab.declare("var2", SymbolKind::Variable, intTypeId);
    
    int count = 0;
    symTab.forEachSymbol([&count](const Symbol& sym) {
        if (sym.id != 0) ++count;
    });
    
    // At least our 2 vars + built-in type symbols
    EXPECT_GE(count, 2);
}

TEST_F(SymbolTableTest, ForEachType) {
    int count = 0;
    symTab.forEachType([&count](TypeId id, const TypeInfo& type) {
        ++count;
        EXPECT_NE(id, 0u);
        EXPECT_FALSE(type.name.empty());
    });
    
    // At least all built-in types
    EXPECT_GE(count, 21); // 21 built-in types
}

// ============================================================================
// ScopeId and TypeId Invalid Values
// ============================================================================

TEST_F(SymbolTableTest, InvalidIdsAreZero) {
    // 0 is reserved as invalid
    EXPECT_EQ(ScopeId(0), 0u);
    EXPECT_EQ(SymbolId(0), 0u);
    EXPECT_EQ(TypeId(0), 0u);
    
    // getScope(0) should return nullptr (invalid)
    EXPECT_EQ(symTab.getScope(0), nullptr);
    
    // getSymbol(0) should return nullptr
    EXPECT_EQ(symTab.get(0), nullptr);
    
    // getType(0) should return nullptr
    EXPECT_EQ(symTab.getType(0), nullptr);
}

// ============================================================================
// Symbol Stability Tests (IDs don't change after reallocation)
// ============================================================================

TEST_F(SymbolTableTest, SymbolIdsStableAfterManyDeclarations) {
    TypeId intTypeId = symTab.getTypeIdByName("INT");
    
    // Declare many symbols to trigger vector reallocations
    std::vector<SymbolId> ids;
    for (int i = 0; i < 1000; ++i) {
        SymbolId id = symTab.declare("var_" + std::to_string(i), SymbolKind::Variable, intTypeId);
        ids.push_back(id);
    }
    
    // All IDs should be valid and retrievable
    for (size_t i = 0; i < ids.size(); ++i) {
        const Symbol* sym = symTab.get(ids[i]);
        ASSERT_NE(sym, nullptr);
        EXPECT_EQ(sym->name, "var_" + std::to_string(i));
    }
}

TEST_F(SymbolTableTest, TypeIdsStableAfterManyRegistrations) {
    // Register many types to trigger vector reallocations
    std::vector<TypeId> typeIds;
    for (int i = 0; i < 100; ++i) {
        TypeInfo t = TypeInfo::makeInt(BaseType::INT);
        t.name = "CustomInt" + std::to_string(i);
        TypeId id = symTab.registerType(t);
        typeIds.push_back(id);
    }
    
    for (size_t i = 0; i < typeIds.size(); ++i) {
        const TypeInfo* t = symTab.getType(typeIds[i]);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->name, "CustomInt" + std::to_string(i));
    }
}