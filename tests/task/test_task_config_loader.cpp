/**
 * @file test_task_config_loader.cpp
 * @brief Implementation of test cases for the task configuration loader
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "task/TaskConfigLoader.h"
#include <filesystem>

namespace fs = std::filesystem;
using st2cpp::task::TaskConfigLoader;
using st2cpp::task::TaskConfigLoadResult;
using st2cpp::task::groupByPlc;

// ============================================================================
//  Fixture
// ============================================================================

namespace {

/**
 * @brief Loads one of the fixture files copied next to the test binary
 */
TaskConfigLoadResult load(const std::string& fileName)
{
   return TaskConfigLoader::fromFile((fs::path(TASK_SAMPLES_DIR) / fileName).string());
}

} // namespace

/**
 * @brief Shared fixture for every task configuration test
 */
class TaskConfigLoaderTest : public ::testing::Test
{
};

// ============================================================================
//  Happy path
// ============================================================================

/**
 * @brief A complete configuration keeps every declared value
 */
TEST_F(TaskConfigLoaderTest, ValidConfigurationIsLoaded)
{
   auto result = load("valid_tasks.json");

   ASSERT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front().toString());
   ASSERT_EQ(result.config->tasks.size(), 4u);

   const auto& first = result.config->tasks[0];
   EXPECT_EQ(first.name, "main_cycle");
   EXPECT_EQ(first.plc, "Line1");
   EXPECT_EQ(first.cycleMs, 10);
   EXPECT_EQ(first.priority, 40);
   EXPECT_EQ(first.cpuAffinity, -1); // automatic by default
   ASSERT_EQ(first.programs.size(), 2u);
   EXPECT_EQ(first.programs[0], "MAIN");
   EXPECT_EQ(first.programs[1], "Control"); // order is preserved
}

/**
 * @brief An explicit CPU affinity is kept as declared
 */
TEST_F(TaskConfigLoaderTest, ExplicitCpuAffinityIsKept)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());

   const auto& slow = result.config->tasks[1];
   EXPECT_EQ(slow.cpuAffinity, 5);
}

/**
 * @brief A task without programs is legal: it just runs nothing
 */
TEST_F(TaskConfigLoaderTest, TaskWithoutProgramsIsAllowed)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());

   EXPECT_TRUE(result.config->tasks[2].programs.empty());
}

/**
 * @brief The schema version is optional and defaults to 1.0
 */
TEST_F(TaskConfigLoaderTest, SchemaVersionDefaultsWhenAbsent)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());
   EXPECT_EQ(result.config->schemaVersion, "1.0");
}

/**
 * @brief An explicit schema version is read
 */
TEST_F(TaskConfigLoaderTest, ExplicitSchemaVersionIsRead)
{
   auto result = load("schema_version_tasks.json");
   ASSERT_TRUE(result.ok());
   EXPECT_EQ(result.config->schemaVersion, "1.0");
   EXPECT_EQ(result.config->tasks[0].cpuAffinity, 0);
}

/**
 * @brief An empty task list is valid, it just generates nothing
 */
TEST_F(TaskConfigLoaderTest, EmptyTaskListIsValid)
{
   auto result = load("no_tasks.json");
   EXPECT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front().toString());
   EXPECT_TRUE(result.config->tasks.empty());
}

// ============================================================================
//  Grouping
// ============================================================================

/**
 * @brief Tasks are grouped by PLC instance, in first-appearance order
 */
TEST_F(TaskConfigLoaderTest, TasksAreGroupedByPlc)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());

   const auto groups = groupByPlc(*result.config);
   ASSERT_EQ(groups.size(), 2u);

   EXPECT_EQ(groups[0].plc, "Line1");
   EXPECT_EQ(groups[0].tasks.size(), 3u);
   EXPECT_EQ(groups[1].plc, "Line2");
   EXPECT_EQ(groups[1].tasks.size(), 1u);
}

/**
 * @brief The PLC name is a key, so casing and spacing must not fork it
 */
TEST_F(TaskConfigLoaderTest, PlcGroupingIsCaseAndSpaceInsensitive)
{
   st2cpp::task::TaskConfig config;
   st2cpp::task::TaskEntry a;
   a.name = "one";
   a.plc = "Line1";
   a.cycleMs = 10;
   a.priority = 40;
   st2cpp::task::TaskEntry b = a;
   b.name = "two";
   b.plc = " line1 ";
   config.tasks = {a, b};

   const auto groups = groupByPlc(config);
   EXPECT_EQ(groups.size(), 1u);
   EXPECT_EQ(groups[0].tasks.size(), 2u);
}

