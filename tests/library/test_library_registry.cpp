/**
 * @file test_library_registry.cpp
 * @brief Tests for LibraryRegistry
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "library/LibraryLoader.h"
#include "library/LibraryRegistry.h"

using namespace st2cpp::library;

namespace {

// Load a descriptor from a raw JSON string; fail the test on error.
LibraryDescriptor descriptorFromText(const std::string& text)
{
   LibraryLoadResult r = LibraryLoader::fromString(text);
   if (!r.descriptor) {
      ADD_FAILURE() << "failed to load descriptor: " << (r.errors.empty() ? "unknown error" : r.errors[0].message);
      return {};
   }
   return *r.descriptor;
}

LibraryDescriptor loadSample()
{
   return descriptorFromText("{\"$schemaVersion\": \"1.0\", \"id\": \"examplelib\", \"name\": \"ExampleLib\", "
                             "\"version\": \"1.0.0\", \"dependencies\": [{\"id\": \"corelib\", \"version\": \"^1.0.0\"}], "
                             "\"functions\": [{\"name\": \"F\", \"returnType\": {\"kind\": \"primitive\", \"name\": \"INT\"}, "
                             "\"cppBinding\": {\"symbol\": \"f\", \"kind\": \"freeFunction\"}, \"parameters\": []}]}");
}

} // namespace

TEST(LibraryRegistry, RegisterAndGet)
{
   LibraryRegistry registry;
   LibraryDescriptor d = loadSample();
   ASSERT_FALSE(d.id.empty());

   registry.registerLibrary(d);
   const LibraryDescriptor* got = registry.get("EXAMPLELIB");
   ASSERT_NE(got, nullptr);
   EXPECT_EQ(got->id, "examplelib");
   EXPECT_EQ(got->name, "ExampleLib");
   EXPECT_EQ(got->functions.size(), 1u);
}

TEST(LibraryRegistry, GetMissingReturnsNull)
{
   LibraryRegistry registry;
   EXPECT_EQ(registry.get("nope"), nullptr);
   EXPECT_FALSE(registry.contains("nope"));
   EXPECT_EQ(registry.size(), 0u);
}

TEST(LibraryRegistry, DuplicateIdRejected)
{
   LibraryRegistry registry;
   LibraryDescriptor d = loadSample();
   std::string error;
   EXPECT_TRUE(registry.registerLibrary(d, error));
   error.clear();
   EXPECT_FALSE(registry.registerLibrary(d, error));
   EXPECT_FALSE(error.empty());
   EXPECT_EQ(registry.size(), 1u);
}

TEST(LibraryRegistry, EmptyIdRejected)
{
   LibraryRegistry registry;
   LibraryDescriptor d;
   d.id = "";
   std::string error;
   EXPECT_FALSE(registry.registerLibrary(std::move(d), error));
   EXPECT_FALSE(error.empty());
   EXPECT_EQ(registry.size(), 0u);
}

TEST(LibraryRegistry, RemoveAndClear)
{
   LibraryRegistry registry;
   registry.registerLibrary(loadSample());
   registry.registerLibrary(
      descriptorFromText("{\"$schemaVersion\": \"1.0\", \"id\": \"other\", \"name\": \"Other\", \"version\": \"1.0.0\"}"));

   EXPECT_EQ(registry.size(), 2u);
   EXPECT_TRUE(registry.remove("examplelib"));
   EXPECT_FALSE(registry.remove("examplelib"));
   EXPECT_EQ(registry.size(), 1u);

   registry.clear();
   EXPECT_EQ(registry.size(), 0u);
   EXPECT_TRUE(registry.ids().empty());
}

TEST(LibraryRegistry, IdsAndAll)
{
   LibraryRegistry registry;
   registry.registerLibrary(
      descriptorFromText("{\"$schemaVersion\": \"1.0\", \"id\": \"alpha\", \"name\": \"Alpha\", \"version\": \"1.0.0\"}"));
   registry.registerLibrary(
      descriptorFromText("{\"$schemaVersion\": \"1.0\", \"id\": \"beta\", \"name\": \"Beta\", \"version\": \"2.0.0\"}"));

   std::vector<std::string> ids = registry.ids();
   ASSERT_EQ(ids.size(), 2u);
   EXPECT_TRUE(std::find(ids.begin(), ids.end(), "alpha") != ids.end());
   EXPECT_TRUE(std::find(ids.begin(), ids.end(), "beta") != ids.end());

   std::vector<const LibraryDescriptor*> all = registry.all();
   EXPECT_EQ(all.size(), 2u);
}

TEST(LibraryRegistry, MissingDependencies)
{
   LibraryRegistry registry;
   LibraryDescriptor d = loadSample();
   ASSERT_EQ(d.dependencies.size(), 1u);

   std::vector<std::string> missing = registry.missingDependencies(d);
   ASSERT_EQ(missing.size(), 1u);
   EXPECT_EQ(missing[0], "corelib");

   registry.registerLibrary(
      descriptorFromText("{\"$schemaVersion\": \"1.0\", \"id\": \"corelib\", \"name\": \"CoreLib\", \"version\": \"1.0.0\"}"));
   EXPECT_TRUE(registry.missingDependencies(d).empty());
   EXPECT_FALSE(registry.contains("CORELIB") == false);
}