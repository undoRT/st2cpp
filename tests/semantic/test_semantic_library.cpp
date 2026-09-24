/**
 * @file test_semantic_library.cpp
 * @brief Semantic integration tests: LibraryRegistry -> SemanticAnalyzer
 *
 * Exercises the end-to-end bridge:
 *   ProjectConfig (JSON) -> ProjectLoader -> LibraryRegistry -> LibrarySymbolImporter
 *   -> SymbolTable (external scope) -> DeclVisitor/BodyVisitor -> SemanticInfo
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include "project/ProjectConfigLoader.h"
#include "project/ProjectLoader.h"
#include "semantic/SemanticAnalyzer.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include <string>

using namespace st2cpp::semantic;
using st2cpp::library::LibraryDescriptor;
using st2cpp::library::LibraryRegistry;

#ifndef PROJECT_SAMPLES_DIR
#define PROJECT_SAMPLES_DIR "tests/project/data"
#endif
#ifndef ST_SAMPLES_DIR
#define ST_SAMPLES_DIR "tests/st_samples"
#endif

namespace {

std::string samplePath(const std::string& name)
{
   return std::string(PROJECT_SAMPLES_DIR) + "/" + name;
}

/**
 * @brief Load the registry described by a project config fixture.
 */
LibraryRegistry loadRegistry(const std::string& configName)
{
   auto cfgResult = st2cpp::project::ProjectConfigLoader::fromFile(samplePath(configName));
   EXPECT_TRUE(cfgResult.ok());
   auto loadResult = st2cpp::project::ProjectLoader::load(cfgResult.config.value());
   EXPECT_TRUE(loadResult.ok()) << "fixture load failed: " << loadResult.errors.size();
   return std::move(loadResult.registry);
}

SymbolId externalOf(const SymbolTable& st, const std::string& name)
{
   return st.lookupExternal(name);
}

const Symbol* symOf(const SymbolTable& st, SymbolId id)
{
   return id == 0 ? nullptr : st.get(id);
}

} // namespace

class SemanticLibraryTest : public ::testing::Test {
protected:
    SemanticInfo analyze(const std::string& st, const LibraryRegistry& registry, bool strict = false)
    {
       auto tu = TestHelper::parseST(st);
       SemanticAnalyzer analyzer;
       return analyzer.analyze(tu, registry,
           strict ? SemanticAnalyzer::Strictness::Strict : SemanticAnalyzer::Strictness::Permissive);
    }
};

