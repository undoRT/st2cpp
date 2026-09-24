/**
 * @file test_project_loader.cpp
 * @brief Tests for ProjectLoader: ProjectConfig -> LibraryRegistry
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "project/ProjectConfigLoader.h"
#include "project/ProjectLoader.h"
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

ProjectConfig loadConfigOrDie(const std::string& name)
{
   auto cfgResult = ProjectConfigLoader::fromFile(samplePath(name));
   EXPECT_TRUE(cfgResult.ok());
   return std::move(cfgResult.config.value());
}
}

// ============================================================================
// TEST 1 — Load a single library
// ============================================================================
TEST(ProjectLoaderTest, SingleLibraryLoads) {
   ProjectConfig cfg = loadConfigOrDie("single_lib_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   EXPECT_TRUE(result.ok()) << "errors: " << result.errors.size();
   EXPECT_EQ(result.errors.size(), 0u);
   EXPECT_TRUE(result.registry.contains("corelib"));
   EXPECT_EQ(result.registry.size(), 1u);

   const auto* lib = result.registry.get("corelib");
   ASSERT_NE(lib, nullptr);
   EXPECT_EQ(lib->id, "corelib");
   EXPECT_NE(lib->findStruct("Range"), nullptr);
}

// ============================================================================
// TEST 2 — Two libraries with satisfied version constraints
// ============================================================================
TEST(ProjectLoaderTest, TwoLibrariesLoad) {
   ProjectConfig cfg = loadConfigOrDie("two_lib_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   EXPECT_TRUE(result.ok());
   EXPECT_EQ(result.registry.size(), 2u);
   EXPECT_TRUE(result.registry.contains("corelib"));
   EXPECT_TRUE(result.registry.contains("examplelib"));

   // examplelib declares a dependency on corelib ^1.0.0: already satisfied.
   const auto* examplelib = result.registry.get("examplelib");
   ASSERT_NE(examplelib, nullptr);
   EXPECT_TRUE(result.registry.missingDependencies(*examplelib).empty());
}

// ============================================================================
// TEST 3 — Minimal config: empty registry, no errors
// ============================================================================
TEST(ProjectLoaderTest, MinimalConfigEmptyRegistry) {
   ProjectConfig cfg = loadConfigOrDie("minimal_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);
   EXPECT_TRUE(result.ok());
   EXPECT_EQ(result.registry.size(), 0u);
}

// ============================================================================
// TEST 4 — Duplicate id (case-insensitive) rejected
// ============================================================================
TEST(ProjectLoaderTest, DuplicateLibraryRejected) {
   ProjectConfig cfg = loadConfigOrDie("dup_lib_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   ASSERT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("already registered"), std::string::npos);
   EXPECT_EQ(result.errors[0].libraryId, "CORElib");
   // The first registration sticks.
   EXPECT_EQ(result.registry.size(), 1u);
}

// ============================================================================
// TEST 5 — Descriptor id mismatch is an error
// ============================================================================
TEST(ProjectLoaderTest, DescriptorIdMismatch) {
   ProjectConfig cfg = loadConfigOrDie("wrong_id_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   ASSERT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("does not match the configured id"), std::string::npos);
   EXPECT_EQ(result.errors[0].libraryId, "examplelib");
   EXPECT_EQ(result.registry.size(), 0u);
}

// ============================================================================
// TEST 6 — Version constraint not satisfied
// ============================================================================
TEST(ProjectLoaderTest, IncompatibleVersion) {
   ProjectConfig cfg = loadConfigOrDie("incompatible_version_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   ASSERT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("does not satisfy constraint"), std::string::npos);
   EXPECT_EQ(result.registry.size(), 0u);
}

// ============================================================================
// TEST 7 — Missing descriptor file
// ============================================================================
TEST(ProjectLoaderTest, MissingDescriptorFile) {
   ProjectConfig cfg = loadConfigOrDie("missing_file_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);

   ASSERT_EQ(result.errors.size(), 1u);
   EXPECT_NE(result.errors[0].message.find("cannot open"), std::string::npos);
   EXPECT_EQ(result.errors[0].libraryId, "myLib");
   EXPECT_EQ(result.registry.size(), 0u);
}

// ============================================================================
// TEST 8 — Loaded registry supports deterministic ordering
// ============================================================================
TEST(ProjectLoaderTest, AllOrderedIsDeterministic) {
   ProjectConfig cfg = loadConfigOrDie("two_lib_config.json");
   ProjectLibraryResult result = ProjectLoader::load(cfg);
   ASSERT_TRUE(result.ok());

   auto ordered = result.registry.allOrdered();
   ASSERT_EQ(ordered.size(), 2u);
   EXPECT_EQ(ordered[0]->id, "corelib");
   EXPECT_EQ(ordered[1]->id, "examplelib");
}