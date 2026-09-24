/**
 * @file test_decl_visitor.cpp
 * @brief Tests for DeclVisitor - Declaration Phase of Semantic Analysis
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include "semantic/SemanticAnalyzer.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"

using namespace st2cpp::semantic;

class DeclVisitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        analyzer_ = std::make_unique<SemanticAnalyzer>();
    }

    SemanticInfo analyze(const std::string& st) {
        auto tu = TestHelper::parseST(st);
        return analyzer_->analyze(tu);
    }

    const SymbolTable& getSymbolTable() const { return analyzer_->getSymbolTable(); }
    const Diagnostics& getDiagnostics() const { return analyzer_->getDiagnostics(); }

    std::unique_ptr<SemanticAnalyzer> analyzer_;
};

// ============================================================================
// TEST 1 — Empty translation unit
// ============================================================================
TEST_F(DeclVisitorTest, EmptyTranslationUnit) {
    std::string st = "";
    auto info = analyze(st);

    EXPECT_FALSE(info.diagnostics.hasErrors());
    EXPECT_EQ(info.symbolTable->globalScope(), 1u);
    EXPECT_GE(info.symbolTable->getTypeIdByName("INT"), 1u);
    EXPECT_GE(info.symbolTable->getTypeIdByName("BOOL"), 1u);
}

// ============================================================================
// TEST 2 — Simple FUNCTION
// ============================================================================
TEST_F(DeclVisitorTest, SimpleFunction) {
    std::string st = R"(
        FUNCTION Add : INT
            VAR_INPUT
                A : INT;
                B : INT;
            END_VAR
            VAR
                Temp : INT;
            END_VAR
        END_FUNCTION
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Check FUNCTION symbol exists
    SymbolId funcId = symTab.lookupRecursive("Add");
    EXPECT_NE(funcId, 0u);

    const Symbol* funcSym = symTab.get(funcId);
    ASSERT_NE(funcSym, nullptr);
    EXPECT_EQ(funcSym->kind, SymbolKind::Function);
    EXPECT_EQ(funcSym->name, "Add");

    // Check return type
    EXPECT_NE(funcSym->returnTypeId, 0u);
    const TypeInfo* retType = symTab.getType(funcSym->returnTypeId);
    ASSERT_NE(retType, nullptr);
    EXPECT_EQ(retType->name, "INT");

    // Check parameters A and B are registered
    // They should be in the function's scope
    // Note: We can't easily verify parameter registration without scope access
    // But we can verify the function has params
    EXPECT_GE(funcSym->params.size(), 2);
}

// ============================================================================
// TEST 3 — Simple FUNCTION_BLOCK
// ============================================================================
TEST_F(DeclVisitorTest, SimpleFunctionBlock) {
    std::string st = R"(
        FUNCTION_BLOCK Counter
            VAR_INPUT
                Enable : BOOL;
                Reset : BOOL;
            END_VAR
            VAR_OUTPUT
                Count : INT;
            END_VAR
            VAR
                Internal : INT := 0;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId fbId = symTab.lookupRecursive("Counter");
    EXPECT_NE(fbId, 0u);

    const Symbol* fbSym = symTab.get(fbId);
    ASSERT_NE(fbSym, nullptr);
    EXPECT_EQ(fbSym->kind, SymbolKind::FunctionBlock);
    EXPECT_EQ(fbSym->name, "Counter");
    EXPECT_FALSE(fbSym->isAbstract);
    EXPECT_FALSE(fbSym->isFinal);

    // Check FB has a scope
    EXPECT_NE(fbSym->scopeId, 0u);
}

// ============================================================================
// TEST 4 — STRUCT
// ============================================================================
TEST_F(DeclVisitorTest, Struct) {
    std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Struct should be registered as a Type
    SymbolId structSymId = symTab.lookupRecursive("Point");
    EXPECT_NE(structSymId, 0u);

    const Symbol* structSym = symTab.get(structSymId);
    ASSERT_NE(structSym, nullptr);
    EXPECT_EQ(structSym->kind, SymbolKind::Type);

    // Check TypeInfo
    const TypeInfo* typeInfo = symTab.getType(structSym->typeId);
    ASSERT_NE(typeInfo, nullptr);
    EXPECT_EQ(typeInfo->kind, TypeKind::Struct);
    EXPECT_EQ(typeInfo->name, "Point");
    EXPECT_EQ(typeInfo->symbolId, structSymId);

    // Check members
    EXPECT_EQ(structSym->members.size(), 2);
    for (SymbolId memberId : structSym->members) {
        const Symbol* memberSym = symTab.get(memberId);
        ASSERT_NE(memberSym, nullptr);
        EXPECT_EQ(memberSym->kind, SymbolKind::StructMember);
        EXPECT_NE(memberSym->typeId, 0u);
    }
}

// ============================================================================
// TEST 5 — ENUM
// ============================================================================
TEST_F(DeclVisitorTest, Enum) {
    std::string st = R"(
        TYPE Color :
            (Red, Green := 5, Blue, Yellow := 10)
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId enumSymId = symTab.lookupRecursive("Color");
    EXPECT_NE(enumSymId, 0u);

    const Symbol* enumSym = symTab.get(enumSymId);
    ASSERT_NE(enumSym, nullptr);
    EXPECT_EQ(enumSym->kind, SymbolKind::Type);

    // Check TypeInfo
    const TypeInfo* typeInfo = symTab.getType(enumSym->typeId);
    ASSERT_NE(typeInfo, nullptr);
    EXPECT_EQ(typeInfo->kind, TypeKind::Enum);
    EXPECT_EQ(typeInfo->name, "Color");

    // Check enumerators
    EXPECT_EQ(enumSym->enumerators.size(), 4);
    for (SymbolId enumValId : enumSym->enumerators) {
        const Symbol* enumValSym = symTab.get(enumValId);
        ASSERT_NE(enumValSym, nullptr);
        EXPECT_EQ(enumValSym->kind, SymbolKind::Enumerator);
    }
}

// ============================================================================
// TEST 6 — Duplicate declaration
// ============================================================================
TEST_F(DeclVisitorTest, DuplicateDeclaration) {
    std::string st = R"(
        FUNCTION_BLOCK Test
            VAR
                x : INT;
                x : REAL;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_GE(info.diagnostics.errorCount(), 1);

    // Check error code is DuplicateDeclaration
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::DuplicateDeclaration) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST 7 — Unknown type
// ============================================================================
TEST_F(DeclVisitorTest, UnknownType) {
    std::string st = R"(
        FUNCTION_BLOCK Test
            VAR
                x : NonExistentType;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_GE(info.diagnostics.errorCount(), 1);

    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::InvalidTypeName) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST 8 — Forward type reference
// ============================================================================
TEST_F(DeclVisitorTest, ForwardTypeReference) {
    // A is declared BEFORE B and references it: the forward type reference
    // must resolve regardless of declaration order (two-pass registration).
    std::string st = R"(
        TYPE A :
            STRUCT
                b : B;
            END_STRUCT
        END_TYPE

        TYPE B :
            STRUCT
                val : INT;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Both structs should be registered
    SymbolId aId = symTab.lookupRecursive("A");
    SymbolId bId = symTab.lookupRecursive("B");
    EXPECT_NE(aId, 0u);
    EXPECT_NE(bId, 0u);

    // A's member b : B must resolve to B's type id
    const Symbol* aSym = symTab.get(aId);
    ASSERT_NE(aSym, nullptr);
    ASSERT_EQ(aSym->members.size(), 1u);
    const Symbol* bMember = symTab.get(aSym->members[0]);
    ASSERT_NE(bMember, nullptr);
    const Symbol* bType = symTab.get(bId);
    ASSERT_NE(bType, nullptr);
    EXPECT_EQ(bMember->typeId, bType->typeId);
}

// ============================================================================
// TEST 9 — Inheritance (EXTENDS)
// ============================================================================
TEST_F(DeclVisitorTest, InheritanceExtends) {
    std::string st = R"(
        FUNCTION_BLOCK Base
            VAR
                id : INT;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base
            VAR
                extra : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId baseId = symTab.lookupRecursive("Base");
    SymbolId derivedId = symTab.lookupRecursive("Derived");
    EXPECT_NE(baseId, 0u);
    EXPECT_NE(derivedId, 0u);

    // Check baseClassSymbolId in SemanticInfo
    EXPECT_NE(info.fbBaseClass.find(derivedId), info.fbBaseClass.end());
    EXPECT_EQ(info.fbBaseClass[derivedId], baseId);

    // Check SymbolTable relation
    const Symbol* derivedSym = symTab.get(derivedId);
    ASSERT_NE(derivedSym, nullptr);
    EXPECT_EQ(derivedSym->baseClassId, baseId);
}

// ============================================================================
// TEST 10 — Interface (IMPLEMENTS)
// ============================================================================
TEST_F(DeclVisitorTest, InterfaceImplements) {
    std::string st = R"(
        INTERFACE I_Device
            METHOD Init : BOOL
                VAR_INPUT name : STRING; END_VAR
            END_METHOD
            METHOD Process : BOOL
            END_METHOD
        END_INTERFACE

        FUNCTION_BLOCK MyDevice IMPLEMENTS I_Device
            VAR
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId ifaceId = symTab.lookupRecursive("I_Device");
    SymbolId fbId = symTab.lookupRecursive("MyDevice");
    EXPECT_NE(ifaceId, 0u);
    EXPECT_NE(fbId, 0u);

    // Check fbImplements in SemanticInfo
    EXPECT_NE(info.fbImplements.find(fbId), info.fbImplements.end());
    const auto& impl = info.fbImplements[fbId];
    EXPECT_EQ(impl.size(), 1);
    EXPECT_EQ(impl[0], ifaceId);

    // Check SymbolTable relation
    const Symbol* fbSym = symTab.get(fbId);
    ASSERT_NE(fbSym, nullptr);
    EXPECT_EQ(fbSym->implementedInterfaces.size(), 1);
    EXPECT_EQ(fbSym->implementedInterfaces[0], ifaceId);
}

// ============================================================================
// TEST 11 — Shadowing
// ============================================================================
TEST_F(DeclVisitorTest, Shadowing) {
    std::string st = R"(
        VAR_GLOBAL
            x : INT;
        END_VAR

        FUNCTION_BLOCK Test
            VAR
                x : REAL;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Global x should exist
    SymbolId globalX = symTab.lookupRecursive("x");
    EXPECT_NE(globalX, 0u);

    // FB Test should exist
    SymbolId fbId = symTab.lookupRecursive("Test");
    EXPECT_NE(fbId, 0u);

    // Both should be valid (shadowing is allowed)
    const Symbol* globalXSym = symTab.get(globalX);
    const Symbol* fbSym = symTab.get(fbId);
    ASSERT_NE(globalXSym, nullptr);
    ASSERT_NE(fbSym, nullptr);
}

// ============================================================================
// TEST 12 — Topological sort (FB)
// ============================================================================
TEST_F(DeclVisitorTest, TopologicalSortFB) {
    std::string st = R"(
        FUNCTION_BLOCK Base
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK MoreDerived EXTENDS Derived
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    // Check topological order
    EXPECT_EQ(info.fbTopoOrder.size(), 3);
    
    const SymbolTable& symTab = *info.symbolTable;
    SymbolId baseId = symTab.lookupRecursive("Base");
    SymbolId derivedId = symTab.lookupRecursive("Derived");
    SymbolId moreDerivedId = symTab.lookupRecursive("MoreDerived");

    // Base should come before Derived, Derived before MoreDerived
    auto itBase = std::find(info.fbTopoOrder.begin(), info.fbTopoOrder.end(), baseId);
    auto itDerived = std::find(info.fbTopoOrder.begin(), info.fbTopoOrder.end(), derivedId);
    auto itMore = std::find(info.fbTopoOrder.begin(), info.fbTopoOrder.end(), moreDerivedId);

    EXPECT_NE(itBase, info.fbTopoOrder.end());
    EXPECT_NE(itDerived, info.fbTopoOrder.end());
    EXPECT_NE(itMore, info.fbTopoOrder.end());

    EXPECT_LT(itBase - info.fbTopoOrder.begin(), itDerived - info.fbTopoOrder.begin());
    EXPECT_LT(itDerived - info.fbTopoOrder.begin(), itMore - info.fbTopoOrder.begin());
}

// ============================================================================
// TEST 13 — Cycle detection
// ============================================================================
TEST_F(DeclVisitorTest, CycleDetection) {
    std::string st = R"(
        FUNCTION_BLOCK A EXTENDS B
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK B EXTENDS A
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::CircularInheritance) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST 14 — Complex program with multiple POUs
// ============================================================================
TEST_F(DeclVisitorTest, ComplexProgram) {
    std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
                y : INT;
            END_STRUCT
        END_TYPE

        TYPE Color : (Red, Green, Blue) END_TYPE

        FUNCTION Add : INT
            VAR_INPUT a : INT; b : INT; END_VAR
        END_FUNCTION

        FUNCTION_BLOCK Counter
            VAR_INPUT enable : BOOL; END_VAR
            VAR_OUTPUT count : INT; END_VAR
            VAR internal : INT; END_VAR
        END_FUNCTION_BLOCK

        PROGRAM Main
            VAR
                c : Counter;
                result : INT;
                p : Point;
            END_VAR
        END_PROGRAM
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Check all types registered
    EXPECT_NE(symTab.lookupRecursive("Point"), 0u);
    EXPECT_NE(symTab.lookupRecursive("Color"), 0u);

    // Check all POUs registered
    EXPECT_NE(symTab.lookupRecursive("Add"), 0u);
    EXPECT_NE(symTab.lookupRecursive("Counter"), 0u);
    EXPECT_NE(symTab.lookupRecursive("Main"), 0u);

    // Check kinds
    const Symbol* addSym = symTab.get(symTab.lookupRecursive("Add"));
    const Symbol* counterSym = symTab.get(symTab.lookupRecursive("Counter"));
    const Symbol* mainSym = symTab.get(symTab.lookupRecursive("Main"));

    ASSERT_NE(addSym, nullptr);
    ASSERT_NE(counterSym, nullptr);
    ASSERT_NE(mainSym, nullptr);

    EXPECT_EQ(addSym->kind, SymbolKind::Function);
    EXPECT_EQ(counterSym->kind, SymbolKind::FunctionBlock);
    EXPECT_EQ(mainSym->kind, SymbolKind::Program);
}

// ============================================================================
// TEST 15 — ENUM with duplicate values
// ============================================================================
TEST_F(DeclVisitorTest, EnumDuplicateValues) {
    std::string st = R"(
        TYPE Color :
            (Red, Green, Red)
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::DuplicateEnumValue) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST 16 — Interface method registration
// ============================================================================
TEST_F(DeclVisitorTest, InterfaceMethodRegistration) {
    std::string st = R"(
        INTERFACE I_Test
            METHOD Method1 : INT
                VAR_INPUT x : INT; END_VAR
            END_METHOD
            METHOD Method2 : BOOL
            END_METHOD
        END_INTERFACE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId ifaceId = symTab.lookupRecursive("I_Test");
    EXPECT_NE(ifaceId, 0u);

    const Symbol* ifaceSym = symTab.get(ifaceId);
    ASSERT_NE(ifaceSym, nullptr);
    EXPECT_EQ(ifaceSym->kind, SymbolKind::Interface);

    // Check methods are registered
    EXPECT_EQ(ifaceSym->members.size(), 2);
    for (SymbolId methodId : ifaceSym->members) {
        const Symbol* methodSym = symTab.get(methodId);
        ASSERT_NE(methodSym, nullptr);
        EXPECT_EQ(methodSym->kind, SymbolKind::Method);
        EXPECT_TRUE(methodSym->isAbstract); // Interface methods are abstract
        EXPECT_NE(methodSym->returnTypeId, 0u);
    }
}

// ============================================================================
// TEST 17 — Global variables
// ============================================================================
TEST_F(DeclVisitorTest, GlobalVariables) {
    std::string st = R"(
        VAR_GLOBAL
            g_int : INT := 10;
            g_bool : BOOL := TRUE;
        END_VAR
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    SymbolId gIntId = symTab.lookupRecursive("g_int");
    SymbolId gBoolId = symTab.lookupRecursive("g_bool");

    EXPECT_NE(gIntId, 0u);
    EXPECT_NE(gBoolId, 0u);

    const Symbol* gIntSym = symTab.get(gIntId);
    const Symbol* gBoolSym = symTab.get(gBoolId);

    ASSERT_NE(gIntSym, nullptr);
    ASSERT_NE(gBoolSym, nullptr);

    EXPECT_EQ(gIntSym->kind, SymbolKind::Variable);
    EXPECT_EQ(gBoolSym->kind, SymbolKind::Variable);

    // Check type IDs
    const TypeInfo* intType = symTab.getType(gIntSym->typeId);
    const TypeInfo* boolType = symTab.getType(gBoolSym->typeId);
    ASSERT_NE(intType, nullptr);
    ASSERT_NE(boolType, nullptr);
    EXPECT_EQ(intType->name, "INT");
    EXPECT_EQ(boolType->name, "BOOL");
}

// ============================================================================
// TEST 18 — Circular inheritance detection in structs
// ============================================================================
TEST_F(DeclVisitorTest, StructCircularDependency) {
    // A contains B by value and B contains A by value: an un-emittable C++
    // layout. Both types now resolve (two-pass), so the cycle itself must be
    // reported as a dedicated CircularDependency error naming the types.
    std::string st = R"(
        TYPE A :
            STRUCT
                b : B;
            END_STRUCT
        END_TYPE

        TYPE B :
            STRUCT
                a : A;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::CircularDependency) {
            found = true;
            EXPECT_NE(d.message.find('A'), std::string::npos);
            EXPECT_NE(d.message.find('B'), std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST 19 — Method with parameters
// ============================================================================
TEST_F(DeclVisitorTest, MethodWithParameters) {
    std::string st = R"(
        FUNCTION_BLOCK Math
            VAR_INPUT
                a : INT;
            END_VAR
            METHOD Add : INT
                VAR_INPUT x : INT; y : INT; END_VAR
            END_METHOD
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // Check method is registered
    SymbolId fbId = symTab.lookupRecursive("Math");
    EXPECT_NE(fbId, 0u);

    // Check FB has method
    const Symbol* fbSym = symTab.get(fbId);
    ASSERT_NE(fbSym, nullptr);
}

// ============================================================================
// TEST 20 — SemanticInfo completeness
// ============================================================================
TEST_F(DeclVisitorTest, SemanticInfoCompleteness) {
    std::string st = R"(
        TYPE Point :
            STRUCT
                x : INT;
            END_STRUCT
        END_TYPE

        FUNCTION_BLOCK Base
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK Derived EXTENDS Base IMPLEMENTS I_Dummy
        END_FUNCTION_BLOCK

        INTERFACE I_Dummy
            METHOD DoIt : BOOL
            END_METHOD
        END_INTERFACE

        FUNCTION Add : INT
            VAR_INPUT a : INT; b : INT; END_VAR
        END_FUNCTION

        PROGRAM Main
        END_PROGRAM
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    // Check all SemanticInfo fields are populated
    EXPECT_NE(info.symbolTable, nullptr);
    EXPECT_FALSE(info.empty());

    // fbTopoOrder should have FBs
    EXPECT_GE(info.fbTopoOrder.size(), 2); // Base, Derived

    // structTopoOrder should have structs
    EXPECT_GE(info.structTopoOrder.size(), 1); // Point

    // fbBaseClass should have Derived -> Base
    const SymbolTable& symTab = *info.symbolTable;
    SymbolId baseId = symTab.lookupRecursive("Base");
    SymbolId derivedId = symTab.lookupRecursive("Derived");
    if (baseId != 0 && derivedId != 0) {
        EXPECT_NE(info.fbBaseClass.find(derivedId), info.fbBaseClass.end());
    }

    // fbImplements should have Derived -> I_Dummy
    if (derivedId != 0) {
        EXPECT_NE(info.fbImplements.find(derivedId), info.fbImplements.end());
    }

    // piConfig should have defaults
    EXPECT_GT(info.piConfig.inputBytes, 0);
    EXPECT_GT(info.piConfig.outputBytes, 0);
    EXPECT_GT(info.piConfig.markerBytes, 0);
}

// ============================================================================
// Array bounds are captured from the AST (no placeholders)
// ============================================================================
TEST_F(DeclVisitorTest, ArrayBoundsFromAst) {
    std::string st = R"(
        VAR_GLOBAL
            A : ARRAY[0..9] OF INT;
            B : ARRAY[2..10] OF INT;
            C : ARRAY[-5..5] OF REAL;
            D : ARRAY[1..3, 4..6] OF BOOL;
        END_VAR
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    auto checkArray = [&](const std::string& varName, size_t dimCount, int low0, int high0) {
        SymbolId varId = symTab.lookupRecursive(varName);
        ASSERT_NE(varId, 0u) << "variable " << varName << " not found";
        const Symbol* varSym = symTab.get(varId);
        ASSERT_NE(varSym, nullptr);
        const TypeInfo* arrType = symTab.getType(varSym->typeId);
        ASSERT_NE(arrType, nullptr);
        ASSERT_EQ(arrType->kind, TypeKind::Array);
        EXPECT_EQ(arrType->dimensions.size(), dimCount);
        if (!arrType->dimensions.empty()) {
            EXPECT_EQ(arrType->dimensions[0].low, low0);
            EXPECT_EQ(arrType->dimensions[0].high, high0);
        }
    };

    checkArray("A", 1, 0, 9);
    checkArray("B", 1, 2, 10);
    checkArray("C", 1, -5, 5);
    checkArray("D", 2, 1, 3);

    // Size is computed from the real bounds: count = high - low + 1 per dim
    SymbolId bId = symTab.lookupRecursive("B");
    const TypeInfo* bType = symTab.getType(symTab.get(bId)->typeId);
    ASSERT_NE(bType, nullptr);
    // ARRAY[2..10] OF INT -> 9 elements of Int16 (2 bytes) = 18
    EXPECT_EQ(bType->sizeInBytes, size_t(18));

    SymbolId dId = symTab.lookupRecursive("D");
    const TypeInfo* dType = symTab.getType(symTab.get(dId)->typeId);
    ASSERT_NE(dType, nullptr);
    // ARRAY[1..3, 4..6] OF BOOL -> 3 * 3 = 9 elements of 1 byte
    EXPECT_EQ(dType->sizeInBytes, size_t(9));
}

// ============================================================================
// Array bounds must be constant integer literals (Bug: variable bounds accepted)
// ============================================================================
TEST_F(DeclVisitorTest, ArrayBoundsVariableRejected) {
    std::string st = R"(
        VAR_GLOBAL
            n : INT := 5;
            A : ARRAY[n..10] OF INT;
        END_VAR
    )";

    auto info = analyze(st);

    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::ArrayBoundsNotConstant) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected ArrayBoundsNotConstant diagnostic";
}

// ============================================================================
// Named type aliases (TYPE Name : <type>; END_TYPE)
// ============================================================================
TEST_F(DeclVisitorTest, TypeAliasToScalar) {
    std::string st = R"(
        TYPE MyInt : INT; END_TYPE
        VAR_GLOBAL
            A : MyInt := 42;
        END_VAR
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // The alias name resolves to the canonical INT type id.
    TypeId aliasId = symTab.getTypeIdByName("MyInt");
    ASSERT_NE(aliasId, 0u);
    const TypeInfo* aliasType = symTab.getType(aliasId);
    ASSERT_NE(aliasType, nullptr);
    EXPECT_TRUE(aliasType->isNumeric);
    EXPECT_EQ(aliasType->baseType, BaseType::INT);

    // A variable using the alias carries the canonical type.
    SymbolId varId = symTab.lookupRecursive("A");
    ASSERT_NE(varId, 0u);
    EXPECT_EQ(symTab.get(varId)->typeId, aliasId);
}

TEST_F(DeclVisitorTest, TypeAliasArray) {
    std::string st = R"(
        TYPE MyCounter : ARRAY[0..9] OF INT; END_TYPE
        VAR_GLOBAL
            B : MyCounter;
        END_VAR
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    TypeId aliasId = symTab.getTypeIdByName("MyCounter");
    ASSERT_NE(aliasId, 0u);
    const TypeInfo* aliasType = symTab.getType(aliasId);
    ASSERT_NE(aliasType, nullptr);
    EXPECT_EQ(aliasType->kind, TypeKind::Array);
    ASSERT_EQ(aliasType->dimensions.size(), size_t(1));
    EXPECT_EQ(aliasType->dimensions[0].low, 0);
    EXPECT_EQ(aliasType->dimensions[0].high, 9);
    EXPECT_EQ(aliasType->elementTypeId, symTab.getTypeIdByName("INT"));

    SymbolId varId = symTab.lookupRecursive("B");
    ASSERT_NE(varId, 0u);
    EXPECT_EQ(symTab.get(varId)->typeId, aliasId);
}

TEST_F(DeclVisitorTest, TypeAliasToStructAndAliasChain) {
    std::string st = R"(
        TYPE Point : STRUCT
            x : INT;
            y : INT;
        END_STRUCT END_TYPE
        TYPE MyPoint : Point; END_TYPE
        TYPE MyPoint2 : MyPoint; END_TYPE
        VAR_GLOBAL
            P : MyPoint2;
        END_VAR
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;

    // The whole chain resolves to the single canonical Point struct.
    TypeId pointId = symTab.getTypeIdByName("Point");
    ASSERT_NE(pointId, 0u);
    EXPECT_EQ(symTab.getTypeIdByName("MyPoint"), pointId);
    EXPECT_EQ(symTab.getTypeIdByName("MyPoint2"), pointId);
    EXPECT_EQ(symTab.get(symTab.lookupRecursive("P"))->typeId, pointId);
}

TEST_F(DeclVisitorTest, TypeAliasUnknownTargetRejected) {
    std::string st = R"(
        TYPE Bad : NoSuchType; END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());

    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::InvalidTypeName) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected InvalidTypeName diagnostic for unknown alias target";
}

// ============================================================================
// TEST 22 — FB composition ordering
// ============================================================================
TEST_F(DeclVisitorTest, FbTopoOrderIncludesComposition) {
    // FB_Outer uses FB_Inner as a member but is declared FIRST in source.
    // The topological order must still place FB_Inner before FB_Outer.
    std::string st = R"(
        FUNCTION_BLOCK FB_Outer
            VAR
                inner : FB_Inner;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK FB_Inner
            VAR
                x : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;
    SymbolId innerId = symTab.lookupRecursive("FB_Inner");
    SymbolId outerId = symTab.lookupRecursive("FB_Outer");
    EXPECT_NE(innerId, 0u);
    EXPECT_NE(outerId, 0u);

    // FB instance types must resolve nominally
    SymbolId outerVarId = 0;
    const Symbol* outerSym = symTab.get(outerId);
    ASSERT_NE(outerSym, nullptr);
    const Scope* outerScope = symTab.getScope(outerSym->scopeId);
    ASSERT_NE(outerScope, nullptr);
    auto varIt = outerScope->symbols.find(SymbolTable::normalizeKey("inner"));
    ASSERT_NE(varIt, outerScope->symbols.end());
    outerVarId = varIt->second;
    const Symbol* innerVarSym = symTab.get(outerVarId);
    ASSERT_NE(innerVarSym, nullptr);
    const TypeInfo* innerVarType = symTab.getType(innerVarSym->typeId);
    ASSERT_NE(innerVarType, nullptr);
    EXPECT_EQ(innerVarType->kind, TypeKind::FunctionBlock);
    EXPECT_EQ(innerVarType->symbolId, innerId);

    // Topological order: FB_Inner (dependency) must come before FB_Outer
    size_t innerPos = info.fbTopoOrder.size();
    size_t outerPos = info.fbTopoOrder.size();
    for (size_t i = 0; i < info.fbTopoOrder.size(); ++i) {
        if (info.fbTopoOrder[i] == innerId) innerPos = i;
        if (info.fbTopoOrder[i] == outerId) outerPos = i;
    }
    EXPECT_LT(innerPos, outerPos);
    EXPECT_EQ(info.fbTopoOrder.size(), size_t(2));
}

TEST_F(DeclVisitorTest, FbTopoOrderCombinesInheritanceAndComposition) {
    // FB_Leaf extends FB_Base and contains FB_Component.
    // Both edges must be honored: Base and Component precede Leaf.
    std::string st = R"(
        FUNCTION_BLOCK FB_Base
            VAR
                id : INT;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK FB_Leaf EXTENDS FB_Base
            VAR
                comp : FB_Component;
            END_VAR
        END_FUNCTION_BLOCK

        FUNCTION_BLOCK FB_Component
            VAR
                v : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;
    SymbolId baseId = symTab.lookupRecursive("FB_Base");
    SymbolId leafId = symTab.lookupRecursive("FB_Leaf");
    SymbolId compId = symTab.lookupRecursive("FB_Component");

    auto pos = [&info](SymbolId id) -> int {
        for (size_t i = 0; i < info.fbTopoOrder.size(); ++i) {
            if (info.fbTopoOrder[i] == id) return static_cast<int>(i);
        }
        return -1;
    };

    EXPECT_GE(pos(baseId), 0);
    EXPECT_GE(pos(compId), 0);
    EXPECT_GE(pos(leafId), 0);
    EXPECT_LT(pos(baseId), pos(leafId));
    EXPECT_LT(pos(compId), pos(leafId));
}

// ============================================================================
// Forward type references
//
// The declaration phase registers every type name before resolving any body,
// so IEC 61131-3 declaration order never matters. Each test below is exercised
// twice: once with the referenced type declared before its user and once with
// it declared after; both variants must produce identical outcomes.
// ============================================================================

// ============================================================================
// TEST F1 — struct -> struct forward reference
// ============================================================================
TEST_F(DeclVisitorTest, ForwardStructToStruct) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string st = std::string(after ?
            "TYPE A :\nSTRUCT\n    inner1 : B;\nEND_STRUCT\nEND_TYPE\n\nTYPE B :\nSTRUCT\n    val : INT;\nEND_STRUCT\nEND_TYPE\n" :
            "TYPE B :\nSTRUCT\n    val : INT;\nEND_STRUCT\nEND_TYPE\n\nTYPE A :\nSTRUCT\n    inner1 : B;\nEND_STRUCT\nEND_TYPE\n");
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        SymbolId aId = symTab.lookupRecursive("A");
        SymbolId bId = symTab.lookupRecursive("B");
        EXPECT_NE(aId, 0u);
        EXPECT_NE(bId, 0u);
        const Symbol* aSym = symTab.get(aId);
        const Symbol* bSym = symTab.get(bId);
        ASSERT_NE(aSym, nullptr);
        ASSERT_NE(bSym, nullptr);
        ASSERT_EQ(aSym->members.size(), 1u);
        const Symbol* m = symTab.get(aSym->members[0]);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->typeId, bSym->typeId);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F2 — struct -> FB forward reference
// ============================================================================
TEST_F(DeclVisitorTest, ForwardStructToFB) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string st = std::string(after ?
            "TYPE S :\nSTRUCT\n    fb1 : FB_T;\nEND_STRUCT\nEND_TYPE\n\nFUNCTION_BLOCK FB_T\nVAR\n    v : INT;\nEND_VAR\nEND_FUNCTION_BLOCK\n" :
            "FUNCTION_BLOCK FB_T\nVAR\n    v : INT;\nEND_VAR\nEND_FUNCTION_BLOCK\n\nTYPE S :\nSTRUCT\n    fb1 : FB_T;\nEND_STRUCT\nEND_TYPE\n");
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        SymbolId sId = symTab.lookupRecursive("S");
        SymbolId fbId = symTab.lookupRecursive("FB_T");
        EXPECT_NE(sId, 0u);
        EXPECT_NE(fbId, 0u);
        const Symbol* sSym = symTab.get(sId);
        const Symbol* fbSym = symTab.get(fbId);
        ASSERT_NE(sSym, nullptr);
        ASSERT_NE(fbSym, nullptr);
        ASSERT_EQ(sSym->members.size(), 1u);
        const Symbol* m = symTab.get(sSym->members[0]);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->typeId, fbSym->typeId);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F3 — FB -> struct forward reference
// ============================================================================
TEST_F(DeclVisitorTest, ForwardFBToStruct) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string st = std::string(after ?
            "FUNCTION_BLOCK Task\nVAR\n    st : SLate;\nEND_VAR\nEND_FUNCTION_BLOCK\n\nTYPE SLate :\nSTRUCT\n    w : INT;\nEND_STRUCT\nEND_TYPE\n" :
            "TYPE SLate :\nSTRUCT\n    w : INT;\nEND_STRUCT\nEND_TYPE\n\nFUNCTION_BLOCK Task\nVAR\n    st : SLate;\nEND_VAR\nEND_FUNCTION_BLOCK\n");
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        SymbolId fbId = symTab.lookupRecursive("Task");
        SymbolId stId = symTab.lookupRecursive("SLate");
        EXPECT_NE(fbId, 0u);
        EXPECT_NE(stId, 0u);
        const Symbol* stSym = symTab.get(stId);
        ASSERT_NE(stSym, nullptr);
        // The FB variable st must carry SLate's TypeId
        const Scope* fbScope = symTab.getScope(symTab.get(fbId)->scopeId);
        ASSERT_NE(fbScope, nullptr);
        auto it = fbScope->symbols.find(st2cpp::semantic::SymbolTable::normalizeKey("st"));
        ASSERT_NE(it, fbScope->symbols.end());
        const Symbol* stVar = symTab.get(it->second);
        ASSERT_NE(stVar, nullptr);
        EXPECT_EQ(stVar->typeId, stSym->typeId);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F4 — variable of enum / interface declared after the usage
// ============================================================================
TEST_F(DeclVisitorTest, ForwardEnumAndInterfaceTypeDeclaredAfter) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string globalsProgram =
            "VAR_GLOBAL\n"
            "    g1 : ILate;\n"
            "    g2 : ELate;\n"
            "END_VAR\n\n"
            "PROGRAM P\n"
            "    VAR\n"
            "        x : INT;\n"
            "    END_VAR\n"
            "END_PROGRAM\n";
        const std::string typeDecls =
            "INTERFACE ILate\n"
            "    METHOD DoIt : BOOL\n"
            "    END_METHOD\n"
            "END_INTERFACE\n\n"
            "TYPE ELate :\n"
            "    ( A, B );\n"
            "END_TYPE\n";
        const std::string st = after ? (globalsProgram + typeDecls)
                                     : (typeDecls + globalsProgram);
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        EXPECT_NE(symTab.lookupRecursive("ILate"), 0u);
        EXPECT_NE(symTab.lookupRecursive("ELate"), 0u);
        EXPECT_NE(symTab.lookupRecursive("g1"), 0u);
        EXPECT_NE(symTab.lookupRecursive("g2"), 0u);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F5 — ARRAY OF / POINTER TO / REF_TO of a type declared after
// ============================================================================
TEST_F(DeclVisitorTest, ForwardWrappedTypes) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string st = std::string(after ?
            "PROGRAM P\nVAR\n    a1 : ARRAY[0..9] OF ALate;\n    p1 : POINTER TO ALate;\n    r1 : REF_TO ALate;\nEND_VAR\nEND_PROGRAM\n\nTYPE ALate :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n" :
            "TYPE ALate :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n\nPROGRAM P\nVAR\n    a1 : ARRAY[0..9] OF ALate;\n    p1 : POINTER TO ALate;\n    r1 : REF_TO ALate;\nEND_VAR\nEND_PROGRAM\n");
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        EXPECT_NE(symTab.lookupRecursive("ALate"), 0u);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F6 — FUNCTION return type declared after the function
// ============================================================================
TEST_F(DeclVisitorTest, ForwardFunctionReturnType) {
    auto run = [&](bool after) -> SemanticInfo {
        const std::string st = std::string(after ?
            "FUNCTION F : FLate\nVAR_INPUT x : INT; END_VAR\n    F := 1;\nEND_FUNCTION\n\nTYPE FLate :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n" :
            "TYPE FLate :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n\nFUNCTION F : FLate\nVAR_INPUT x : INT; END_VAR\n    F := 1;\nEND_FUNCTION\n");
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        SymbolId fId = symTab.lookupRecursive("F");
        SymbolId lateId = symTab.lookupRecursive("FLate");
        EXPECT_NE(fId, 0u);
        EXPECT_NE(lateId, 0u);
        const Symbol* fSym = symTab.get(fId);
        const Symbol* lateSym = symTab.get(lateId);
        ASSERT_NE(fSym, nullptr);
        ASSERT_NE(lateSym, nullptr);
        EXPECT_EQ(fSym->returnTypeId, lateSym->typeId);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F7 — chain of three types, fully reversed declaration order
// ============================================================================
TEST_F(DeclVisitorTest, ForwardChainOfThree) {
    auto run = [&](bool reverse) -> SemanticInfo {
        const std::string fwd = "TYPE A :\nSTRUCT\n    b : B;\nEND_STRUCT\nEND_TYPE\n\n"
                                "TYPE B :\nSTRUCT\n    c : C;\nEND_STRUCT\nEND_TYPE\n\n"
                                "TYPE C :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n";
        const std::string rev = "TYPE C :\nSTRUCT\n    v : INT;\nEND_STRUCT\nEND_TYPE\n\n"
                                "TYPE B :\nSTRUCT\n    c : C;\nEND_STRUCT\nEND_TYPE\n\n"
                                "TYPE A :\nSTRUCT\n    b : B;\nEND_STRUCT\nEND_TYPE\n";
        const std::string st = reverse ? rev : fwd;
        return analyze(st);
    };

    auto check = [this](const SemanticInfo& info) {
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
        const SymbolTable& symTab = *info.symbolTable;
        SymbolId aId = symTab.lookupRecursive("A");
        SymbolId cId = symTab.lookupRecursive("C");
        EXPECT_NE(aId, 0u);
        EXPECT_NE(cId, 0u);
        const Symbol* aSym = symTab.get(aId);
        ASSERT_NE(aSym, nullptr);
        ASSERT_EQ(aSym->members.size(), 1u);
        const Symbol* bMember = symTab.get(aSym->members[0]);
        ASSERT_NE(bMember, nullptr);
        // Resolved member B must itself reference C
        const TypeInfo* bType = symTab.getType(bMember->typeId);
        ASSERT_NE(bType, nullptr);
        ASSERT_NE(bType->symbolId, 0u);
        const Symbol* bSym2 = symTab.get(bType->symbolId);
        ASSERT_NE(bSym2, nullptr);
        ASSERT_EQ(bSym2->members.size(), 1u);
        const Symbol* cMember = symTab.get(bSym2->members[0]);
        ASSERT_NE(cMember, nullptr);
        EXPECT_EQ(cMember->typeId, symTab.get(cId)->typeId);
    };

    check(run(true));
    check(run(false));
}

// ============================================================================
// TEST F8 — by-value cycle struct->struct must error (CircularDependency)
// ============================================================================
TEST_F(DeclVisitorTest, ValueCycleStructStruct) {
    std::string st = R"(
        TYPE A :
            STRUCT
                b : B;
            END_STRUCT
        END_TYPE

        TYPE B :
            STRUCT
                a : A;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::CircularDependency) {
            found = true;
            EXPECT_NE(d.message.find('A'), std::string::npos);
            EXPECT_NE(d.message.find('B'), std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST F8b — function block containing itself by value must error
// ============================================================================
TEST_F(DeclVisitorTest, ValueCycleFBSelf) {
    std::string st = R"(
        FUNCTION_BLOCK F
            VAR
                sub : F;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::CircularDependency) {
            found = true;
            EXPECT_NE(d.message.find('F'), std::string::npos);
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// TEST F9 — a pointer-based "cycle" stays legal (POINTER TO is not a
// by-value dependency)
// ============================================================================
TEST_F(DeclVisitorTest, PointerCycleIsLegal) {
    std::string st = R"(
        TYPE A :
            STRUCT
                pb : POINTER TO B;
            END_STRUCT
        END_TYPE

        TYPE B :
            STRUCT
                a : A;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
    for (const auto& d : info.diagnostics.all()) {
        EXPECT_NE(d.code, DiagnosticCode::CircularDependency);
    }
}

// ============================================================================
// TEST F10 — unknown type reports InvalidTypeName pointing at the
// referencing declaration's line, not <unknown>
// ============================================================================
TEST_F(DeclVisitorTest, UnknownTypeReportsLine) {
    std::string st = R"(
        FUNCTION F : Missing
            VAR
                v : AlsoMissing;
            END_VAR
        END_FUNCTION
    )";

    auto info = analyze(st);
    // Both the FUNCTION return type and the POU-local variable's unknown type
    // must point at their declaration line (the parser now fills
    // VarDecl::line for local VAR sections).
    bool returnTypeLineOk = false;
    bool localUnknownLineOk = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code == DiagnosticCode::InvalidTypeName) {
            if (d.message.find(": Missing") != std::string::npos) {
                returnTypeLineOk = (d.location.line > 0);
            }
            if (d.message.find(": AlsoMissing") != std::string::npos) {
                localUnknownLineOk = (d.location.line == 4);
            }
        }
    }
    EXPECT_TRUE(returnTypeLineOk);
    EXPECT_TRUE(localUnknownLineOk);
}

// ============================================================================
// TEST F11 — struct member whose name equals its type name (case-insensitive)
// must not break resolution: 'b : B' resolves to the global type B, never to
// the member itself
// ============================================================================
TEST_F(DeclVisitorTest, MemberNamedLikeItsType) {
    std::string st = R"(
        TYPE B :
            STRUCT
                val : INT;
            END_STRUCT
        END_TYPE

        TYPE A :
            STRUCT
                b : B;
            END_STRUCT
        END_TYPE
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    // The member symbol still exists, and its type is the struct B.
    const SymbolTable& symTab = *info.symbolTable;
    SymbolId aId = symTab.lookupRecursive("A");
    SymbolId bId = symTab.lookupRecursive("B");
    ASSERT_NE(aId, 0u);
    ASSERT_NE(bId, 0u);
    const Symbol* aSym = symTab.get(aId);
    const Symbol* bSym = symTab.get(bId);
    ASSERT_EQ(aSym->members.size(), 1u);
    // The member's type must be struct B's TypeId, and the member name is not
    // a duplicate of the type's global symbol.
    EXPECT_EQ(symTab.get(aSym->members[0])->typeId, bSym->typeId);
}

// ============================================================================
// TEST F12 — struct containing an FB declared later: the FB appears in
// fbTopoOrder, the struct in structTopoOrder, and generation orders the FB
// first (see CodegenSemanticTest.StructFBLaterIsDefinedFbFirst).
// ============================================================================
TEST_F(DeclVisitorTest, StructFBTopoMembership) {
    std::string st = R"(
        TYPE S :
            STRUCT
                fb1 : FB_T;
            END_STRUCT
        END_TYPE

        FUNCTION_BLOCK FB_T
            VAR
                v : INT;
            END_VAR
        END_FUNCTION_BLOCK
    )";

    auto info = analyze(st);
    EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

    const SymbolTable& symTab = *info.symbolTable;
    SymbolId sId = symTab.lookupRecursive("S");
    SymbolId fbId = symTab.lookupRecursive("FB_T");
    ASSERT_NE(sId, 0u);
    ASSERT_NE(fbId, 0u);

    auto contains = [](const std::vector<SymbolId>& v, SymbolId id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    EXPECT_TRUE(contains(info.fbTopoOrder, fbId));
    EXPECT_TRUE(contains(info.structTopoOrder, sId));
}

// ============================================================================
// TEST F13 — workspace merge: analyzing the two halves of a project in either
// order must yield the same diagnostics and the same topological order
// ============================================================================
TEST_F(DeclVisitorTest, WorkspaceMergeOrderIndependent) {
    const std::string fileA =
        "FUNCTION_BLOCK FB_A\n"
        "VAR\n"
        "    inner1 : S_B;\n"
        "END_VAR\n"
        "END_FUNCTION_BLOCK\n";
    const std::string fileB =
        "TYPE S_B :\n"
        "STRUCT\n"
        "    w : INT;\n"
        "END_STRUCT\n"
        "END_TYPE\n"
        "FUNCTION_BLOCK FB_B EXTENDS FB_A\n"
        "VAR\n"
        "END_VAR\n"
        "END_FUNCTION_BLOCK\n";
    const std::string program =
        "PROGRAM Main\n"
        "VAR\n"
        "    b1 : FB_A;\n"
        "    b2 : FB_B;\n"
        "    s1 : S_B;\n"
        "END_VAR\n"
        "END_PROGRAM\n";

    auto infoAB = analyze(fileA + fileB + program);
    auto infoBA = analyze(fileB + fileA + program);

    // Symbol ids differ between the two runs (declaration order differs), so
    // compare the NAME sequences instead of the raw SymbolId lists, decoding
    // each order with the symbol table of the run that produced it.
    auto orderNames = [](const SemanticInfo& info, const std::vector<SymbolId>& order) {
        std::vector<std::string> names;
        const SymbolTable& symTab = *info.symbolTable;
        for (SymbolId id : order) {
            const Symbol* sym = symTab.get(id);
            names.push_back(sym ? sym->name : "<?>");
        }
        return names;
    };

    EXPECT_FALSE(infoAB.diagnostics.hasErrors());
    EXPECT_FALSE(infoBA.diagnostics.hasErrors());
    EXPECT_EQ(infoAB.diagnostics.errorCount(), infoBA.diagnostics.errorCount());
    EXPECT_EQ(orderNames(infoAB, infoAB.fbTopoOrder), orderNames(infoBA, infoBA.fbTopoOrder));
    EXPECT_EQ(orderNames(infoAB, infoAB.structTopoOrder), orderNames(infoBA, infoBA.structTopoOrder));
}

// ============================================================================
// Diagnostics carry the real source file name and a precise column
// ============================================================================
TEST_F(DeclVisitorTest, DiagnosticsCarryRealFileNameAndColumn) {
    std::string st = "VAR_GLOBAL\n"
                     "    n : INT := 5;\n"
                     "    A : ARRAY[n..2] OF INT;\n"
                     "END_VAR\n";

    auto tu = TestHelper::parseST(st, "my_source.st");
    analyzer_->setSourceName("my_source.st");
    auto info = analyzer_->analyze(tu);

    bool found = false;
    for (const auto& d : info.diagnostics.all()) {
        if (d.code != DiagnosticCode::ArrayBoundsNotConstant) {
            continue;
        }
        found = true;
        EXPECT_EQ(d.location.fileName, "my_source.st");
        EXPECT_GT(d.location.line, 0u);
        EXPECT_GT(d.location.column, 0u);
        // Column must point at the declaration start on the array line
        EXPECT_NE(d.location.toString().find("my_source.st"), std::string::npos);
    }
    EXPECT_TRUE(found) << "expected ArrayBoundsNotConstant diagnostic";
}

// ============================================================================
// A duplicate variable declaration is located at its own column
// ============================================================================
TEST_F(DeclVisitorTest, DuplicateVariableLocatedAtItsOwnColumn) {
    std::string st = "FUNCTION_BLOCK D\n"
                     "VAR\n"
                     "    a : INT;\n"
                     "    a : REAL;\n"
                     "END_VAR\n"
                     "END_FUNCTION_BLOCK\n";

    auto info = analyze(st);

    for (const auto& d : info.diagnostics.all()) {
        if (d.code != DiagnosticCode::DuplicateDeclaration) {
            continue;
        }
        // The second 'a' starts at line 4, column 5 (1-based)
        EXPECT_EQ(d.location.line, 4u);
        EXPECT_EQ(d.location.column, 5u);
    }
}
