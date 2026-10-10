/**
 * @file test_codegen_diag.cpp
 * @brief Diagnostic reflection table: generation, structure and runtime usability
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../helpers/TestHelper.h"
#include "ast/AST.h"
#include "codegen/CodeGenerator.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/SemanticAnalyzer.h"
#include "task/TaskConfig.h"
#include "st2cpp_includes/undoPLC/include/undoDiag.hpp"

namespace fs = std::filesystem;

class CodegenDiagTest : public ::testing::Test
{
protected:
   void SetUp() override
   {
      m_tempDir = fs::temp_directory_path() / "st2cpp_diag_test";
      fs::remove_all(m_tempDir);
      fs::create_directories(m_tempDir);
      writeRuntimeStub();
   }

   void TearDown() override { fs::remove_all(m_tempDir); }

   void writeRuntimeStub()
   {
      const std::string runtimeStub = R"(
            #pragma once
            #include <cstdint>
            #include <array>
            #include <initializer_list>
            #include <stdexcept>
            #include <type_traits>
            using Bool = bool;
            using Int8 = int8_t;
            using Int16 = int16_t;
            using Int32 = int32_t;
            using Int64 = int64_t;
            using UInt8 = uint8_t;
            using UInt16 = uint16_t;
            using UInt32 = uint32_t;
            using UInt64 = uint64_t;
            using Float = float;
            using Double = double;
            using String = const char*;
            using Byte = UInt8;
            using Word = UInt16;
            using Dword = UInt32;
            using Lword = UInt64;
            template<typename T, int Low, int High>
            struct STArray {
                static constexpr int low_bound() { return Low; }
                static constexpr int high_bound() { return High; }
                static constexpr size_t size_total() { return static_cast<size_t>(High - Low + 1); }
                std::array<T, size_total()> data;
                STArray() { data.fill(T{}); }
                STArray(std::initializer_list<T> init) {
                    data.fill(T{});
                    size_t i = 0;
                    for (const auto& v : init) {
                        if (i < size_total()) { data[i++] = v; }
                    }
                }
                T& operator[](int idx) {
                    if (idx < Low || idx > High) throw std::out_of_range("STArray out of bounds");
                    return data[static_cast<size_t>(idx - Low)];
                }
                const T& operator[](int idx) const {
                    if (idx < Low || idx > High) throw std::out_of_range("STArray out of bounds");
                    return data[static_cast<size_t>(idx - Low)];
                }
            };
            template<typename T>
            struct VAR_INOUT {
                T* ptr;
                VAR_INOUT() : ptr(nullptr) {}
                VAR_INOUT(T& ref) : ptr(&ref) {}
                VAR_INOUT& operator=(T& ref) { ptr = &ref; return *this; }
                operator T&() { return *ptr; }
                operator const T&() const { return *ptr; }
            };
        )";
      const fs::path rtDir = m_tempDir / "undoCore";
      fs::create_directories(rtDir);
      TestHelper::writeFile((rtDir / "types.hpp").string(), runtimeStub);
   }

   /// Include paths that resolve the *real* undoDiag.hpp (and the Boost it needs)
   /// for generated-code compilation. The generated DiagVars.cpp is meant to be
   /// built against the runtime it reflects, so the round-trip test must not
   /// settle for a stub that can silently drift from the real Node layout.
   std::vector<std::string> realDiagIncludePaths() const
   {
      const std::string root = ST2CPP_REPO_ROOT;
      return {root + "/st2cpp_includes/undoPLC/include", root + "/st2cpp_includes/undoPLC/third_party/boost_1_91_0"};
   }

   fs::path m_tempDir;
};

TEST_F(CodegenDiagTest, ReflectsNestedAggregatesAndArrays)
{
   const std::string st = R"(
       TYPE tPoint :
          STRUCT
             x : INT;
             y : INT;
          END_STRUCT
       END_TYPE

       TYPE tLine :
          STRUCT
             p1 : tPoint;
             p2 : tPoint;
          END_STRUCT
       END_TYPE

       FUNCTION_BLOCK FB_Ctrl
          VAR
             m : INT;
          END_VAR
       END_FUNCTION_BLOCK

       PROGRAM Main
          VAR
             pos : tLine;
             values : ARRAY[1..3] OF INT;
             grid : ARRAY[1..2,1..2] OF tPoint;
             ctrl : FB_Ctrl;
          END_VAR
       END_PROGRAM

       VAR_GLOBAL
          G0 : INT;
          GA : ARRAY[0..1] OF tPoint;
       END_VAR
   )";

   Lexer lexer(st, "<diag>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   ASSERT_FALSE(info.diagnostics.hasErrors());

   CodeGenerator gen;
   gen.setNamespace("undoCore");
   gen.setRuntimeHeader("undoCore/types.hpp");
   gen.setCaseSensitive(false);
   ProcessImageConfig pi;
   pi.inputBytes = 256;
   pi.outputBytes = 256;
   pi.markerBytes = 256;
   pi.autoDetect = true;
   gen.setProcessImageConfig(pi);
   gen.setSemanticInfo(&info);

   auto files = gen.generateModularProject(tu, m_tempDir.string(), nullptr, true);
   auto diag = gen.generateDiag();

   // JSON manifest must be well-formed and self-consistent
   std::string manifest = diag.manifest;
   ASSERT_FALSE(manifest.empty());
   const std::string marker = "\"format\"";
   EXPECT_TRUE(manifest.find(marker) != std::string::npos);
   EXPECT_TRUE(manifest.find("st2cpp.diag") != std::string::npos);
   EXPECT_TRUE(manifest.find("\"nodeCount\"") != std::string::npos);

   // Expect at least: MAIN.Root + members + arrays + struct children + globals
   EXPECT_GT(diag.nodeCount, 6u);

   // Paths are the canonical names the generated C++ uses: the default
   // case-insensitive mode folds identifiers to upper case, so the reflection
   // path is exactly the struct member the offset is measured against.
   // MAIN.POS.P1.X, MAIN.VALUES[], MAIN.GRID[], GLOBAL GA[].X
   const std::string src = diag.source;
   EXPECT_TRUE(src.find("\"MAIN.POS.P1.X\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"MAIN.VALUES[]\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"MAIN.GRID[]\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"MAIN.CTRL.M\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"G0\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"GA[]\"") != std::string::npos);
   EXPECT_TRUE(src.find("\"GA[].X\"") != std::string::npos);

   // Table must live in the generated namespace
   EXPECT_TRUE(src.find("kDiagNodes") != std::string::npos);

   // Compile DiagVars.cpp alongside a trivial main against the stub undoDiag.hpp.
   // The generated files land at the fixture root so the quoted includes the
   // generator emits ("SimpleGVLs.hpp", "undoDiag.hpp") resolve from one -I.
   const fs::path outDir = m_tempDir;
   fs::create_directories(outDir);
   for (const auto& f : files) {
      fs::path d = outDir;
      if (!f.subdir.empty()) {
         d /= f.subdir;
      }
      fs::create_directories(d);
      const char* ext = f.type == GenFileType::SOURCE ? ".cpp" : (f.type == GenFileType::JSON ? ".json" : ".hpp");
      TestHelper::writeFile((d / (f.name + ext)).string(), f.content);
   }

   const std::string testMain = R"(
       #include "DiagVars.hpp"
       #include "Programs.hpp"
       #include <cstdint>
       #include <cstring>
       using namespace ST2CPP_RUNTIME_NAMESPACE;

       // Children contract: for every node, (firstChild, childCount) must
       // enumerate exactly the nodes whose parent is that node, and contiguously.
       // A struct member emitted out of order, or an array element left with a
       // stale parent, would make a tree walker miss a member or read a sibling.
       static bool childrenContract(const undoDiag::Node* nodes, uint32_t count) {
           for (uint32_t i = 0; i < count; ++i) {
               uint32_t seen = 0;
               for (uint32_t j = 0; j < count; ++j) {
                   if (nodes[j].parent == i) {
                       if (j < nodes[i].firstChild || j >= uint32_t(nodes[i].firstChild + nodes[i].childCount)) return false;
                       ++seen;
                   }
               }
               if (seen != nodes[i].childCount) return false;
           }
           return true;
       }

       int main() {
           undoDiag::CommandQueue cq;
           undoDiag::SampleQueue sq;
           undoDiag::Hub hub(ST2CPP_RUNTIME_NAMESPACE::kDiagNodes, ST2CPP_RUNTIME_NAMESPACE::kDiagNodeCount, cq, sq);
           MAIN m;
           if (!hub.bindRoot(0, &m)) return 1;
           if (!childrenContract(ST2CPP_RUNTIME_NAMESPACE::kDiagNodes, ST2CPP_RUNTIME_NAMESPACE::kDiagNodeCount)) return 8;

           const uint32_t id = hub.findByPath("MAIN.POS.P1.X");
           if (id == undoDiag::NO_NODE) return 2;
           // Write through the public command interface: the table is only
           // useful if the real Hub can resolve it and land on the live member.
           undoDiag::Command w{};
           w.op = static_cast<uint8_t>(undoDiag::Op::Write);
           w.node = id;
           w.len = sizeof(Int16);
           Int16 five = 5;
           std::memcpy(w.payload, &five, sizeof(Int16));
           if (!cq.push(w)) return 3;
           hub.process(1, 0);
           if (m.POS.P1.X != 5) return 4;

           // A struct nested inside an array must carry real member offsets: with
           // a moved-from container name the offset would collapse to 0 and this
           // write would land on GRID[1][1].X instead of .Y.
           const uint32_t yid = hub.findByPath("MAIN.GRID[].Y");
           if (yid == undoDiag::NO_NODE) return 5;
           undoDiag::Command w2{};
           w2.op = static_cast<uint8_t>(undoDiag::Op::Write);
           w2.node = yid;
           w2.nIdx = 2;
           w2.idx[0] = 1;
           w2.idx[1] = 1;
           w2.len = sizeof(Int16);
           Int16 fortytwo = 42;
           std::memcpy(w2.payload, &fortytwo, sizeof(Int16));
           if (!cq.push(w2)) return 6;
           hub.process(2, 0);
           if (m.GRID[1][1].Y != 42) return 7;
           return 0;
       }
   )";
   TestHelper::writeFile((outDir / "test_diag_rt.cpp").string(), testMain);

   std::vector<std::string> sources;
   sources.push_back((outDir / "DiagVars.cpp").string());
   sources.push_back((outDir / "test_diag_rt.cpp").string());
   // Compile against the real undoDiag.hpp and its Boost, not a stub: the Node
   // layout this table describes is the layout the runtime walks, and only the
   // real header can catch a drift between them.
   const int rc = TestHelper::compileSources(sources, m_tempDir.string(), false, realDiagIncludePaths());
   EXPECT_EQ(rc, 0) << "Compiling diag table against the real Hub must succeed";
}

// With a task configuration the table carries one root per program *instance*,
// named "PLC.TASK.PROG", and the generated runtime has to bind exactly those
// paths. This is the contract that keeps a program shared by two tasks (two
// independent objects) distinguishable; if the two emitters ever folded the
// prefix differently the runtime would silently bind nothing.
TEST_F(CodegenDiagTest, EmitsOneRootPerInstanceAndRuntimeBindsThosePaths)
{
   const std::string st = R"(
       PROGRAM Main
       VAR
          x : INT;
       END_VAR
       END_PROGRAM

       PROGRAM Control
       VAR
          y : INT;
       END_VAR
       END_PROGRAM

       VAR_GLOBAL
          G : INT;
       END_VAR
   )";

   Lexer lexer(st, "<diag>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   ASSERT_FALSE(info.diagnostics.hasErrors());

   st2cpp::task::TaskConfig config;
   st2cpp::task::TaskEntry t1;
   t1.name = "T1";
   t1.plc = "Line1";
   t1.cycleMs = 10;
   t1.priority = 40;
   t1.programs = {"Main"};
   st2cpp::task::TaskEntry t2;
   t2.name = "T2";
   t2.plc = "Line1";
   t2.cycleMs = 10;
   t2.priority = 40;
   t2.programs = {"Main", "Control"};
   config.tasks = {t1, t2};

   CodeGenerator gen;
   gen.setNamespace("undoCore");
   gen.setRuntimeHeader("undoCore/types.hpp");
   gen.setCaseSensitive(false);
   ProcessImageConfig pi;
   pi.inputBytes = 256;
   pi.outputBytes = 256;
   pi.markerBytes = 256;
   pi.autoDetect = true;
   gen.setProcessImageConfig(pi);
   gen.setSemanticInfo(&info);

   auto diag = gen.generateDiag(&config);

   // One root per instance, all sharing a PLC; Main appears once per task.
   EXPECT_TRUE(diag.source.find("\"Line1.T1.MAIN\"") != std::string::npos);
   EXPECT_TRUE(diag.source.find("\"Line1.T2.MAIN\"") != std::string::npos);
   EXPECT_TRUE(diag.source.find("\"Line1.T2.CONTROL\"") != std::string::npos);

   // The runtime binds exactly the instance paths and the globals of the table.
   ASSERT_EQ(diag.globalRoots.size(), 1u);
   EXPECT_EQ(diag.globalRoots.front().path, "G");
   EXPECT_EQ(diag.globalRoots.front().cppRef, "undoCore::G");

   auto files = gen.generateModularProject(tu, m_tempDir.string(), &config);
   bool hasInstanceTable = false;
   for (const auto& f : files) {
      if (f.name == "DiagVars" && f.type == GenFileType::SOURCE) {
         hasInstanceTable = f.content.find("\"Line1.T1.MAIN\"") != std::string::npos;
      }
   }
   EXPECT_TRUE(hasInstanceTable) << "the project ships the instance-aware table";

   auto runtimeResult = gen.generateRuntime(tu, config);
   ASSERT_TRUE(runtimeResult.ok()) << (runtimeResult.errors.empty() ? "" : runtimeResult.errors.front());
   const std::string& runtime = runtimeResult.content;
   EXPECT_TRUE(runtime.find("#include \"DiagVars.hpp\"") != std::string::npos);
   EXPECT_TRUE(runtime.find("hub.findByPath((_taskName + \".MAIN\").c_str())") != std::string::npos);
   EXPECT_TRUE(runtime.find("hub.findByPath((_taskName + \".CONTROL\").c_str())") != std::string::npos);
   EXPECT_TRUE(runtime.find("_diagHub.findByPath(\"G\")") != std::string::npos);
   EXPECT_TRUE(runtime.find("&undoCore::G") != std::string::npos);
   EXPECT_TRUE(runtime.find("_diagHub.process(") != std::string::npos);
   // A force must act on the physical process at the two cycle points, not just
   // on the observer view: input hook after the image read, output hook before
   // the bus copy.
   EXPECT_TRUE(runtime.find("void onInputsRead() override") != std::string::npos);
   EXPECT_TRUE(runtime.find("void onBeforeOutputsWrite() override") != std::string::npos);
   EXPECT_TRUE(runtime.find("_diagHub.applyForces()") != std::string::npos);
}
