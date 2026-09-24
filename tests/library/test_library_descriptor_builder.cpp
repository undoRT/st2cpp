/**
 * @file test_library_descriptor_builder.cpp
 * @brief Tests for LibraryDescriptorBuilder:
 *        ST + SemanticInfo -> semantic-only LibraryDescriptor -> JSON -> reload
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <string>
#include "tests/helpers/TestHelper.h"
#include "library/LibraryLoader.h"
#include "library/LibrarySerializer.h"
#include "library/LibraryRegistry.h"
#include "semantic/LibraryDescriptorBuilder.h"
#include "semantic/LibrarySymbolImporter.h"

using namespace st2cpp::semantic;
using namespace st2cpp::library;

namespace {

struct Options {
    static LibraryExportOptions make() {
        LibraryExportOptions o;
        o.id = "mylib";
        o.name = "MyLib";
        o.version = "1.2.3";
        o.description = "Exported ST library";
        return o;
    }
};

/// Build descriptor from ST through the semantic pipeline.
LibraryExportResult build(const std::string& st, LibraryExportOptions opts = Options::make())
{
    return TestHelper::buildDescriptorFromST(st, opts);
}

/// Round-trip: descriptor -> JSON -> loader -> descriptor. Returns the reloaded
/// descriptor when the JSON is valid and stable.
bool roundTrips(const LibraryDescriptor& d, LibraryDescriptor& out)
{
    const std::string json = LibrarySerializer::toJson(d, 2);
    LibraryLoadResult r = LibraryLoader::fromString(json);
    if (!r.ok() || !r.descriptor) {
        return false;
    }
    out = *r.descriptor;
    return LibrarySerializer::toJson(out, 2) == json;
}

/// Build a loaded library from raw JSON and register it in a registry.
void registerJsonLibrary(LibraryRegistry& registry, const std::string& json,
                         const std::string& label)
{
    LibraryLoadResult r = LibraryLoader::fromString(json);
    ASSERT_TRUE(r.ok()) << label << " invalid: " << (r.errors.empty() ? "?" : r.errors[0].message);
    std::string error;
    ASSERT_TRUE(registry.registerLibrary(*r.descriptor, error)) << label << " register: " << error;
}

} // namespace

// ============================================================================
// Metadata & options validation
// ============================================================================

TEST(LibraryDescriptorBuilder, MetadataEmitted)
{
    auto r = build(R"(
VAR_GLOBAL CONSTANT x : INT := 5; END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    const LibraryDescriptor& d = *r.descriptor;
    EXPECT_EQ(d.schemaVersion, "1.0");
    EXPECT_EQ(d.id, "mylib");
    EXPECT_EQ(d.name, "MyLib");
    EXPECT_EQ(d.version, "1.2.3");
    EXPECT_EQ(d.description, "Exported ST library");
}

TEST(LibraryDescriptorBuilder, EmptyIdIsError)
{
    auto o = Options::make();
    o.id.clear();
    auto r = build("", o);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.descriptor->id.empty()); // mirrors the (empty) requested id
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "options" && e.message.find("id") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, InvalidVersionIsError)
{
    auto o = Options::make();
    o.version = "not-a-version";
    auto r = build("", o);
    EXPECT_FALSE(r.ok());
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "options" && e.message.find("version") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// ENUM export
// ============================================================================

TEST(LibraryDescriptorBuilder, EnumExport)
{
    auto r = build(R"(
TYPE Color : (Red, Green := 5, Blue); END_TYPE
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    const EnumTypeDef* e = r.descriptor->findEnum("Color");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->members.size(), 3u);
    EXPECT_EQ(e->members[0].name, "Red");
    EXPECT_EQ(e->members[0].value, 0);
    EXPECT_EQ(e->members[1].name, "Green");
    EXPECT_EQ(e->members[1].value, 5);
    EXPECT_EQ(e->members[2].name, "Blue");
    EXPECT_EQ(e->members[2].value, 6);
    EXPECT_EQ(e->baseType.kind, TypeRefKind::Primitive);
    EXPECT_EQ(e->baseType.primitive, BaseType::INT);
    EXPECT_EQ(e->initValue.kind, InitKind::Scalar);
    EXPECT_EQ(e->initValue.scalar, "Red");
}

TEST(LibraryDescriptorBuilder, EnumNonLiteralExplicitValueIsError)
{
    auto r = build(R"(
VAR_GLOBAL y : INT; END_VAR
TYPE Bad : (A, B := y); END_TYPE
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "enum Bad.B" && e.message.find("decimal") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, EnumNegativeExplicitValue)
{
    auto r = build(R"(
TYPE Signed : (Neg := -1, Zero, Pos := 1); END_TYPE
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    const EnumTypeDef* e = r.descriptor->findEnum("Signed");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->members[0].value, -1);
    EXPECT_EQ(e->members[1].value, 0);
    EXPECT_EQ(e->members[2].value, 1);
}

// ============================================================================
// STRUCT export
// ============================================================================

TEST(LibraryDescriptorBuilder, StructExport)
{
    auto r = build(R"(
TYPE Point : STRUCT
   x : INT;
   y : INT := 7;
END_STRUCT END_TYPE
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    const StructTypeDef* s = r.descriptor->findStruct("Point");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 2u);
    EXPECT_EQ(s->fields[0].name, "x");
    EXPECT_EQ(s->fields[0].type.kind, TypeRefKind::Primitive);
    EXPECT_EQ(s->fields[0].type.primitive, BaseType::INT);
    EXPECT_EQ(s->fields[0].initValue.kind, InitKind::None);
    EXPECT_EQ(s->fields[1].name, "y");
    EXPECT_EQ(s->fields[1].initValue.kind, InitKind::Scalar);
    EXPECT_EQ(s->fields[1].initValue.scalar, "7");
}

TEST(LibraryDescriptorBuilder, StructStringLengthIsError)
{
    auto r = build(R"(
TYPE Bag : STRUCT
   name : STRING[12];
END_STRUCT END_TYPE
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("STRING[n]") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, StructArrayField)
{
    auto r = build(R"(
TYPE Row : STRUCT
   v : ARRAY[0..3] OF REAL;
END_STRUCT END_TYPE
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    const StructTypeDef* s = r.descriptor->findStruct("Row");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->fields.size(), 1u);
    EXPECT_EQ(s->fields[0].type.kind, TypeRefKind::Array);
    EXPECT_EQ(s->fields[0].type.lowerBound, 0);
    EXPECT_EQ(s->fields[0].type.upperBound, 3);
    ASSERT_NE(s->fields[0].type.elementType, nullptr);
    EXPECT_EQ(s->fields[0].type.elementType->kind, TypeRefKind::Primitive);
    EXPECT_EQ(s->fields[0].type.elementType->primitive, BaseType::REAL);
}

TEST(LibraryDescriptorBuilder, MultiDimArrayIsError)
{
    auto r = build(R"(
VAR_GLOBAL m : ARRAY[0..1, 0..2] OF INT; END_VAR
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("multi-dimensional") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, PointerTypeIsError)
{
    auto r = build(R"(
VAR_GLOBAL p : POINTER TO INT; END_VAR
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("POINTER") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// Globals & constants
// ============================================================================

TEST(LibraryDescriptorBuilder, VarGlobalConstantExported)
{
    auto r = build(R"(
VAR_GLOBAL CONSTANT MAX_CHANNELS : INT := 8; END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->constants.size(), 1u);
    const Constant& c = r.descriptor->constants[0];
    EXPECT_EQ(c.name, "MAX_CHANNELS");
    EXPECT_EQ(c.type.kind, TypeRefKind::Primitive);
    EXPECT_EQ(c.type.primitive, BaseType::INT);
    EXPECT_EQ(c.value.kind, InitKind::Scalar);
    EXPECT_EQ(c.value.scalar, "8");
    EXPECT_FALSE(c.hasCppBinding);
}

TEST(LibraryDescriptorBuilder, ConstantWithoutValueIsError)
{
    auto r = build(R"(
VAR_GLOBAL CONSTANT C : INT; END_VAR
)");
    ASSERT_TRUE(r.descriptor);
    EXPECT_EQ(r.descriptor->constants.size(), 0u);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "global C" && e.message.find("value") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, PlainGlobalExported)
{
    auto r = build(R"(
VAR_GLOBAL counter : INT := 1; END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->globalVariables.size(), 1u);
    const GlobalVariable& g = r.descriptor->globalVariables[0];
    EXPECT_EQ(g.name, "counter");
    EXPECT_EQ(g.scope, "global");
    EXPECT_FALSE(g.constant);
    EXPECT_EQ(g.initValue.kind, InitKind::Scalar);
    EXPECT_EQ(g.initValue.scalar, "1");
    EXPECT_EQ(r.descriptor->constants.size(), 0u);
}

TEST(LibraryDescriptorBuilder, GlobalArrayInitExported)
{
    auto r = build(R"(
VAR_GLOBAL arr : ARRAY[0..2] OF INT := [1, 2, 3]; END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->globalVariables.size(), 1u);
    const GlobalVariable& g = r.descriptor->globalVariables[0];
    ASSERT_EQ(g.type.kind, TypeRefKind::Array);
    EXPECT_EQ(g.initValue.kind, InitKind::List);
    ASSERT_EQ(g.initValue.list.size(), 3u);
    EXPECT_EQ(g.initValue.list[0].scalar, "1");
    EXPECT_EQ(g.initValue.list[2].scalar, "3");
}

TEST(LibraryDescriptorBuilder, GlobalInitializerScalars)
{
    auto r = build(R"(
VAR_GLOBAL
   negv : INT := -5;
   strv : STRING := 'Hello World';
   bv : BOOL := TRUE;
   tv : TIME := T#500ms;
END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->globalVariables.size(), 4u);
    EXPECT_EQ(r.descriptor->globalVariables[0].initValue.scalar, "-5");
    EXPECT_EQ(r.descriptor->globalVariables[1].initValue.scalar, "Hello World");
    EXPECT_EQ(r.descriptor->globalVariables[2].initValue.scalar, "true");
    EXPECT_EQ(r.descriptor->globalVariables[3].initValue.scalar, "T#500ms");
}

TEST(LibraryDescriptorBuilder, GlobalEnumInitWithIdentExpr)
{
    auto r = build(R"(
TYPE Color : (Red, Green, Blue); END_TYPE
VAR_GLOBAL g : Color := Red; END_VAR
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->globalVariables.size(), 1u);
    const GlobalVariable& g = r.descriptor->globalVariables[0];
    EXPECT_EQ(g.initValue.kind, InitKind::Scalar);
    EXPECT_EQ(g.initValue.scalar, "Red");
    EXPECT_EQ(g.type.kind, TypeRefKind::Named);
    EXPECT_EQ(g.type.name, "Color");
    EXPECT_TRUE(g.type.library.empty());
}

TEST(LibraryDescriptorBuilder, GlobalRetainIsError)
{
    auto r = build(R"(
VAR_GLOBAL RETAIN keep : INT; END_VAR
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("RETAIN") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, GlobalAtIsError)
{
    auto r = build(R"(
VAR_GLOBAL mapped AT %MW10 : INT; END_VAR
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("AT (externally mapped)") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// FUNCTION export
// ============================================================================

TEST(LibraryDescriptorBuilder, FunctionExport)
{
    auto r = build(R"(
FUNCTION Clamp : INT
VAR_INPUT value, lo, hi : INT; END_VAR
VAR_OUTPUT result : INT; END_VAR
VAR_TEMP t : INT; END_VAR
END_FUNCTION
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->functions.size(), 1u);
    const FunctionDef& f = r.descriptor->functions[0];
    EXPECT_EQ(f.name, "Clamp");
    EXPECT_FALSE(f.hasCppBinding);
    EXPECT_EQ(f.returnType.kind, TypeRefKind::Primitive);
    EXPECT_EQ(f.returnType.primitive, BaseType::INT);
    ASSERT_EQ(f.parameters.size(), 4u);
    EXPECT_EQ(f.parameters[0].name, "value");
    EXPECT_EQ(f.parameters[0].direction, ParamDirection::In);
    EXPECT_EQ(f.parameters[3].name, "result");
    EXPECT_EQ(f.parameters[3].direction, ParamDirection::Out);
}

TEST(LibraryDescriptorBuilder, FunctionParamDefault)
{
    auto r = build(R"(
FUNCTION WithDefault : INT
VAR_INPUT p : INT := 5; END_VAR
END_FUNCTION
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->functions.size(), 1u);
    const FunctionDef& f = r.descriptor->functions[0];
    ASSERT_EQ(f.parameters.size(), 1u);
    EXPECT_EQ(f.parameters[0].name, "p");
    EXPECT_EQ(f.parameters[0].initValue.kind, InitKind::Scalar);
    EXPECT_EQ(f.parameters[0].initValue.scalar, "5");
}

TEST(LibraryDescriptorBuilder, FunctionWithoutReturnIsError)
{
    auto r = build(R"(
FUNCTION Bad
VAR_INPUT x : INT; END_VAR
END_FUNCTION
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "function Bad" && e.message.find("return type") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, FunctionExternalVarIsError)
{
    auto r = build(R"(
FUNCTION F : INT
VAR_EXTERNAL x : INT; END_VAR
END_FUNCTION
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("VAR_EXTERNAL") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, FunctionStringReturnIsError)
{
    auto r = build(R"(
FUNCTION F : STRING[10]
END_FUNCTION
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("STRING[n]") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// FUNCTION_BLOCK export
// ============================================================================

TEST(LibraryDescriptorBuilder, FunctionBlockExport)
{
    auto r = build(R"(
FUNCTION_BLOCK Ton
VAR_INPUT in : BOOL; END_VAR
VAR_OUTPUT q : BOOL; END_VAR
VAR h : TIME; END_VAR
END_FUNCTION_BLOCK
)");
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->functionBlocks.size(), 1u);
    const FunctionBlockDef& fb = r.descriptor->functionBlocks[0];
    EXPECT_EQ(fb.name, "Ton");
    EXPECT_FALSE(fb.hasCppBinding);
    ASSERT_EQ(fb.parameters.size(), 2u);
    EXPECT_EQ(fb.parameters[0].name, "in");
    EXPECT_EQ(fb.parameters[0].direction, ParamDirection::In);
    EXPECT_EQ(fb.parameters[1].name, "q");
    EXPECT_EQ(fb.parameters[1].direction, ParamDirection::Out);
}

TEST(LibraryDescriptorBuilder, FunctionBlockWithMethodsIsError)
{
    auto r = build(R"(
FUNCTION_BLOCK Dev
VAR_INPUT x : INT; END_VAR
METHOD Reset
END_METHOD
END_FUNCTION_BLOCK
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("methods") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, FunctionBlockExtendsIsError)
{
    auto r = build(R"(
FUNCTION_BLOCK Base
END_FUNCTION_BLOCK
FUNCTION_BLOCK Derived EXTENDS Base
END_FUNCTION_BLOCK
)");
    ASSERT_TRUE(r.descriptor);
    ASSERT_EQ(r.descriptor->functionBlocks.size(), 1u);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.message.find("EXTENDS") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// Unsupported top-level constructs
// ============================================================================

TEST(LibraryDescriptorBuilder, ProgramIsError)
{
    auto r = build(R"(
PROGRAM Main
VAR x : INT; END_VAR
END_PROGRAM
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "program Main" && e.message.find("PROGRAM") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, TypeAliasIsError)
{
    auto r = build(R"(
TYPE MyInt : INT; END_TYPE
)");
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "type alias MyInt" && e.message.find("alias") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

// ============================================================================
// Dependencies
// ============================================================================

TEST(LibraryDescriptorBuilder, DependencyMissingPolicyIsError)
{
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "timerlib", "name": "TimerLib", "version": "1.0.0",
      "enums": [{ "name": "Mode", "baseType": { "kind": "primitive", "name": "INT" },
                  "members": [{ "name": "Off", "value": 0 }, { "name": "On", "value": 1 }] }]
    })", "timerlib");

    auto r = TestHelper::buildDescriptorFromST(R"(
VAR_GLOBAL m : Mode; END_VAR
)", Options::make(), registry);
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "dependencies" && e.message.find("missing version policy") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
    ASSERT_FALSE(r.descriptor->dependencies.empty());
    EXPECT_EQ(r.descriptor->dependencies[0].id, "timerlib");
}

TEST(LibraryDescriptorBuilder, DependencyInvalidConstraintIsError)
{
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "timerlib", "name": "TimerLib", "version": "1.0.0",
      "enums": [{ "name": "Mode", "baseType": { "kind": "primitive", "name": "INT" },
                  "members": [{ "name": "Off", "value": 0 }] }]
    })", "timerlib");

    auto o = Options::make();
    o.dependencyVersions["timerlib"] = "not a constraint!";
    auto r = TestHelper::buildDescriptorFromST(R"(
VAR_GLOBAL m : Mode; END_VAR
)", o, registry);
    ASSERT_TRUE(r.descriptor);
    bool found = false;
    for (const auto& e : r.errors) {
        if (e.entity == "dependencies" && e.message.find("invalid version constraint") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST(LibraryDescriptorBuilder, DependencyPolicyApplied)
{
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "timerlib", "name": "TimerLib", "version": "1.0.0",
      "enums": [{ "name": "Mode", "baseType": { "kind": "primitive", "name": "INT" },
                  "members": [{ "name": "Off", "value": 0 }, { "name": "On", "value": 1 }] }]
    })", "timerlib");

    auto o = Options::make();
    o.dependencyVersions["TiMeRlIb"] = "^1.0.0"; // case-insensitive policy lookup
    auto r = TestHelper::buildDescriptorFromST(R"(
VAR_GLOBAL m : Mode; END_VAR
)", o, registry);
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->dependencies.size(), 1u);
    const Dependency& d = r.descriptor->dependencies[0];
    EXPECT_EQ(d.id, "timerlib");
    EXPECT_TRUE(d.version.valid);
    EXPECT_EQ(d.version.raw, "^1.0.0");

    const GlobalVariable* g = r.descriptor->findGlobalVariable("m");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->type.kind, TypeRefKind::Named);
    EXPECT_EQ(g->type.library, "timerlib");
}

TEST(LibraryDescriptorBuilder, ExternalEnumeratorInInitAddsDependency)
{
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "timerlib", "name": "TimerLib", "version": "1.0.0",
      "enums": [{ "name": "Mode", "baseType": { "kind": "primitive", "name": "INT" },
                  "members": [{ "name": "Off", "value": 0 }, { "name": "On", "value": 1 }] }]
    })", "timerlib");

    auto o = Options::make();
    o.dependencyVersions["timerlib"] = "1.0.0";
    auto r = TestHelper::buildDescriptorFromST(R"(
VAR_GLOBAL m : Mode := On; END_VAR
)", o, registry);
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    // One dependency only: the enum type and its enumerator both resolve to the
    // same external library, and dependencies are deduplicated.
    ASSERT_EQ(r.descriptor->dependencies.size(), 1u);
    EXPECT_EQ(r.descriptor->dependencies[0].id, "timerlib");
    const GlobalVariable* g = r.descriptor->findGlobalVariable("m");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->initValue.kind, InitKind::Scalar);
    EXPECT_EQ(g->initValue.scalar, "On");
}

// ============================================================================
// Round trip & determinism
// ============================================================================

TEST(LibraryDescriptorBuilder, SerializeThenReloadIsStable)
{
    const std::string st = R"(
TYPE Color : (Red, Green := 5); END_TYPE
TYPE Point : STRUCT
   x : INT;
Nested : Color;
END_STRUCT END_TYPE
VAR_GLOBAL CONSTANT LIMIT : INT := 8; END_VAR
VAR_GLOBAL arr : ARRAY[0..1] OF INT := [1, 2]; END_VAR
FUNCTION Clamp : INT
VAR_INPUT v : INT; END_VAR
END_FUNCTION
FUNCTION_BLOCK Ton
VAR_INPUT in : BOOL; END_VAR
END_FUNCTION_BLOCK
)";
    auto r = build(st);
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();

    LibraryDescriptor loaded;
    ASSERT_TRUE(roundTrips(*r.descriptor, loaded));
    EXPECT_EQ(loaded.id, "mylib");
    EXPECT_EQ(loaded.enums.size(), 1u);
    EXPECT_EQ(loaded.types.size(), 1u);
    EXPECT_EQ(loaded.constants.size(), 1u);
    EXPECT_EQ(loaded.globalVariables.size(), 1u);
    EXPECT_EQ(loaded.functions.size(), 1u);
    EXPECT_EQ(loaded.functionBlocks.size(), 1u);
    const FunctionDef& f = *loaded.findFunction("Clamp");
    EXPECT_FALSE(f.hasCppBinding);
    const FunctionBlockDef& fb = *loaded.findFunctionBlock("Ton");
    EXPECT_FALSE(fb.hasCppBinding);
}

TEST(LibraryDescriptorBuilder, SameInputYieldsSameJson)
{
    const std::string st = R"(
TYPE Color : (Red, Green, Blue); END_TYPE
VAR_GLOBAL g : Color := Green; END_VAR
FUNCTION F : INT
VAR_INPUT p : INT := 1; END_VAR
END_FUNCTION
)";
    auto r1 = build(st);
    auto r2 = build(st);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(LibrarySerializer::toJson(*r1.descriptor, 2),
              LibrarySerializer::toJson(*r2.descriptor, 2));
}

// ============================================================================
// Import regression (B1): external FB types must cache their TypeId
// ============================================================================

TEST(LibraryDescriptorBuilder, ExternalFbTypeIsCachedOnImport)
{
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "timerlib", "name": "TimerLib", "version": "1.0.0",
      "functionBlocks": [{
         "name": "Timer",
         "parameters": [{ "name": "in", "direction": "IN",
                          "type": { "kind": "primitive", "name": "BOOL" } }]
      }]
    })", "timerlib");
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "userlib", "name": "UserLib", "version": "1.0.0",
      "dependencies": [{ "id": "timerlib", "version": "1.0.0" }],
      "globalVariables": [{
         "name": "g_timer",
         "scope": "global",
         "type": { "kind": "named", "name": "Timer", "library": "timerlib" }
      }]
    })", "userlib");

    Diagnostics diag;
    SymbolTable st;
    LibrarySymbolImporter importer(st, diag);
    importer.import(registry);

    const Symbol* g = st.get(st.lookupExternal("g_timer"));
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->isExternal, true);
    ASSERT_NE(g->typeId, 0u) << "external FB type must be resolved, not 0";

    const TypeInfo* ti = st.getType(g->typeId);
    ASSERT_NE(ti, nullptr);
    EXPECT_EQ(ti->kind, TypeKind::FunctionBlock);
    EXPECT_EQ(ti->name, "Timer");
}

TEST(LibraryDescriptorBuilder, ExternalStructFieldInEnumDependencyExport)
{
    // Guards the dependency cross-section emission order (structs before POUs).
    LibraryRegistry registry;
    registerJsonLibrary(registry, R"({
      "$schemaVersion": "1.0",
      "id": "stdlib", "name": "StdLib", "version": "1.0.0",
      "types": [{ "name": "Pair", "fields": [
          { "name": "a", "type": { "kind": "primitive", "name": "INT" } },
          { "name": "b", "type": { "kind": "primitive", "name": "INT" } }] }]
    })", "stdlib");

    auto o = Options::make();
    o.dependencyVersions["stdlib"] = ">=1.0.0 <2.0.0";
    auto r = TestHelper::buildDescriptorFromST(R"(
VAR_GLOBAL pair : Pair; END_VAR
)", o, registry);
    ASSERT_TRUE(r.ok()) << r.errors[0].toString();
    ASSERT_EQ(r.descriptor->dependencies.size(), 1u);
    EXPECT_EQ(r.descriptor->dependencies[0].id, "stdlib");
    EXPECT_EQ(r.descriptor->dependencies[0].version.raw, ">=1.0.0 <2.0.0");
}