/**
 * @brief undoPLC drives a master cycle, so it runs at the finest cycle asked for
 */
TEST_F(TaskConfigLoaderTest, MasterCycleIsTheFastestTaskCycle)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());

   const auto groups = groupByPlc(*result.config);
   ASSERT_EQ(groups.size(), 2u);
   EXPECT_EQ(groups[0].cycleMs, 10); // not 50, even though "slow" exists
   EXPECT_EQ(groups[1].cycleMs, 20);
}

/**
 * @brief The master has to preempt the tasks it supervises
 */
TEST_F(TaskConfigLoaderTest, MasterPriorityIsAboveEveryTask)
{
   auto result = load("valid_tasks.json");
   ASSERT_TRUE(result.ok());

   const auto groups = groupByPlc(*result.config);
   ASSERT_EQ(groups.size(), 2u);
   EXPECT_EQ(groups[0].masterPriority, 51); // max task priority 50 + 1
   EXPECT_EQ(groups[1].masterPriority, 41);
}

// ============================================================================
//  Rejected configurations
// ============================================================================

/**
 * @brief A configuration that cannot produce a working runtime is refused
 * @details Each rule below is a fixture that must be rejected, with the reason
 *          it names the offending member (checked in the individual cases).
 */
class TaskConfigLoaderInvalidTest : public ::testing::TestWithParam<const char*>
{
};

TEST_P(TaskConfigLoaderInvalidTest, InvalidConfigurationIsRejected)
{
   auto result = load(GetParam());
   EXPECT_FALSE(result.ok());
   EXPECT_FALSE(result.errors.empty());
}

INSTANTIATE_TEST_SUITE_P(RejectedConfigurations,
                        TaskConfigLoaderInvalidTest,
                        ::testing::Values("missing_name.json",
                                          "missing_plc.json",
                                          "duplicate_task.json",
                                          "zero_cycle.json",
                                          "priority_out_of_range.json",
                                          "negative_affinity.json",
                                          "wrong_programs_type.json",
                                          "tasks_not_array.json"));

/**
 * @brief A task needs a name to become a class and to be reported at run time
 */
TEST_F(TaskConfigLoaderTest, MissingNameIsReported)
{
   auto result = load("missing_name.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("name"), std::string::npos);
}

/**
 * @brief A task needs to know which PLC instance runs it
 */
TEST_F(TaskConfigLoaderTest, MissingPlcIsReported)
{
   auto result = load("missing_plc.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("plc"), std::string::npos);
}

/**
 * @brief Two tasks with the same name would generate the same class
 */
TEST_F(TaskConfigLoaderTest, DuplicateTaskInSamePlcIsReported)
{
   auto result = load("duplicate_task.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("uplicate"), std::string::npos);
}

/**
 * @brief A zero cycle would make the master spin
 */
TEST_F(TaskConfigLoaderTest, ZeroCycleIsReported)
{
   auto result = load("zero_cycle.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("cycle_ms"), std::string::npos);
}

/**
 * @brief SCHED_FIFO priorities stop at 99, and the master needs one above the
 *        task, so a task may not take the top one
 */
TEST_F(TaskConfigLoaderTest, TopPriorityIsReported)
{
   auto result = load("priority_out_of_range.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("priority"), std::string::npos);
}

/**
 * @brief Only -1 (automatic) or a real core number are meaningful
 */
TEST_F(TaskConfigLoaderTest, NegativeAffinityBelowAutomaticIsReported)
{
   auto result = load("negative_affinity.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("cpu_affinity"), std::string::npos);
}

/**
 * @brief Programs are named, not numbered
 */
TEST_F(TaskConfigLoaderTest, NonStringProgramIsReported)
{
   auto result = load("wrong_programs_type.json");
   ASSERT_FALSE(result.ok());
   EXPECT_NE(result.errors.front().toString().find("programs"), std::string::npos);
}

/**
 * @brief The loader must not invent a configuration when the file is absent
 */
TEST_F(TaskConfigLoaderTest, MissingFileIsReported)
{
   auto result = TaskConfigLoader::fromFile((fs::path(TASK_SAMPLES_DIR) / "does_not_exist.json").string());
   EXPECT_FALSE(result.ok());
   EXPECT_FALSE(result.errors.empty());
}
