/**
 * @file test_golden.cpp
 * @brief Golden tests for st2cpp - compares generated C++ against expected output
 *
 * These tests read .st files from the st_samples directory, generate C++ code,
 * and compare it with golden files (.golden.cpp and .golden.hpp).
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include <filesystem>
#include <vector>
#include <string>

// Allow the parameterized test suite to be uninstantiated (when no golden files exist)
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GoldenTest);

namespace fs = std::filesystem;

/**
 * @brief Parameters for a golden test
 */
struct GoldenTestParam
{
   std::string name;          // Test name (e.g., "basic_vars")
   std::string stPath;        // Path to .st file
   std::string goldenCppPath; // Path to expected .cpp file
   std::string goldenHppPath; // Path to expected .hpp file
};

/**
 * @brief Golden test fixture
 */
class GoldenTest : public ::testing::TestWithParam<GoldenTestParam>
{
protected:
   void SetUp() override
   {
      auto param = GetParam();
      m_st = TestHelper::readFile(param.stPath);
      m_expectedCpp = TestHelper::readFile(param.goldenCppPath);
      m_expectedHpp = TestHelper::readFile(param.goldenHppPath);
   }

   std::string m_st;
   std::string m_expectedCpp;
   std::string m_expectedHpp;
};

/**
 * @brief Test that generated code matches golden files
 */
TEST_P(GoldenTest, GenerateMatchesGolden)
{
   auto result = TestHelper::generateFromST(m_st);

   EXPECT_EQ(result.header, m_expectedHpp) << "Header mismatch for: " << GetParam().name;

   EXPECT_EQ(result.source, m_expectedCpp) << "Source mismatch for: " << GetParam().name;
}

/**
 * @brief Discover golden test files from the st_samples directory
 */
std::vector<GoldenTestParam> discoverGoldenTests()
{
   std::vector<GoldenTestParam> params;

   // The samples directory path is passed from CMake via ST_SAMPLES_DIR
   std::string samplesDir;
#ifdef ST_SAMPLES_DIR
   samplesDir = ST_SAMPLES_DIR;
#else
   samplesDir = "tests/st_samples/";
   if (!fs::exists(samplesDir)) {
      samplesDir = "st_samples/";
   }
#endif

   // Check if directory exists
   if (!fs::exists(samplesDir)) {
      // No golden tests available - skip
      return params;
   }

   // Iterate over all .st files
   for (const auto& entry : fs::directory_iterator(samplesDir)) {
      if (entry.path().extension() == ".st") {
         std::string base = entry.path().stem().string();
         if (base.empty() || base[0] == '_') {
            continue;
         }

         GoldenTestParam p;
         p.name = base;
         p.stPath = entry.path().string();
         p.goldenCppPath = samplesDir + base + ".golden.cpp";
         p.goldenHppPath = samplesDir + base + ".golden.hpp";

         if (fs::exists(p.goldenCppPath) && fs::exists(p.goldenHppPath)) {
            params.push_back(p);
         }
      }
   }

   return params;
}

// Se non ci sono golden test, INSTANTIATE_TEST_SUITE_P non farà nulla
// e il test verrà saltato automaticamente
INSTANTIATE_TEST_SUITE_P(AllGoldenTests, GoldenTest, ::testing::ValuesIn(discoverGoldenTests()));