/**
 * @file test_project_config_loader.cpp
 * @brief Tests for ProjectConfigLoader: JSON -> ProjectConfig + validation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "project/ProjectConfigLoader.h"
#include "json/JsonValue.h"
#include <string>

using namespace st2cpp::project;

#ifndef PROJECT_SAMPLES_DIR
#define PROJECT_SAMPLES_DIR "tests/project/data"
#endif

namespace {
std::string samplePath(const std::string& name)
{
   return std::string(PROJECT_SAMPLES_DIR) + "/" + name;
}

const ProjectConfig* findConfig(const ProjectConfigLoadResult& r)
{
   return r.config.has_value() ? &r.config.value() : nullptr;
}
}

// ============================================================================
// TEST 1 — Minimal config: empty library list
// ============================================================================
TEST(ProjectConfigLoaderTest, MinimalConfig) {
   auto result = ProjectConfigLoader::fromFile(samplePath("minimal_config.json"));
   ASSERT_TRUE(result.ok()) << "errors: " << result.errors.size();

   const ProjectConfig* cfg = findConfig(result);
   ASSERT_NE(cfg, nullptr);
   EXPECT_EQ(cfg->schemaVersion, "1.0");
   EXPECT_EQ(cfg->name, "Minimal");
   EXPECT_TRUE(cfg->libraries.empty());
}

// ============================================================================
// TEST 2 — Single library: relative path resolved against the config dir
// ============================================================================
TEST(ProjectConfigLoaderTest, SingleLibrary) {
   auto result = ProjectConfigLoader::fromFile(samplePath("single_lib_config.json"));
   ASSERT_TRUE(result.ok());

   const ProjectConfig* cfg = findConfig(result);
   ASSERT_NE(cfg, nullptr);
   ASSERT_EQ(cfg->libraries.size(), 1u);

   const LibraryEntry& entry = cfg->libraries[0];
   EXPECT_EQ(entry.id, "corelib");
   EXPECT_FALSE(entry.hasVersion);
   EXPECT_EQ(entry.path, "../../library/data/corelib.json");
   // The resolved path must point at the real descriptor file (base-dir aware).
   EXPECT_NE(entry.resolvedPath.find("library/data/corelib.json"), std::string::npos);
   EXPECT_FALSE(entry.resolvedPath.empty());
}

// ============================================================================
// TEST 3 — Two libraries with version constraints
// ============================================================================
TEST(ProjectConfigLoaderTest, TwoLibrariesWithVersions) {
   auto result = ProjectConfigLoader::fromFile(samplePath("two_lib_config.json"));
   ASSERT_TRUE(result.ok());

   const ProjectConfig* cfg = findConfig(result);
   ASSERT_NE(cfg, nullptr);
   ASSERT_EQ(cfg->libraries.size(), 2u);

   EXPECT_EQ(cfg->libraries[0].id, "corelib");
   EXPECT_TRUE(cfg->libraries[0].hasVersion);
   EXPECT_TRUE(cfg->libraries[0].version.valid);
   EXPECT_EQ(cfg->libraries[0].version.raw, "^1.0.0");

   EXPECT_EQ(cfg->libraries[1].id, "examplelib");
   EXPECT_TRUE(cfg->libraries[1].version.valid);

   // Duplicate ids are tolerated by the config loader (ProjectLoader rejects them).
   EXPECT_TRUE(cfg->libraries[0].resolvedPath.find("library/data/corelib.json") != std::string::npos);
   EXPECT_TRUE(cfg->libraries[1].resolvedPath.find("library/data/examplelib.json") != std::string::npos);
}

// ============================================================================
// TEST 4 — Missing $schemaVersion
// ============================================================================
TEST(ProjectConfigLoaderTest, MissingSchemaVersion) {
   auto result = ProjectConfigLoader::fromString(R"({
        "name": "N",
        "libraries": []
      })");
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 1u);
   EXPECT_EQ(result.errors[0].path, "$schemaVersion");
}

// ============================================================================
// TEST 5 — Unsupported schema version
// ============================================================================
TEST(ProjectConfigLoaderTest, UnsupportedSchemaVersion) {
   auto result = ProjectConfigLoader::fromString(R"({
        "$schemaVersion": "2.0"
      })");
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("unsupported schema version"), std::string::npos);
}

// ============================================================================
// TEST 6 — Library entry validation: missing members and bad types
// ============================================================================
TEST(ProjectConfigLoaderTest, InvalidLibraryEntry) {
   auto result = ProjectConfigLoader::fromString(R"({
        "$schemaVersion": "1.0",
        "libraries": [
          { "id": "ok-lib" },
          { "id": "", "path": 42 },
          { "version": "^1.0.0" }
        ]
      })");
   ASSERT_FALSE(result.ok());

   bool foundMissingPath = false;
   bool foundBlankId = false;
   bool foundNonStringPath = false;
   for (const auto& e : result.errors) {
      if (e.path == "libraries[0].path") foundMissingPath = true;
      if (e.path == "libraries[1].id") foundBlankId = true;
      if (e.path == "libraries[1].path") foundNonStringPath = true;
   }
   EXPECT_TRUE(foundMissingPath);
   EXPECT_TRUE(foundBlankId);
   EXPECT_TRUE(foundNonStringPath);
   // The third entry has no id and no path declared.
   bool foundThirdMissingId = false;
   for (const auto& e : result.errors) {
      if (e.path == "libraries[2].id") foundThirdMissingId = true;
   }
   EXPECT_TRUE(foundThirdMissingId);
}

// ============================================================================
// TEST 7 — Invalid version constraint in an entry
// ============================================================================
TEST(ProjectConfigLoaderTest, InvalidVersionConstraint) {
   auto result = ProjectConfigLoader::fromFile(samplePath("invalid_constraint_config.json"));
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 1u);
   EXPECT_EQ(result.errors[0].path, "libraries[0].version");
   EXPECT_NE(result.errors[0].message.find("invalid version constraint"), std::string::npos);
}

// ============================================================================
// TEST 8 — Malformed JSON becomes a single parse error
// ============================================================================
TEST(ProjectConfigLoaderTest, MalformedJson) {
   auto result = ProjectConfigLoader::fromString("not json at all");
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 1u);
   EXPECT_TRUE(result.errors[0].path.empty());
}

// ============================================================================
// TEST 9 — Non-object root
// ============================================================================
TEST(ProjectConfigLoaderTest, NonObjectRoot) {
   auto result = ProjectConfigLoader::fromString("[1, 2, 3]");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors[0].message.find("object"), std::string::npos);
}

// ============================================================================
// TEST 10 — libraries is not an array
// ============================================================================
TEST(ProjectConfigLoaderTest, LibrariesNotArray) {
   auto result = ProjectConfigLoader::fromString(R"({
        "$schemaVersion": "1.0",
        "libraries": { "id": "x" }
      })");
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors[0].path, "libraries");
}

// ============================================================================
// TEST 11 — Path resolution semantics
// ============================================================================
TEST(ProjectConfigLoaderTest, PathResolution) {
   // Absolute paths pass through verbatim.
   EXPECT_EQ(ProjectConfigLoader::resolvePath("/base", "/abs/desc.json"), "/abs/desc.json");
   // Drive-letter absolute paths (Windows).
   EXPECT_EQ(ProjectConfigLoader::resolvePath("/base", "C:\\lib\\desc.json"), "C:\\lib\\desc.json");
   // Relative paths are joined against the base directory.
   EXPECT_EQ(ProjectConfigLoader::resolvePath("/home/me/st2cpp", "lib/desc.json"),
             "/home/me/st2cpp/lib/desc.json");
   EXPECT_EQ(ProjectConfigLoader::resolvePath("/home/me", "desc.json"), "/home/me/desc.json");
   // No (or dot) base directory: the path stays as declared.
   EXPECT_EQ(ProjectConfigLoader::resolvePath("", "lib/desc.json"), "lib/desc.json");
   EXPECT_EQ(ProjectConfigLoader::resolvePath(".", "lib/desc.json"), "lib/desc.json");
}

// ============================================================================
// TEST 12 — dirName semantics
// ============================================================================
TEST(ProjectConfigLoaderTest, DirName) {
   EXPECT_EQ(ProjectConfigLoader::dirName("/a/b/c.json"), "/a/b");
   EXPECT_EQ(ProjectConfigLoader::dirName("c.json"), ".");
   EXPECT_EQ(ProjectConfigLoader::dirName("/c.json"), "/");
   EXPECT_EQ(ProjectConfigLoader::dirName("sub/c.json"), "sub");
}

// ============================================================================
// TEST 13 — Nonexistent file reported as such
// ============================================================================
TEST(ProjectConfigLoaderTest, MissingFile) {
   auto result = ProjectConfigLoader::fromFile(samplePath("no_such_config.json"));
   ASSERT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("cannot open"), std::string::npos);
}