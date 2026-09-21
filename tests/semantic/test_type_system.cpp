/**
 * @file test_type_system.cpp
 * @brief Tests for TypeSystem
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "semantic/TypeSystem.h"
#include "semantic/SymbolTable.h"

using namespace st2cpp::semantic;

class TypeSystemTest : public ::testing::Test {
protected:
    SymbolTable symTab;
};

TEST_F(TypeSystemTest, ElementaryTypeFactories) {
    // BOOL
    TypeInfo boolType = TypeInfo::makeBool();
    EXPECT_EQ(boolType.baseType, BaseType::BOOL);
    EXPECT_TRUE(boolType.isBitType);
    EXPECT_FALSE(boolType.isNumeric);
    EXPECT_EQ(boolType.sizeInBytes, 1);
    EXPECT_EQ(boolType.alignment, 1);
    
    // Signed integers
    TypeInfo sint = TypeInfo::makeInt(BaseType::SINT);
    EXPECT_EQ(sint.baseType, BaseType::SINT);
    EXPECT_EQ(sint.sizeInBytes, 1);
    EXPECT_TRUE(sint.isSigned);
    
    TypeInfo intType = TypeInfo::makeInt(BaseType::INT);
    EXPECT_EQ(intType.sizeInBytes, 2);
    
    TypeInfo dint = TypeInfo::makeInt(BaseType::DINT);
    EXPECT_EQ(dint.sizeInBytes, 4);
    
    TypeInfo lint = TypeInfo::makeInt(BaseType::LINT);
    EXPECT_EQ(lint.sizeInBytes, 8);
    
    // Unsigned integers
    TypeInfo usint = TypeInfo::makeInt(BaseType::USINT);
    EXPECT_FALSE(usint.isSigned);
    
    // Reals
    TypeInfo real = TypeInfo::makeReal(BaseType::REAL);
    EXPECT_TRUE(real.isReal);
    EXPECT_EQ(real.sizeInBytes, 4);
    
    TypeInfo lreal = TypeInfo::makeReal(BaseType::LREAL);
    EXPECT_EQ(lreal.sizeInBytes, 8);
    
    // Bit strings
    TypeInfo byte = TypeInfo::makeBitString(BaseType::BYTE);
    EXPECT_TRUE(byte.isBitString);
    EXPECT_FALSE(byte.isSigned);
    
    TypeInfo word = TypeInfo::makeBitString(BaseType::WORD);
    EXPECT_EQ(word.sizeInBytes, 2);
    
    // Time types
    TypeInfo time = TypeInfo::makeTime(BaseType::TIME);
    EXPECT_TRUE(time.isTime);
    EXPECT_EQ(time.sizeInBytes, 4);
    
    TypeInfo tod = TypeInfo::makeTime(BaseType::TOD);
    EXPECT_EQ(tod.sizeInBytes, 8);
    
    // Strings
    TypeInfo str = TypeInfo::makeString(BaseType::STRING);
    EXPECT_TRUE(str.isString);
    EXPECT_EQ(str.sizeInBytes, 0); // Dynamic
    
    // Void
    TypeInfo voidType = TypeInfo::makeVoid();
    EXPECT_EQ(voidType.kind, TypeKind::Void);
    EXPECT_EQ(voidType.baseType, BaseType::VOID);
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_SameType) {
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* intType2 = symTab.getType(symTab.getTypeIdByName("INT"));
    
    EXPECT_TRUE(intType->isCompatibleWith(intType2, CompatContext::Assignment));
    EXPECT_TRUE(intType->isAssignableFrom(intType2));
    EXPECT_TRUE(intType->isImplicitlyConvertibleFrom(intType2));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_NumericWidening) {
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* dintType = symTab.getType(symTab.getTypeIdByName("DINT"));
    const TypeInfo* realType = symTab.getType(symTab.getTypeIdByName("REAL"));
    const TypeInfo* lrealType = symTab.getType(symTab.getTypeIdByName("LREAL"));
    
    // INT -> DINT (widening)
    EXPECT_TRUE(dintType->isAssignableFrom(intType));
    EXPECT_TRUE(dintType->isCompatibleWith(intType, CompatContext::Assignment));
    
    // REAL -> LREAL
    EXPECT_TRUE(lrealType->isAssignableFrom(realType));
    
    // Signed -> unsigned (C++ allows, may warn)
    TypeInfo* uintType = const_cast<TypeInfo*>(symTab.getType(symTab.getTypeIdByName("UINT")));
    TypeInfo* intTypePtr = const_cast<TypeInfo*>(intType);
    EXPECT_TRUE(uintType->isAssignableFrom(intTypePtr));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_BoolStrict) {
    const TypeInfo* boolType = symTab.getType(symTab.getTypeIdByName("BOOL"));
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    
    // BOOL not assignable from INT
    EXPECT_FALSE(boolType->isAssignableFrom(intType));
    EXPECT_FALSE(intType->isAssignableFrom(boolType));
    EXPECT_FALSE(boolType->isCompatibleWith(intType, CompatContext::Assignment));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_Arrays) {
    TypeId intId = symTab.getTypeIdByName("INT");
    TypeId dintId = symTab.getTypeIdByName("DINT");
    
    // Same element type, same dimensions -> compatible
    TypeInfo arr1 = TypeInfo::makeInt(BaseType::INT);
    arr1.kind = TypeKind::Array;
    arr1.elementTypeId = intId;
    arr1.dimensions = {{0, 10, true}};
    
    TypeInfo arr2 = TypeInfo::makeInt(BaseType::INT);
    arr2.kind = TypeKind::Array;
    arr2.elementTypeId = intId;
    arr2.dimensions = {{0, 10, true}};
    
    EXPECT_TRUE(arr1.isCompatibleWith(&arr2, CompatContext::Assignment));
    EXPECT_TRUE(arr1.isAssignableFrom(&arr2));
    
    // Different dimensions -> not compatible
    TypeInfo arr3 = arr1;
    arr3.dimensions = {{0, 5, true}};
    EXPECT_FALSE(arr1.isCompatibleWith(&arr3, CompatContext::Assignment));
    
    // Different element type -> not compatible
    TypeInfo arr4 = arr1;
    arr4.elementTypeId = dintId;
    EXPECT_FALSE(arr1.isCompatibleWith(&arr4, CompatContext::Assignment));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_StructNominal) {
    TypeId structId = symTab.getTypeIdByName("BOOL"); // dummy
    SymbolId structSymId = symTab.declare("MyStruct", SymbolKind::Type, structId);
    
    TypeInfo struct1;
    struct1.kind = TypeKind::Struct;
    struct1.symbolId = structSymId;
    struct1.name = "MyStruct";
    
    TypeInfo struct2 = struct1; // Same symbol
    EXPECT_TRUE(struct1.isCompatibleWith(&struct2, CompatContext::Assignment));
    EXPECT_TRUE(struct1.isAssignableFrom(&struct2));
    
    // Different struct symbol -> not compatible
    SymbolId otherSym = symTab.declare("OtherStruct", SymbolKind::Type, structId);
    TypeInfo struct3 = struct1;
    struct3.symbolId = otherSym;
    EXPECT_FALSE(struct1.isCompatibleWith(&struct3, CompatContext::Assignment));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_Pointers) {
    TypeId intId = symTab.getTypeIdByName("INT");
    
    TypeInfo ptr1;
    ptr1.kind = TypeKind::Pointer;
    ptr1.pointedTypeId = intId;
    
    TypeInfo ptr2 = ptr1;
    EXPECT_TRUE(ptr1.isCompatibleWith(&ptr2, CompatContext::Assignment));
    
    TypeId boolId = symTab.getTypeIdByName("BOOL");
    TypeInfo ptr3 = ptr1;
    ptr3.pointedTypeId = boolId;
    EXPECT_FALSE(ptr1.isCompatibleWith(&ptr3, CompatContext::Assignment));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_References) {
    TypeId intId = symTab.getTypeIdByName("INT");
    
    TypeInfo ref1;
    ref1.kind = TypeKind::Reference;
    ref1.pointedTypeId = intId;
    
    TypeInfo ref2 = ref1;
    EXPECT_TRUE(ref1.isCompatibleWith(&ref2, CompatContext::Assignment));
    EXPECT_TRUE(ref1.isAssignableFrom(&ref2));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_Void) {
    const TypeInfo* voidType = symTab.getType(symTab.getTypeIdByName("VOID"));
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    
    EXPECT_FALSE(voidType->isAssignableFrom(intType));
    EXPECT_FALSE(intType->isAssignableFrom(voidType));
    EXPECT_FALSE(voidType->isCompatibleWith(intType, CompatContext::Assignment));
    EXPECT_TRUE(voidType->isAssignableFrom(voidType));
}

TEST_F(TypeSystemTest, TypeInfoCompatibility_Unknown) {
    TypeInfo unknown = TypeInfo::makeUnknown();
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    
    // Unknown compatible with anything during resolution
    EXPECT_TRUE(unknown.isCompatibleWith(intType, CompatContext::Assignment));
    EXPECT_TRUE(intType->isCompatibleWith(&unknown, CompatContext::Assignment));
    EXPECT_TRUE(unknown.isAssignableFrom(intType));
    EXPECT_TRUE(intType->isAssignableFrom(&unknown));
}

TEST_F(TypeSystemTest, ConversionGroupClassifier) {
    EXPECT_EQ(TypeInfo::makeBool().conversionGroup(), ConversionGroup::Boolean);
    EXPECT_EQ(TypeInfo::makeInt(BaseType::SINT).conversionGroup(), ConversionGroup::Integer);
    EXPECT_EQ(TypeInfo::makeInt(BaseType::UINT).conversionGroup(), ConversionGroup::Integer);
    EXPECT_EQ(TypeInfo::makeReal(BaseType::REAL).conversionGroup(), ConversionGroup::Real);
    EXPECT_EQ(TypeInfo::makeReal(BaseType::LREAL).conversionGroup(), ConversionGroup::Real);
    EXPECT_EQ(TypeInfo::makeBitString(BaseType::BYTE).conversionGroup(), ConversionGroup::BitString);
    EXPECT_EQ(TypeInfo::makeBitString(BaseType::LWORD).conversionGroup(), ConversionGroup::BitString);
    EXPECT_EQ(TypeInfo::makeTime(BaseType::TIME).conversionGroup(), ConversionGroup::Time);
    EXPECT_EQ(TypeInfo::makeTime(BaseType::DATE).conversionGroup(), ConversionGroup::Time);
    EXPECT_EQ(TypeInfo::makeString(BaseType::STRING).conversionGroup(), ConversionGroup::String);
    EXPECT_EQ(TypeInfo::makeString(BaseType::WSTRING).conversionGroup(), ConversionGroup::String);
    EXPECT_EQ(TypeInfo::makeUnknown().conversionGroup(), ConversionGroup::Unknown);
    EXPECT_EQ(TypeInfo::makeVoid().conversionGroup(), ConversionGroup::Other);

    TypeInfo arr = TypeInfo::makeInt(BaseType::INT);
    arr.kind = TypeKind::Array;
    EXPECT_EQ(arr.conversionGroup(), ConversionGroup::Other);
}

TEST_F(TypeSystemTest, AssignmentPermissiveBaseline) {
    // Permissive assignment baseline (C++ delegates conversions, project decision):
    // integer widening, narrowing, sign change, integer<->real and bit-string
    // cross-conversions all remain assignable.
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* dintType = symTab.getType(symTab.getTypeIdByName("DINT"));
    const TypeInfo* realType = symTab.getType(symTab.getTypeIdByName("REAL"));
    const TypeInfo* byteType = symTab.getType(symTab.getTypeIdByName("BYTE"));
    const TypeInfo* wordType = symTab.getType(symTab.getTypeIdByName("WORD"));
    const TypeInfo* boolType = symTab.getType(symTab.getTypeIdByName("BOOL"));

    EXPECT_TRUE(intType->isAssignableFrom(intType));
    EXPECT_TRUE(intType->isAssignableFrom(dintType));           // narrowing
    EXPECT_TRUE(dintType->isAssignableFrom(intType));           // widening
    EXPECT_TRUE(realType->isAssignableFrom(intType));           // int -> real
    EXPECT_TRUE(intType->isAssignableFrom(realType));           // real -> int
    EXPECT_TRUE(byteType->isAssignableFrom(wordType));          // narrowing bit-string
    EXPECT_TRUE(wordType->isAssignableFrom(byteType));          // widening bit-string
    EXPECT_TRUE(wordType->isAssignableFrom(intType));           // int -> bit-string
    EXPECT_TRUE(intType->isAssignableFrom(byteType));           // bit-string -> int
    EXPECT_FALSE(boolType->isAssignableFrom(intType));          // BOOL stays strict
    EXPECT_FALSE(intType->isAssignableFrom(boolType));
}

TEST_F(TypeSystemTest, ImplicitConversionRules) {
    // IEC 61131-3 implicit (value-preserving) conversions. These are the rules
    // consumed by Strict mode and expression contexts; isAssignableFrom is the
    // permissive baseline that delegates the remaining conversions to C++.
    const TypeInfo* sint = symTab.getType(symTab.getTypeIdByName("SINT"));
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* dint = symTab.getType(symTab.getTypeIdByName("DINT"));
    const TypeInfo* usint = symTab.getType(symTab.getTypeIdByName("USINT"));
    const TypeInfo* uint = symTab.getType(symTab.getTypeIdByName("UINT"));
    const TypeInfo* real = symTab.getType(symTab.getTypeIdByName("REAL"));
    const TypeInfo* lreal = symTab.getType(symTab.getTypeIdByName("LREAL"));
    const TypeInfo* byte = symTab.getType(symTab.getTypeIdByName("BYTE"));
    const TypeInfo* word = symTab.getType(symTab.getTypeIdByName("WORD"));
    const TypeInfo* dword = symTab.getType(symTab.getTypeIdByName("DWORD"));
    const TypeInfo* boolType = symTab.getType(symTab.getTypeIdByName("BOOL"));
    const TypeInfo* time = symTab.getType(symTab.getTypeIdByName("TIME"));
    const TypeInfo* date = symTab.getType(symTab.getTypeIdByName("DATE"));
    const TypeInfo* str = symTab.getType(symTab.getTypeIdByName("STRING"));
    const TypeInfo* wstr = symTab.getType(symTab.getTypeIdByName("WSTRING"));

    // Integer -> same-sign wider rank
    EXPECT_TRUE(dint->isImplicitlyConvertibleFrom(sint));
    EXPECT_TRUE(dint->isImplicitlyConvertibleFrom(intType));
    EXPECT_TRUE(intType->isImplicitlyConvertibleFrom(sint));
    EXPECT_TRUE(intType->isImplicitlyConvertibleFrom(intType));
    EXPECT_FALSE(sint->isImplicitlyConvertibleFrom(intType));   // narrowing
    EXPECT_FALSE(intType->isImplicitlyConvertibleFrom(dint));   // narrowing

    // Sign change is NOT implicit (would move value into unsigned/negative range)
    EXPECT_TRUE(uint->isImplicitlyConvertibleFrom(usint));
    EXPECT_TRUE(uint->isImplicitlyConvertibleFrom(uint));
    EXPECT_FALSE(uint->isImplicitlyConvertibleFrom(intType));   // INT -> UINT
    EXPECT_FALSE(intType->isImplicitlyConvertibleFrom(uint));   // UINT -> INT
    EXPECT_FALSE(intType->isImplicitlyConvertibleFrom(usint));  // USINT -> INT

    // Integer -> Real widening
    EXPECT_TRUE(real->isImplicitlyConvertibleFrom(sint));
    EXPECT_TRUE(real->isImplicitlyConvertibleFrom(dint));
    EXPECT_TRUE(lreal->isImplicitlyConvertibleFrom(dint));
    EXPECT_FALSE(intType->isImplicitlyConvertibleFrom(real));   // Real -> Integer never
    EXPECT_FALSE(real->isImplicitlyConvertibleFrom(lreal));     // LREAL -> REAL narrowing
    EXPECT_TRUE(lreal->isImplicitlyConvertibleFrom(real));      // REAL -> LREAL widening

    // Bit string -> same-rank-or-wider bit string (its own group, per IEC)
    EXPECT_TRUE(word->isImplicitlyConvertibleFrom(byte));
    EXPECT_TRUE(dword->isImplicitlyConvertibleFrom(byte));
    EXPECT_TRUE(word->isImplicitlyConvertibleFrom(word));
    EXPECT_FALSE(byte->isImplicitlyConvertibleFrom(word));      // narrowing
    // No implicit BitString <-> numeric conversion (IEC 61131-3)
    EXPECT_FALSE(byte->isImplicitlyConvertibleFrom(intType));
    EXPECT_FALSE(intType->isImplicitlyConvertibleFrom(word));
    EXPECT_FALSE(word->isImplicitlyConvertibleFrom(uint));

    // BOOL, Time (exact base type), String (exact base type)
    EXPECT_TRUE(boolType->isImplicitlyConvertibleFrom(boolType));
    EXPECT_FALSE(boolType->isImplicitlyConvertibleFrom(intType));
    EXPECT_TRUE(time->isImplicitlyConvertibleFrom(time));
    EXPECT_FALSE(time->isImplicitlyConvertibleFrom(date));
    EXPECT_FALSE(time->isImplicitlyConvertibleFrom(intType));
    EXPECT_TRUE(str->isImplicitlyConvertibleFrom(str));
    EXPECT_FALSE(str->isImplicitlyConvertibleFrom(wstr));
    EXPECT_FALSE(str->isImplicitlyConvertibleFrom(intType));
}

TEST_F(TypeSystemTest, ImplicitConversionNamedAndUnknown) {
    // Nominal types: implicit only for the exact same symbol
    TypeId structId = symTab.getTypeIdByName("BOOL"); // dummy id
    SymbolId structSymId = symTab.declare("MyStruct", SymbolKind::Type, structId);
    TypeInfo struct1;
    struct1.kind = TypeKind::Struct;
    struct1.symbolId = structSymId;
    struct1.name = "MyStruct";
    TypeInfo struct2 = struct1;
    EXPECT_TRUE(struct1.isImplicitlyConvertibleFrom(&struct2));

    TypeInfo other;
    other.kind = TypeKind::Struct;
    other.symbolId = symTab.declare("OtherStruct", SymbolKind::Type, structId);
    other.name = "OtherStruct";
    EXPECT_FALSE(struct1.isImplicitlyConvertibleFrom(&other));

    // Unknown: compatible with anything during resolution
    TypeInfo unknown = TypeInfo::makeUnknown();
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    EXPECT_TRUE(unknown.isImplicitlyConvertibleFrom(intType));
    EXPECT_TRUE(intType->isImplicitlyConvertibleFrom(&unknown));
}

TEST_F(TypeSystemTest, BinaryOpRejectsRealBitwiseAndMod) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};

    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* wordType = symTab.getType(symTab.getTypeIdByName("WORD"));
    const TypeInfo* realType = symTab.getType(symTab.getTypeIdByName("REAL"));
    const TypeInfo* boolType = symTab.getType(symTab.getTypeIdByName("BOOL"));

    // Integer bitwise OK, produces the common integer type
    TypeId t = TypeChecker::checkBinaryOp("AND", intType, intType, loc, diag, symTab);
    EXPECT_EQ(t, symTab.getTypeIdByName("INT"));
    EXPECT_FALSE(diag.hasErrors());

    // Bit-string bitwise OK (same group), wider wins
    t = TypeChecker::checkBinaryOp("XOR", wordType, intType, loc, diag, symTab);
    EXPECT_NE(t, 0u);
    EXPECT_FALSE(diag.hasErrors());

    // BOOL logical OK
    t = TypeChecker::checkBinaryOp("OR", boolType, boolType, loc, diag, symTab);
    EXPECT_EQ(t, symTab.getTypeIdByName("BOOL"));
    EXPECT_FALSE(diag.hasErrors());

    // REAL bitwise must be rejected (C++ '&'/'|'/'^' do not compile on floats)
    Diagnostics diag2;
    t = TypeChecker::checkBinaryOp("AND", realType, realType, loc, diag2, symTab);
    EXPECT_EQ(t, 0u);
    EXPECT_TRUE(diag2.hasErrors());
    bool found = false;
    for (const auto& d : diag2.all()) {
        if (d.code == DiagnosticCode::InvalidBinaryOperands) found = true;
    }
    EXPECT_TRUE(found);

    // MOD on integers OK
    Diagnostics diag3;
    t = TypeChecker::checkBinaryOp("MOD", intType, intType, loc, diag3, symTab);
    EXPECT_EQ(t, symTab.getTypeIdByName("INT"));
    EXPECT_FALSE(diag3.hasErrors());

    // MOD on REAL must be rejected (C++ '%' does not compile on floats)
    Diagnostics diag4;
    t = TypeChecker::checkBinaryOp("MOD", realType, realType, loc, diag4, symTab);
    EXPECT_EQ(t, 0u);
    EXPECT_TRUE(diag4.hasErrors());
    found = false;
    for (const auto& d : diag4.all()) {
        if (d.code == DiagnosticCode::InvalidBinaryOperands) found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(TypeSystemTest, ArrayIndexRejectsRealIndex) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 11, 7};

    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* realType = symTab.getType(symTab.getTypeIdByName("REAL"));

    TypeInfo arr = TypeInfo::makeInt(BaseType::INT);
    arr.kind = TypeKind::Array;
    arr.name = "ARR";
    arr.sizeInBytes = 20;
    TypeId intId = symTab.getTypeIdByName("INT");
    TypeId realId = symTab.getTypeIdByName("REAL");
    arr.elementTypeId = intId;
    (void)realId;

    auto r1 = TypeChecker::checkIndexAccess(arr, *intType, symTab, diag, loc);
    EXPECT_TRUE(r1.valid);
    EXPECT_EQ(r1.elementTypeId, intId);
    EXPECT_FALSE(diag.hasErrors());

    Diagnostics diag2;
    auto r2 = TypeChecker::checkIndexAccess(arr, *realType, symTab, diag2, loc);
    EXPECT_FALSE(r2.valid);
    EXPECT_TRUE(diag2.hasErrors());
}

TEST_F(TypeSystemTest, TypeCheckerHelpers) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};
    
    const TypeInfo* intType = symTab.getType(symTab.getTypeIdByName("INT"));
    const TypeInfo* dintType = symTab.getType(symTab.getTypeIdByName("DINT"));
    (void)intType; (void)dintType; // Used in getArithmeticCommonType calls below
    
    // Boolean check
    EXPECT_TRUE(TypeChecker::isBoolType(symTab.getTypeIdByName("BOOL"), symTab));
    EXPECT_FALSE(TypeChecker::isBoolType(symTab.getTypeIdByName("INT"), symTab));
    
    // Arithmetic common type
    TypeId common = TypeChecker::getArithmeticCommonType(
        symTab.getType(symTab.getTypeIdByName("INT")),
        symTab.getType(symTab.getTypeIdByName("DINT")),
        symTab
    );
    EXPECT_EQ(common, symTab.getTypeIdByName("DINT"));
    
    common = TypeChecker::getArithmeticCommonType(
        symTab.getType(symTab.getTypeIdByName("INT")),
        symTab.getType(symTab.getTypeIdByName("REAL")),
        symTab
    );
    EXPECT_EQ(common, symTab.getTypeIdByName("REAL"));
    
    common = TypeChecker::getArithmeticCommonType(
        symTab.getType(symTab.getTypeIdByName("REAL")),
        symTab.getType(symTab.getTypeIdByName("LREAL")),
        symTab
    );
    EXPECT_EQ(common, symTab.getTypeIdByName("LREAL"));
}