// ============================================================================
// TEST 1 — Imported symbols are projected into the external scope
// ============================================================================
TEST_F(SemanticLibraryTest, ExternalScopeContainsImportedSymbols) {
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   auto tu = TestHelper::parseST("PROGRAM Main\nVAR x : INT;\nEND_VAR\nEND_PROGRAM");
   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, registry);
   const SymbolTable& st = *info.symbolTable;

   // Types
   SymbolId channelId = externalOf(st, "Channel");
   ASSERT_NE(channelId, 0u);
   const Symbol* channel = symOf(st, channelId);
   ASSERT_NE(channel, nullptr);
   EXPECT_TRUE(channel->isExternal);
   EXPECT_EQ(channel->kind, SymbolKind::Type);
   ASSERT_GE(channel->members.size(), 3u);
   ASSERT_GT(channel->members.size(), 0u);

   // Channel members include Value (REAL), Status (State) and Limits (corelib.Range).
   bool hasValue = false, hasStatus = false, hasLimits = false;
   for (SymbolId memberId : channel->members) {
      const Symbol* m = st.get(memberId);
      ASSERT_NE(m, nullptr);
      if (m->name == "Value") hasValue = true;
      if (m->name == "Status") hasStatus = true;
      if (m->name == "Limits") hasLimits = true;
   }
   EXPECT_TRUE(hasValue);
   EXPECT_TRUE(hasStatus);
   EXPECT_TRUE(hasLimits);

   // Limits member must resolve to the corelib.Range struct (cross-library).
   const Symbol* limits = nullptr;
   for (SymbolId memberId : channel->members) {
      const Symbol* m = st.get(memberId);
      if (m && m->name == "Limits") limits = m;
   }
   ASSERT_NE(limits, nullptr);
   ASSERT_NE(limits->typeId, 0u);
   const TypeInfo* limitsType = st.getType(limits->typeId);
   ASSERT_NE(limitsType, nullptr);
   EXPECT_EQ(limitsType->kind, TypeKind::Struct);
   const Symbol* rangeSym = st.get(limitsType->symbolId);
   ASSERT_NE(rangeSym, nullptr);
   EXPECT_EQ(rangeSym->name, "Range");

   // Enum with enumerators
   SymbolId stateId = externalOf(st, "State");
   ASSERT_NE(stateId, 0u);
   const Symbol* state = symOf(st, stateId);
   ASSERT_NE(state, nullptr);
   ASSERT_GE(state->enumerators.size(), 3u);
   for (SymbolId e : state->enumerators) {
      const Symbol* en = st.get(e);
      ASSERT_NE(en, nullptr);
      EXPECT_EQ(en->kind, SymbolKind::Enumerator);
   }

   // Function with parameters
   const Symbol* clamp = symOf(st, externalOf(st, "Clamp"));
   ASSERT_NE(clamp, nullptr);
   EXPECT_EQ(clamp->kind, SymbolKind::Function);
   EXPECT_EQ(clamp->params.size(), 3u);

   // FB with parameter interface
   const Symbol* ton = symOf(st, externalOf(st, "TON"));
   ASSERT_NE(ton, nullptr);
   EXPECT_EQ(ton->kind, SymbolKind::FunctionBlock);
   EXPECT_EQ(ton->params.size(), 4u);

   // Constant and global variable
   EXPECT_NE(externalOf(st, "MAX_CHANNELS"), 0u);
   const Symbol* glob = symOf(st, externalOf(st, "gSystemState"));
   ASSERT_NE(glob, nullptr);
   EXPECT_EQ(glob->kind, SymbolKind::Variable);
   EXPECT_FALSE(glob->isConstant);
}

// ============================================================================
// TEST 2 — Semantic analysis resolves the fixture ST against the libraries
// ============================================================================
TEST_F(SemanticLibraryTest, FixtureResolvesAndTypeChecks) {
   LibraryRegistry registry = loadRegistry("two_lib_config.json");
   std::string st = TestHelper::readFile(std::string(ST_SAMPLES_DIR) + "/library_usage.st");
   ASSERT_FALSE(st.empty());

   auto info = analyze(st, registry);
   EXPECT_FALSE(info.diagnostics.hasErrors())
       << "errors: " << info.diagnostics.errorCount();
}

// ============================================================================
// TEST 3 — Without a registry, external type names are unknown
// ============================================================================
TEST_F(SemanticLibraryTest, ExternalNamesUnknownWithoutRegistry) {
   LibraryRegistry empty;
   auto info = analyze(R"(
        PROGRAM Main
            VAR
                ch : Channel;
                st : State;
            END_VAR
        END_PROGRAM
    )", empty);

   EXPECT_TRUE(info.diagnostics.hasErrors());
   // Each unknown type is reported once at the declaration site.
   size_t invalidTypeCount = 0;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::InvalidTypeName) ++invalidTypeCount;
   }
   EXPECT_EQ(invalidTypeCount, 2u);
}

// ============================================================================
// TEST 4 — Wrong argument count on an external function is an error
// ============================================================================
TEST_F(SemanticLibraryTest, ExternalFunctionArgCheck) {
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   // Clamp takes 3 inputs; passing 5 arguments must be rejected.
   auto bad = analyze(R"(
        PROGRAM Main
            VAR
                x : REAL;
            END_VAR
            x := Clamp(1.0, 2.0, 3.0, 4.0, 5.0);
        END_PROGRAM
    )", registry, true);
   EXPECT_TRUE(bad.diagnostics.hasErrors());
   bool foundWrongCount = false;
   for (const auto& d : bad.diagnostics.all()) {
      if (d.code == DiagnosticCode::WrongArgumentCount) foundWrongCount = true;
   }
   EXPECT_TRUE(foundWrongCount);

   // Correct usage with defaulted trailing inputs passes.
   auto ok = analyze(R"(
        PROGRAM Main
            VAR
                x : REAL;
            END_VAR
            x := Clamp(1.0);
        END_PROGRAM
    )", registry, true);
   EXPECT_FALSE(ok.diagnostics.hasErrors()) << "errors: " << ok.diagnostics.errorCount();
}

