/**
 * @file test_codegen_external_library.cpp
 * @brief C++ generation of external library C++ bindings
 *
 * Closed loop of the external-library pipeline: Registry (descriptor cppBinding)
 * -> SemanticAnalyzer (Symbol::externalLibraryId) -> CodeGenerator (semantic
 * lookup on the registry). Verifies that:
 *   - freeFunction/staticMethod bind to the descriptor symbol/owner,
 *   - FB instances declare instanceType and step via cppBinding.call,
 *   - globals/constants bind through cppBinding.symbol,
 *   - external struct/enum types qualify with the library namespace,
 *   - library includes are emitted only for the libraries actually used,
 *   - project-local declarations keep the legacy ST spelling (shadow wins),
 *   - the multi-library collision case lands on the winning library.
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
#include "codegen/CodeGenerator.h"
#include <regex>
#include <string>

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

} // namespace

class CodegenExternalLibraryTest : public ::testing::Test
{
protected:
   bool hasMatch(const std::string& source, const std::string& pattern) const
   {
      try {
         return std::regex_search(source, std::regex(pattern));
      } catch (const std::regex_error&) {
         return source.find(pattern) != std::string::npos;
      }
   }

   void expectSource(const std::string& source, const std::string& pattern, const std::string& msg = "")
   {
      EXPECT_TRUE(hasMatch(source, pattern)) << msg << "\nFull source:\n" << source;
   }

   void expectHeader(const std::string& header, const std::string& pattern, const std::string& msg = "")
   {
      EXPECT_TRUE(hasMatch(header, pattern)) << msg << "\nFull header:\n" << header;
   }
};

// ============================================================================
// Test 1 — examplelib usage: struct, enum, functions, FB, constant, global.
// ============================================================================
TEST_F(CodegenExternalLibraryTest, ExampleLibBindings)
{
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   std::string st = TestHelper::readFile(std::string(ST_SAMPLES_DIR) + "/library_usage.st");
   ASSERT_FALSE(st.empty());

   // Extend the fixture with a static-method function and an unbound constant.
   st += "\n";
   st += "PROGRAM Extras\n";
   st += "    VAR\n";
   st += "        x : REAL;\n";
   st += "        t : TIME;\n";
   st += "    END_VAR\n";
   st += "    x := Normalize(2.5);\n";
   st += "    t := DEFAULT_TIMEOUT;\n";
   st += "END_PROGRAM\n";

   auto code = TestHelper::generateFromSTWithLibraries(st, registry);

   // Library includes: only the two used libraries, deterministic order.
   expectHeader(code.header, R"(#include "corelib/corelib\.hpp")");
   expectHeader(code.header, R"(#include "examplelib/examplelib\.hpp")");

   // Declarations bind to instanceType / qualified types.
   expectHeader(code.header, R"(examplelib::TonInstance TON\{\};)");
   expectHeader(code.header, R"(examplelib::Channel CH\{\};)");
   expectHeader(code.header, R"(examplelib::State ST\{\};)");

   // Struct member access keeps the normalized ST spelling.
   expectSource(code.source, "CH\\.VALUE = 8\\.5;");
   expectSource(code.source, "CH\\.LIMITS\\.MIN");
   expectSource(code.source, "CH\\.LIMITS\\.MAX");

   // Enum literals qualify with the C++ enum binding and descriptor member.
   expectSource(code.source, "ST = examplelib::State::FAULT;");
   expectSource(code.source, "examplelib::State::RUNNING");
   expectSource(code.source, "examplelib::State::IDLE");

   // freeFunction binds verbatim (no namespace prefix), staticMethod -> owner::symbol.
   expectSource(code.source, "X = clamp\\(1\\.5,");
   expectSource(code.source, "X = examplelib::MathUtils::normalize\\(2\\.5\\);");

   // FB invocation: setters + descriptor step + getters.
   expectSource(code.source, "TON\\.set_IN\\(true\\);");
   expectSource(code.source, "TON\\.process\\(\\);");
   expectSource(code.source, "DONE = TON\\.get_Q\\(\\);");
   expectSource(code.source, "ELAPSED = TON\\.get_ET\\(\\);");

   // Global variable and constant bind through cppBinding.symbol.
   expectSource(code.source, "N = kMaxChannels;");
   expectSource(code.source, "ST = g_system_state;");

   // The unbound constant keeps its ST spelling.
   expectSource(code.source, "T = DEFAULT_TIMEOUT;");
}

// ============================================================================
// Test 2 — Collision: duallib wins over examplelib, includes only used libs.
// ============================================================================
TEST_F(CodegenExternalLibraryTest, CollisionResolvesToWinningLibrary)
{
   LibraryRegistry registry = loadRegistry("collision_config.json");
   ASSERT_EQ(registry.size(), 3u);

   const std::string st = R"(
      PROGRAM Main
          VAR
              ch : Channel;
              x : REAL;
              d : BOOL;
          END_VAR
          ch.Sample := 42;
          x := Clamp(1.0, 2.0);
          d := D_ENABLED;
      END_PROGRAM
   )";

   auto code = TestHelper::generateFromSTWithLibraries(st, registry);

   // duallib.Channel has NO cppBinding on the type: the library namespace is
   // still applied (duallib::Channel). member access is the normalized spelling.
   expectHeader(code.header, R"(duallib::Channel CH\{\};)");
   expectSource(code.source, "CH\\.SAMPLE = 42;");

   // duallib.Clamp is a freeFunction bound to 'clamp' too (descriptor symbol).
   expectSource(code.source, "X = clamp\\(1\\.0, 2\\.0\\);");

   // Unbound constant D_ENABLED keeps the ST spelling.
   expectSource(code.source, "D = D_ENABLED;");

   // Only the library actually used is included (duallib), despite the config
   // loading corelib and examplelib as well.
   expectHeader(code.header, R"(#include "duallib/duallib\.hpp")");
   EXPECT_EQ(code.header.find("corelib/corelib.hpp"), std::string::npos);
   EXPECT_EQ(code.header.find("examplelib/examplelib.hpp"), std::string::npos);
}

// ============================================================================
// Test 3 — Project-local declarations shadow the library bindings.
// ============================================================================
TEST_F(CodegenExternalLibraryTest, LocalShadowsLibraryBinding)
{
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   const std::string st = R"(
      FUNCTION Clamp : INT
          VAR_INPUT
              V : INT;
          END_VAR
          Clamp := V;
      END_FUNCTION

      FUNCTION_BLOCK Timer
          VAR
              Q : BOOL;
          END_VAR
          Q := TRUE;
      END_FUNCTION_BLOCK

      PROGRAM Main
          VAR
              n : INT;
              timer : Timer;
          END_VAR
          n := Clamp(7);
          timer(IN := TRUE);
      END_PROGRAM
   )";

   auto code = TestHelper::generateFromSTWithLibraries(st, registry);

   // The project Clamp wins: the ST spelling (uppercase) is kept, no binding.
   expectSource(code.source, "N = CLAMP\\(7\\);");
   EXPECT_EQ(code.source.find("clamp("), std::string::npos);

   // The local FB keeps the binary call (setter + plain "timer()"), not .process().
   expectSource(code.source, "TIMER\\.set_IN\\(true\\);");
   expectSource(code.source, "TIMER\\(\\);");

   // No library is used, therefore no library include is emitted at all.
   std::regex includeRe(R"(#include "(?!undoCore/types\.hpp"))");
   EXPECT_FALSE(std::regex_search(code.header, includeRe));
}

// ============================================================================
// Test 4 — Modular project headers carry the library includes.
// ============================================================================
TEST_F(CodegenExternalLibraryTest, ModularHeadersIncludeUsedLibraries)
{
   LibraryRegistry registry = loadRegistry("two_lib_config.json");

   const std::string st = TestHelper::readFile(std::string(ST_SAMPLES_DIR) + "/library_usage.st");
   ASSERT_FALSE(st.empty());

   auto tu = TestHelper::parseST(st);
   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto semanticInfo = analyzer.analyze(tu, registry,
       st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);

   CodeGenerator gen;
   gen.setNamespace("undoCore");
   gen.setRuntimeHeader("undoCore/types.hpp");
   gen.setCaseSensitive(false);
   gen.setSemanticInfo(&semanticInfo);
   auto files = gen.generateModularProject(tu, "generated_extlib");

   ASSERT_FALSE(files.empty());
   bool gvlsSeen = false, progSeen = false;
   for (const auto& f : files) {
      if (f.name == "FunctionBlocks") {
         // Per-FB headers exist too; with a single program using only external
         // FBs the master aggregator is present, the per-FB headers are not.
         continue;
      }
      if (f.type != GenFileType::HEADER) {
         continue;
      }
      if (f.name == "SimpleGVLs" || f.name == "GVLs" || f.name == "Functions" ||
          f.name == "MAIN") {
         if (f.name == "MAIN") progSeen = true;
         if (f.name == "SimpleGVLs" || f.name == "GVLs") gvlsSeen = true;
         EXPECT_TRUE(hasMatch(f.content, R"(#include "examplelib/examplelib\.hpp")"))
             << "missing library include in: " << f.name;
      }
   }
   EXPECT_TRUE(gvlsSeen);
   EXPECT_TRUE(progSeen);
}