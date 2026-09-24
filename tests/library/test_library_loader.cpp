/**
 * @file test_library_loader.cpp
 * @brief Tests for LibraryLoader: JSON -> LibraryDescriptor -> validation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <string>
#include "library/LibraryLoader.h"
#include "library/LibrarySerializer.h"

using namespace st2cpp::library;

namespace {

// Load from a raw JSON string, returning the parse result.
LibraryLoadResult loadText(const std::string& text)
{
   return LibraryLoader::fromString(text);
}

#define SAMPLES_DIR LIBRARY_SAMPLES_DIR

std::string samplePath(const char* name)
{
   return std::string(SAMPLES_DIR) + "/" + name;
}

} // namespace

// ============================================================================
// Negative: malformed input
// ============================================================================

TEST(LibraryLoader, MalformedJson)
{
   LibraryLoadResult r = loadText("{ not json ]");
   ASSERT_FALSE(r.ok());
   ASSERT_FALSE(r.descriptor);
   ASSERT_GE(r.errors.size(), 1u);
   EXPECT_FALSE(r.errors[0].message.empty());
}

TEST(LibraryLoader, NotAnObject)
{
   LibraryLoadResult r = loadText("[1, 2, 3]");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, MissingSchemaVersion)
{
   LibraryLoadResult r = loadText("{\"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\"}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, UnsupportedSchemaVersion)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"2.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\"}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Negative: identity
// ============================================================================

TEST(LibraryLoader, MissingId)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"name\": \"X\", \"version\": \"1.0.0\"}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, MissingName)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"version\": \"1.0.0\"}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, EmptyId)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"\", \"name\": \"X\", \"version\": \"1.0.0\"}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, InvalidVersion)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"not-a-version\"}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, InvalidVersionConstraint)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"dependencies\": [{\"id\": \"core\", \"version\": \"banana\"}]}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Negative: duplicates
// ============================================================================

TEST(LibraryLoader, DuplicateDependencyId)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"dependencies\": [{\"id\": \"a\", \"version\": \"1.0.0\"}, {\"id\": \"a\", \"version\": \"1.0.1\"}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, DuplicateConstantAcrossConstantAndGlobal)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"constants\": [{\"name\": \"FOO\", \"type\": {\"kind\": \"primitive\", \"name\": \"INT\"}, \"value\": 1}],"
      "\"globalVariables\": [{\"name\": \"FOO\", \"type\": {\"kind\": \"primitive\", \"name\": \"INT\"}, \"scope\": \"global\"}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, DuplicateEnumAndStructName)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"enums\": [{\"name\": \"Thing\", \"baseType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"members\": [{\"name\": \"A\", \"value\": 0}]}],"
                                  "\"types\": [{\"name\": \"THING\", \"kind\": \"struct\", \"fields\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, DuplicateEnumerators)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"enums\": [{\"name\": \"E\", \"baseType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"members\": [{\"name\": \"A\", \"value\": 0}, {\"name\": \"a\", \"value\": 1}]}]}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Negative: types and references
// ============================================================================

TEST(LibraryLoader, UnknownPrimitiveType)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"constants\": [{\"name\": \"C\", \"type\": {\"kind\": \"primitive\", \"name\": \"NOTA_TYPE\"}, \"value\": 1}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, UnknownNamedType)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"constants\": [{\"name\": \"C\", \"type\": {\"kind\": \"named\", \"name\": \"Ghost\"}, \"value\": 1}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, UndeclaredExternalLibrary)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"types\": [{\"name\": \"S\", \"kind\": \"struct\", \"fields\": ["
                                  "  {\"name\": \"F\", \"type\": {\"kind\": \"named\", \"library\": \"ghostlib\", \"name\": \"T\"}}]}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, SelfReferencingLibrary)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"mylib\", \"name\": \"MyLib\", \"version\": \"1.0.0\","
                                  "\"dependencies\": [{\"id\": \"mylib\", \"version\": \"1.0.0\"}],"
                                  "\"types\": [{\"name\": \"S\", \"kind\": \"struct\", \"fields\": ["
                                  "  {\"name\": \"F\", \"type\": {\"kind\": \"named\", \"library\": \"mylib\", \"name\": \"T\"}}]}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, UnknownEnumTypeReference)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"types\": [{\"name\": \"S\", \"kind\": \"struct\", \"fields\": ["
                                  "  {\"name\": \"F\", \"type\": {\"kind\": \"named\", \"name\": \"Ghost\"}}]}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, ArrayInvalidBounds)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"constants\": [{\"name\": \"C\", \"type\": {\"kind\": \"array\", \"lowerBound\": 5, \"upperBound\": 2,"
      "  \"elementType\": {\"kind\": \"primitive\", \"name\": \"INT\"}}, \"value\": 1}]}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Negative: enums and bindings
// ============================================================================

TEST(LibraryLoader, EnumInvalidBaseType)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"enums\": [{\"name\": \"E\", \"baseType\": {\"kind\": \"primitive\", \"name\": \"REAL\"},"
                                  "  \"members\": [{\"name\": \"A\", \"value\": 0}]}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, EnumMissingMemberValue)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"enums\": [{\"name\": \"E\", \"baseType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"members\": [{\"name\": \"A\"}]}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, EnumInvalidInitValue)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"enums\": [{\"name\": \"E\", \"baseType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"members\": [{\"name\": \"A\", \"value\": 0}],"
                                  "  \"initValue\": {\"kind\": \"scalar\", \"value\": \"GHOST\"}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, FreeFunctionMissingSymbol)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"functions\": [{\"name\": \"F\", \"returnType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"cppBinding\": {\"kind\": \"freeFunction\"}, \"parameters\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, StaticMethodMissingOwner)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"functions\": [{\"name\": \"F\", \"returnType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
                                  "  \"cppBinding\": {\"kind\": \"staticMethod\", \"symbol\": \"m\"}, \"parameters\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, FunctionBlockMissingInstanceType)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"functionBlocks\": [{\"name\": \"FB\", \"cppBinding\": {\"call\": \"process\"}, \"parameters\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, FunctionBlockMissingCall)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"functionBlocks\": [{\"name\": \"FB\", \"cppBinding\": {\"instanceType\": \"examplelib::Ton\"}, \"parameters\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, FunctionMissingReturnType)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"functions\": [{\"name\": \"F\","
                                  "  \"cppBinding\": {\"symbol\": \"f\", \"kind\": \"freeFunction\"}, \"parameters\": []}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, InvalidParameterDirection)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"functions\": [{\"name\": \"F\", \"returnType\": {\"kind\": \"primitive\", \"name\": \"INT\"},"
      "  \"cppBinding\": {\"symbol\": \"f\", \"kind\": \"freeFunction\"},"
      "  \"parameters\": [{\"name\": \"P\", \"type\": {\"kind\": \"primitive\", \"name\": \"INT\"}, \"direction\": \"SIDEWAYS\"}]}]}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Negative: initializers
// ============================================================================

TEST(LibraryLoader, GlobalMissingScope)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"globalVariables\": [{\"name\": \"G\", \"type\": {\"kind\": \"primitive\", \"name\": \"INT\"}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, ArrayRepeatInitializerTooLarge)
{
   LibraryLoadResult r = loadText(
      "{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
      "\"globalVariables\": [{\"name\": \"G\", \"scope\": \"global\","
      "  \"type\": {\"kind\": \"array\", \"lowerBound\": 0, \"upperBound\": 3,"
      "    \"elementType\": {\"kind\": \"primitive\", \"name\": \"INT\"}},"
      "  \"initValue\": {\"kind\": \"repeat\", \"count\": 9, \"value\": {\"kind\": \"scalar\", \"value\": 0}}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, ArrayListInitializerTooLarge)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"globalVariables\": [{\"name\": \"G\", \"scope\": \"global\","
                                  "  \"type\": {\"kind\": \"array\", \"lowerBound\": 0, \"upperBound\": 1,"
                                  "    \"elementType\": {\"kind\": \"primitive\", \"name\": \"INT\"}},"
                                  "  \"initValue\": {\"kind\": \"list\", \"values\": [1, 2, 3]}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, SparseEntryOutOfBounds)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"globalVariables\": [{\"name\": \"G\", \"scope\": \"global\","
                                  "  \"type\": {\"kind\": \"array\", \"lowerBound\": 0, \"upperBound\": 3,"
                                  "    \"elementType\": {\"kind\": \"primitive\", \"name\": \"INT\"}},"
                                  "  \"initValue\": {\"kind\": \"sparse\","
                                  "    \"entries\": [{\"range\": {\"lower\": 3, \"upper\": 5}, \"value\": 1}],"
                                  "    \"default\": 0}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, ScalarInitForStructType)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"types\": [{\"name\": \"S\", \"kind\": \"struct\", \"fields\": []}],"
                                  "\"globalVariables\": [{\"name\": \"G\", \"scope\": \"global\","
                                  "  \"type\": {\"kind\": \"named\", \"name\": \"S\"},"
                                  "  \"initValue\": {\"kind\": \"scalar\", \"value\": \"nope\"}}]}");
   EXPECT_FALSE(r.ok());
}

TEST(LibraryLoader, ConstantMissingValue)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"constants\": [{\"name\": \"C\", \"type\": {\"kind\": \"primitive\", \"name\": \"INT\"}}]}");
   EXPECT_FALSE(r.ok());
}

// ============================================================================
// Positive: core identity and dependencies
// ============================================================================

TEST(LibraryLoader, MinimumDescriptor)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"minimal\", \"name\": \"Minimal\", \"version\": \"0.1.0\"}");
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   ASSERT_TRUE(r.descriptor);
   EXPECT_EQ(r.descriptor->schemaVersion, "1.0");
   EXPECT_EQ(r.descriptor->id, "minimal");
   EXPECT_EQ(r.descriptor->name, "Minimal");
   EXPECT_EQ(r.descriptor->version, "0.1.0");
}

TEST(LibraryLoader, VersionWithPrereleaseAndBuild)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.2.3-beta.1+build7\"}");
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const Version v = Version::parse(r.descriptor->version);
   EXPECT_EQ(v.major, 1);
   EXPECT_EQ(v.minor, 2);
   EXPECT_EQ(v.patch, 3);
}

TEST(LibraryLoader, CaretConstraintParsed)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"dependencies\": [{\"id\": \"corelib\", \"version\": \"^1.0.0\"}]}");
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   ASSERT_EQ(r.descriptor->dependencies.size(), 1u);
   EXPECT_EQ(r.descriptor->dependencies[0].id, "corelib");
   ASSERT_GE(r.descriptor->dependencies[0].version.clauses.size(), 1u);
   EXPECT_EQ(r.descriptor->dependencies[0].version.clauses[0].op, VersionOp::Caret);
}

TEST(LibraryLoader, WildcardConstraintParsed)
{
   LibraryLoadResult r = loadText("{\"$schemaVersion\": \"1.0\", \"id\": \"x\", \"name\": \"X\", \"version\": \"1.0.0\","
                                  "\"dependencies\": [{\"id\": \"corelib\", \"version\": \"1.0.x\"}]}");
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const VersionClause& clause = r.descriptor->dependencies[0].version.clauses[0];
   EXPECT_EQ(clause.op, VersionOp::Wildcard);
   EXPECT_EQ(clause.version.patch, -1);
}

// ============================================================================
// Positive: full sample file
// ============================================================================

TEST(LibraryLoader, LoadFullSampleFromFile)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   ASSERT_TRUE(r.descriptor);

   const LibraryDescriptor& d = *r.descriptor;
   EXPECT_EQ(d.id, "examplelib");
   EXPECT_EQ(d.name, "ExampleLib");
   EXPECT_EQ(d.schemaVersion, "1.0");
   EXPECT_EQ(d.cppBinding.include, "examplelib/examplelib.hpp");
   EXPECT_EQ(d.cppBinding.ns, "examplelib");
   ASSERT_EQ(d.dependencies.size(), 1u);
   EXPECT_EQ(d.dependencies[0].id, "corelib");
}

TEST(LibraryLoader, FullSampleConstants)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.constants.size(), 2u);

   const Constant& c0 = d.constants[0];
   EXPECT_EQ(c0.name, "MAX_CHANNELS");
   ASSERT_EQ(c0.type.kind, TypeRefKind::Primitive);
   EXPECT_EQ(c0.type.primitive, BaseType::INT);
   EXPECT_EQ(c0.value.kind, InitKind::Scalar);
   EXPECT_EQ(c0.value.scalar, "8");
   EXPECT_EQ(c0.cppBinding.symbol, "kMaxChannels");
   EXPECT_FALSE(c0.documentation.empty());

   const Constant& c1 = d.constants[1];
   EXPECT_EQ(c1.name, "DEFAULT_TIMEOUT");
   EXPECT_EQ(c1.type.primitive, BaseType::TIME);
   EXPECT_EQ(c1.value.scalar, "T#500ms");
}

TEST(LibraryLoader, FullSampleEnums)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.enums.size(), 1u);
   const EnumTypeDef& e = d.enums[0];
   EXPECT_EQ(e.name, "State");
   EXPECT_EQ(e.cppBinding.symbol, "State");
   EXPECT_EQ(e.baseType.primitive, BaseType::INT);
   ASSERT_EQ(e.members.size(), 3u);
   EXPECT_EQ(e.members[0].name, "IDLE");
   EXPECT_EQ(e.members[0].value, 0);
   EXPECT_EQ(e.members[2].name, "FAULT");
   EXPECT_EQ(e.members[2].value, 2);
   EXPECT_EQ(e.initValue.kind, InitKind::Scalar);
   EXPECT_EQ(e.initValue.scalar, "IDLE");
}

TEST(LibraryLoader, FullSampleStructTypes)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.types.size(), 2u);

   const StructTypeDef& channel = d.types[0];
   EXPECT_EQ(channel.name, "Channel");
   ASSERT_EQ(channel.fields.size(), 4u);
   EXPECT_EQ(channel.fields[0].name, "Value");
   EXPECT_EQ(channel.fields[0].type.kind, TypeRefKind::Primitive);
   EXPECT_EQ(channel.fields[0].type.primitive, BaseType::REAL);
   EXPECT_EQ(channel.fields[1].type.primitive, BaseType::BOOL);
   EXPECT_EQ(channel.fields[2].type.kind, TypeRefKind::Named);
   EXPECT_EQ(channel.fields[2].type.name, "State");
   EXPECT_EQ(channel.fields[2].initValue.scalar, "IDLE");
   EXPECT_EQ(channel.fields[3].type.kind, TypeRefKind::Named);
   EXPECT_EQ(channel.fields[3].type.library, "corelib");
   EXPECT_EQ(channel.fields[3].type.name, "Range");

   const StructTypeDef& bank = d.types[1];
   EXPECT_EQ(bank.name, "ChannelBank");
   ASSERT_EQ(bank.fields.size(), 1u);
   const st2cpp::library::TypeRef& arr = bank.fields[0].type;
   EXPECT_EQ(arr.kind, TypeRefKind::Array);
   EXPECT_EQ(arr.lowerBound, 0);
   EXPECT_EQ(arr.upperBound, 7);
   ASSERT_TRUE(arr.elementType);
   EXPECT_EQ(arr.elementType->kind, TypeRefKind::Named);
   EXPECT_EQ(arr.elementType->name, "Channel");
   EXPECT_EQ(bank.fields[0].initValue.kind, InitKind::Repeat);
   EXPECT_EQ(bank.fields[0].initValue.count, 8);
}

TEST(LibraryLoader, FullSampleGlobalVariables)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.globalVariables.size(), 4u);

   EXPECT_EQ(d.globalVariables[0].name, "gSystemState");
   EXPECT_EQ(d.globalVariables[0].scope, "global");
   EXPECT_FALSE(d.globalVariables[0].constant);
   EXPECT_EQ(d.globalVariables[0].cppBinding.symbol, "g_system_state");
   EXPECT_EQ(d.globalVariables[0].initValue.scalar, "IDLE");

   EXPECT_TRUE(d.globalVariables[2].constant);
   EXPECT_EQ(d.globalVariables[2].initValue.kind, InitKind::Sparse);
   EXPECT_EQ(d.globalVariables[2].initValue.entries.size(), 2u);
   EXPECT_EQ(d.globalVariables[2].initValue.entries[0].lower, 0);
   EXPECT_EQ(d.globalVariables[2].initValue.entries[0].upper, 0);
   EXPECT_EQ(d.globalVariables[2].initValue.entries[0].value->scalar, "100");
   EXPECT_EQ(d.globalVariables[2].initValue.entries[1].value->scalar, "0");
   EXPECT_EQ(d.globalVariables[2].initValue.defaultValue->scalar, "0");

   EXPECT_EQ(d.globalVariables[3].initValue.kind, InitKind::List);
   ASSERT_EQ(d.globalVariables[3].initValue.list.size(), 4u);
   EXPECT_EQ(d.globalVariables[3].initValue.list[3].scalar, "4");
}

TEST(LibraryLoader, FullSampleFunctions)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.functions.size(), 2u);

   const FunctionDef& clamp = d.functions[0];
   EXPECT_EQ(clamp.name, "Clamp");
   EXPECT_EQ(clamp.returnType.kind, TypeRefKind::Primitive);
   EXPECT_EQ(clamp.returnType.primitive, BaseType::REAL);
   EXPECT_EQ(clamp.cppBinding.kind, FunctionBindingKind::FreeFunction);
   EXPECT_EQ(clamp.cppBinding.symbol, "clamp");
   ASSERT_EQ(clamp.parameters.size(), 3u);
   EXPECT_EQ(clamp.parameters[0].name, "Value");
   EXPECT_EQ(clamp.parameters[0].direction, ParamDirection::In);
   EXPECT_EQ(clamp.parameters[2].initValue.scalar, "100.0");

   const FunctionDef& norm = d.functions[1];
   EXPECT_EQ(norm.cppBinding.kind, FunctionBindingKind::StaticMethod);
   EXPECT_EQ(norm.cppBinding.owner, "examplelib::MathUtils");
}

TEST(LibraryLoader, FullSampleFunctionBlocks)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;
   ASSERT_EQ(d.functionBlocks.size(), 2u);

   const FunctionBlockDef& ton = d.functionBlocks[0];
   EXPECT_EQ(ton.name, "TON");
   EXPECT_EQ(ton.cppBinding.instanceType, "examplelib::TonInstance");
   EXPECT_EQ(ton.cppBinding.call, "process");
   ASSERT_EQ(ton.parameters.size(), 4u);
   EXPECT_EQ(ton.parameters[2].name, "Q");
   EXPECT_EQ(ton.parameters[2].direction, ParamDirection::Out);

   const FunctionBlockDef& reader = d.functionBlocks[1];
   EXPECT_EQ(reader.parameters[0].direction, ParamDirection::InOut);
   EXPECT_EQ(reader.parameters[1].name, "Index");
   EXPECT_EQ(reader.parameters[2].type.kind, TypeRefKind::Named);
   EXPECT_EQ(reader.parameters[2].type.name, "Channel");
}

// ============================================================================
// Query API
// ============================================================================

TEST(LibraryLoader, LookupHelpersCaseInsensitive)
{
   LibraryLoadResult r = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(r.ok()) << r.errors[0].message;
   const LibraryDescriptor& d = *r.descriptor;

   EXPECT_NE(d.findEnum("state"), nullptr);
   EXPECT_NE(d.findEnum("STATE"), nullptr);
   EXPECT_EQ(d.findEnum("ghost"), nullptr);
   EXPECT_NE(d.findStruct("channel"), nullptr);
   EXPECT_EQ(d.findStruct("ghost"), nullptr);
   EXPECT_NE(d.findConstant("default_timeout"), nullptr);
   EXPECT_NE(d.findGlobalVariable("gchannelbank"), nullptr);
   EXPECT_NE(d.findFunction("normalize"), nullptr);
   EXPECT_NE(d.findFunctionBlock("ton"), nullptr);
   EXPECT_NE(d.findDependency("CoreLib"), nullptr);
   EXPECT_EQ(d.findDependency("ghostlib"), nullptr);
   EXPECT_TRUE(d.hasType("STATE"));
   EXPECT_TRUE(d.hasType("Channel"));
   EXPECT_FALSE(d.hasType("Ghost"));
}

// ============================================================================
// Round trip
// ============================================================================

TEST(LibraryLoader, SerializeThenReloadIsStable)
{
   LibraryLoadResult first = LibraryLoader::fromFile(samplePath("examplelib.json"));
   ASSERT_TRUE(first.ok()) << first.errors[0].message;

   const std::string json = LibrarySerializer::toJson(*first.descriptor, 2);

   LibraryLoadResult second = LibraryLoader::fromString(json);
   ASSERT_TRUE(second.ok()) << second.errors[0].message;
   ASSERT_TRUE(second.descriptor);

   EXPECT_EQ(second.descriptor->id, first.descriptor->id);
   EXPECT_EQ(second.descriptor->name, first.descriptor->name);
   EXPECT_EQ(second.descriptor->version, first.descriptor->version);
   EXPECT_EQ(second.descriptor->constants.size(), first.descriptor->constants.size());
   EXPECT_EQ(second.descriptor->enums.size(), first.descriptor->enums.size());
   EXPECT_EQ(second.descriptor->types.size(), first.descriptor->types.size());
   EXPECT_EQ(second.descriptor->globalVariables.size(), first.descriptor->globalVariables.size());
   EXPECT_EQ(second.descriptor->functions.size(), first.descriptor->functions.size());
   EXPECT_EQ(second.descriptor->functionBlocks.size(), first.descriptor->functionBlocks.size());

   const Constant& c0 = second.descriptor->constants[0];
   EXPECT_EQ(c0.name, "MAX_CHANNELS");
   EXPECT_EQ(c0.value.scalar, "8");

   const StructTypeDef& bank = second.descriptor->types[1];
   EXPECT_EQ(bank.fields[0].initValue.kind, InitKind::Repeat);
   EXPECT_EQ(bank.fields[0].initValue.count, 8);

   const GlobalVariable& cal = second.descriptor->globalVariables[2];
   EXPECT_EQ(cal.initValue.kind, InitKind::Sparse);
   EXPECT_EQ(cal.initValue.entries[0].upper, 0);
   EXPECT_EQ(cal.initValue.entries[0].value->scalar, "100");
   EXPECT_EQ(cal.initValue.defaultValue->scalar, "0");
}