// ============================================================================
// TEST 5 — Project-local declarations shadow the external symbols
// ============================================================================
TEST_F(SemanticLibraryTest, ProjectLocalShadowsExternal) {
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   // The project declares its own Clamp (different signature): it wins and no
   // collision warning is emitted.
   auto info = analyze(R"(
        FUNCTION Clamp : INT
            VAR_INPUT
                V : INT;
            END_VAR
            Clamp := V;
        END_FUNCTION

        PROGRAM Main
            VAR
                n : INT;
            END_VAR
            n := Clamp(7);
        END_PROGRAM
    )", registry, true);

   EXPECT_FALSE(info.diagnostics.hasErrors()) << "errors: " << info.diagnostics.errorCount();

   const SymbolTable& st = *info.symbolTable;
   // The global scope holds the project's own Clamp (not external).
   SymbolId localClamp = st.lookupGlobal("Clamp");
   ASSERT_NE(localClamp, 0u);
   const Symbol* localSym = st.get(localClamp);
   ASSERT_NE(localSym, nullptr);
   EXPECT_FALSE(localSym->isExternal);
   EXPECT_EQ(localSym->kind, SymbolKind::Function);

   // No NOTE/ExternalSymbolCollision diagnostics.
   for (const auto& d : info.diagnostics.all()) {
      EXPECT_NE(d.code, DiagnosticCode::ExternalSymbolCollision);
   }
}

// ============================================================================
// TEST 6 — Two libraries exporting the same name: first wins + warning
// ============================================================================
TEST_F(SemanticLibraryTest, CrossLibraryCollisionWarnsAndFirstWins) {
   // Order by uppercase id: corelib < duallib < examplelib. duallib.Channel and
   // duallib.Clamp therefore shadow the examplelib ones and a warning is emitted.
   LibraryRegistry registry = loadRegistry("collision_config.json");
   ASSERT_EQ(registry.size(), 3u);

   auto tu = TestHelper::parseST(R"(
        PROGRAM Main
            VAR
                ch : Channel;
                x : REAL;
                d : BOOL;
            END_VAR
            // duallib.Channel has a single INT member 'Sample'.
            ch.Sample := 42;
            x := Clamp(1.0, 2.0);
            d := D_ENABLED;
        END_PROGRAM
    )");
   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, registry, SemanticAnalyzer::Strictness::Strict);

   // Warnings were emitted for the shadowed examplelib symbols.
   size_t collisionWarnings = 0;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::ExternalSymbolCollision) ++collisionWarnings;
   }
   EXPECT_GE(collisionWarnings, 2u);

   // The code above targets the duallib signatures, so a strict pass succeeds.
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "errors: " << info.diagnostics.errorCount();

   const SymbolTable& st = *info.symbolTable;
   const Symbol* channel = st.get(st.lookupExternal("Channel"));
   ASSERT_NE(channel, nullptr);
   // duallib.Channel has exactly one member (Sample); examplelib's would have four.
   EXPECT_EQ(channel->members.size(), 1u);
   const Symbol* clamp = st.get(st.lookupExternal("Clamp"));
   ASSERT_NE(clamp, nullptr);
   EXPECT_EQ(clamp->params.size(), 2u); // duallib signature, not examplelib's 3
}

// ============================================================================
// TEST 7 — Verifica dell'analisi con librerie che non collidono (1 sola lib)
// ============================================================================
TEST_F(SemanticLibraryTest, SingleLibraryRoundTrip) {
   LibraryRegistry registry = loadRegistry("single_lib_config.json");
   EXPECT_EQ(registry.size(), 1u);

   auto info = analyze(R"(
        PROGRAM Main
            VAR
                r : Range;
            END_VAR
            r.Min := 1.0;
            r.Max := 2.0;
        END_PROGRAM
    )", registry, true);
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "errors: " << info.diagnostics.errorCount();
}