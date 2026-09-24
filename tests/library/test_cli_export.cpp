/**
 * @file test_cli_export.cpp
 * @brief End-to-end tests for the --export-descriptor CLI command.
 *
 * These tests invoke the actual st2cpp executable (path injected through the
 * ST2CPP_CLI_PATH compile definition) on temporary ST inputs and verify the
 * produced JSON Library Descriptor and the process exit codes.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>

#if defined(_WIN32)

TEST(CliExportTest, UnavailableOnWindows)
{
    GTEST_SKIP() << "--export-descriptor CLI integration test is POSIX-only (popen/mkdtemp)";
}

#else

#include "json/JsonValue.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/wait.h>

#ifndef ST2CPP_CLI_PATH
#define ST2CPP_CLI_PATH "st2cpp"
#endif

namespace fs = std::filesystem;

namespace {

const char* kSampleSt = R"ST(
TYPE Color : (Red, Green := 5, Blue); END_TYPE

FUNCTION Clamp : INT
VAR_INPUT value, lo, hi : INT; END_VAR
VAR_OUTPUT result : INT; END_VAR
END_FUNCTION
)ST";

/**
 * @brief Run the st2cpp CLI, capturing combined output and the exit code.
 */
std::string runCli(const std::vector<std::string>& args, int& exitCode)
{
   std::string cmd = "\"" + std::string(ST2CPP_CLI_PATH) + "\"";
   for (const auto& a : args) {
      cmd += " \"" + a + "\"";
   }
   cmd += " 2>&1";

   FILE* pipe = popen(cmd.c_str(), "r");
   if (pipe == nullptr) {
      exitCode = -1;
      return "popen failed";
   }
   std::string out;
   char buf[4096];
   size_t n;
   while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
      out.append(buf, n);
   }
   int rc = pclose(pipe);
   if (rc == -1) {
      exitCode = -1;
   } else if (WIFEXITED(rc)) {
      exitCode = WEXITSTATUS(rc);
   } else {
      exitCode = -1;
   }
   return out;
}

/**
 * @brief Create a unique temporary directory for a test run.
 */
fs::path makeTempDir()
{
   std::string tmpl = "/tmp/st2cpp_cli_export_XXXXXX";
   std::vector<char> buf(tmpl.begin(), tmpl.end());
   buf.push_back('\0');
   char* dir = mkdtemp(buf.data());
   EXPECT_NE(dir, nullptr);
   return fs::path(dir);
}

void writeFile(const fs::path& path, const std::string& content)
{
   std::ofstream f(path);
   ASSERT_TRUE(f.good());
   f << content;
}

} // namespace

class CliExportTest : public ::testing::Test {
protected:
   void SetUp() override
   {
      if (!fs::exists(ST2CPP_CLI_PATH)) {
         GTEST_SKIP() << "st2cpp CLI binary not found at " << ST2CPP_CLI_PATH;
      }
      dir = makeTempDir();
   }

   void TearDown() override
   {
      std::error_code ec;
      fs::remove_all(dir, ec);
   }

   fs::path dir;
};

TEST_F(CliExportTest, WritesValidDescriptor)
{
   fs::path st = dir / "mylib.st";
   writeFile(st, kSampleSt);

   fs::path out = dir / "mylib.json";
   int exitCode = -1;
   std::string output = runCli({
       st.string(),
       "--export-descriptor", out.string(),
       "--lib-id", "clilib",
       "--lib-name", "CliLib",
       "--lib-version", "1.2.3",
       "--lib-description", "Exported by the CLI",
   }, exitCode);

   ASSERT_EQ(exitCode, 0) << output;
   ASSERT_TRUE(fs::exists(out));

   std::ifstream f(out);
   std::string json;
   f.seekg(0, std::ios::end);
   json.resize(static_cast<size_t>(f.tellg()));
   f.seekg(0, std::ios::beg);
   f.read(json.data(), static_cast<std::streamsize>(json.size()));

   st2cpp::json::JsonValue doc = st2cpp::json::parse(json);
   ASSERT_EQ(doc.find("id")->asString(), "clilib");
   ASSERT_EQ(doc.find("name")->asString(), "CliLib");
   ASSERT_EQ(doc.find("version")->asString(), "1.2.3");
   ASSERT_EQ(doc.find("description")->asString(), "Exported by the CLI");
   ASSERT_TRUE(doc.find("enums")->isArray());
   ASSERT_EQ(doc.find("enums")->array.size(), 1u);
}

TEST_F(CliExportTest, WritesOnlyOnValidIdentity)
{
   fs::path st = dir / "mylib.st";
   writeFile(st, kSampleSt);

   fs::path out = dir / "mylib.json";
   int exitCode = -1;
   std::string output = runCli({
       st.string(),
       "--export-descriptor", out.string(),
       "--lib-name", "CliLib",
       "--lib-version", "1.2.3",
   }, exitCode);

   EXPECT_NE(exitCode, 0) << output;
   EXPECT_FALSE(fs::exists(out));
}

TEST_F(CliExportTest, RejectsUnrepresentableConstruct)
{
   fs::path st = dir / "bad.st";
   writeFile(st, R"ST(
PROGRAM Main
VAR x : INT; END_VAR
END_PROGRAM
)ST");

   fs::path out = dir / "bad.json";
   int exitCode = -1;
   std::string output = runCli({
       st.string(),
       "--export-descriptor", out.string(),
       "--lib-id", "badlib",
       "--lib-name", "BadLib",
       "--lib-version", "1.0.0",
   }, exitCode);

   EXPECT_EQ(exitCode, 1) << output;
   EXPECT_NE(output.find("Export error"), std::string::npos) << output;
   EXPECT_NE(output.find("PROGRAM POUs are not representable"), std::string::npos) << output;
}

TEST_F(CliExportTest, WorkspaceExportMergesEntities)
{
   fs::path ws = dir / "ws";
   fs::create_directories(ws);
   writeFile(ws / "part1.st", kSampleSt);
   writeFile(ws / "part2.st", R"ST(
TYPE Vec3 : STRUCT
   x : REAL;
   y : REAL;
   z : REAL;
END_STRUCT END_TYPE
FUNCTION_BLOCK Ramp
VAR_INPUT target : BOOL; END_VAR
END_FUNCTION_BLOCK
)ST");

   fs::path out = dir / "ws.json";
   int exitCode = -1;
   std::string output = runCli({
       "--workspace", ws.string(),
       "--export-descriptor", out.string(),
       "--lib-id", "ws_lib",
       "--lib-name", "WsLib",
       "--lib-version", "2.0.0",
   }, exitCode);

   ASSERT_EQ(exitCode, 0) << output;
   ASSERT_TRUE(fs::exists(out));

   std::ifstream f(out);
   std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   st2cpp::json::JsonValue doc = st2cpp::json::parse(json);

   const st2cpp::json::JsonValue* enums = doc.find("enums");
   ASSERT_TRUE(enums != nullptr && enums->isArray());
   ASSERT_EQ(enums->array.size(), 1u); // Color
   const st2cpp::json::JsonValue* types = doc.find("types");
   ASSERT_TRUE(types != nullptr && types->isArray());
   ASSERT_EQ(types->array.size(), 1u); // Vec3
   const st2cpp::json::JsonValue* fbs = doc.find("functionBlocks");
   ASSERT_TRUE(fbs != nullptr && fbs->isArray());
   ASSERT_EQ(fbs->array.size(), 1u); // Ramp
}

#endif // !defined(_WIN32)