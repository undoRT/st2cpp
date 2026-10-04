/**
 * @file TaskConfig.h
 * @brief Data model of a Task Configuration (tasks.json -> C++ object model)
 *
 * The Task Configuration is the IEC 61131-3 / TwinCAT-style answer to "which
 * cyclic task runs which programs, how fast, at which priority and pinned to
 * which core". It is deliberately separate from the Project Configuration
 * (which libraries a project uses): one project may declare many tasks, and a
 * task owns no library information at all.
 *
 * The mapping onto undoPLC is one Master task per PLC instance and one Worker
 * task per entry of `tasks`, so `plc` is the grouping key and the entry order
 * inside a group is the Worker registration order.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::task {

/**
 * @brief One cyclic task, executed once every `cycleMs`.
 * @details `cpuAffinity` of -1 means "let undoPLC pick": the generated runtime
 * resolves the first isolated CPU no other task has taken. A value >= 0 pins
 * the task to that exact core.
 */
struct TaskEntry
{
   std::string name;         // required, non-empty, unique within its PLC
   std::string plc;          // required, non-empty; the PLC instance it belongs to
   long long cycleMs = 0;    // required, > 0
   int priority = 0;         // required, 1..99
   int cpuAffinity = -1;     // optional, >= -1, -1 = automatic
   std::vector<std::string> programs; // optional, called in this exact order
};

/**
 * @brief A validated Task Configuration (schema version 1.0).
 */
struct TaskConfig
{
   std::string schemaVersion = "1.0";
   std::vector<TaskEntry> tasks;
};

/**
 * @brief The tasks of one PLC instance, with the Master settings they imply.
 * @details undoPLC drives every Worker from a single Master cycle, so the
 * Master has to run at the finest cycle any of its tasks asked for and at a
 * priority strictly above all of them: a task that overruns must never be able
 * to delay the cycle that supervises it.
 */
struct PlcGroup
{
   std::string plc;      // PLC instance name, as declared
   long long cycleMs = 0;    // fastest `cycle_ms` in the group
   int masterPriority = 0;   // max(priority) + 1, always <= kMaxMasterPriority
   std::vector<size_t> tasks; // indices into TaskConfig::tasks, in declaration order
};

/**
 * @brief A single configuration validation/deserialization error.
 * @details `path` locates the offending member inside the document
 * (e.g. "tasks[1].programs[0]"); `message` explains the problem.
 */
struct TaskConfigError
{
   std::string path;
   std::string message;

   std::string toString() const { return path.empty() ? message : path + ": " + message; }
};

/**
 * @brief Result of a configuration load attempt.
 */
struct TaskConfigLoadResult
{
   std::optional<TaskConfig> config;
   std::vector<TaskConfigError> errors;

   bool ok() const { return config.has_value() && errors.empty(); }
};

/**
 * @brief Lowest SCHED_FIFO priority a cyclic task may declare.
 */
constexpr int kMinPriority = 1;

/**
 * @brief Highest SCHED_FIFO priority a cyclic task may declare.
 * @details SCHED_FIFO tops out at 99 and the Master of a PLC instance needs a
 * priority strictly above every task it supervises (see PlcGroup), so the top
 * value is reserved for the Master and a task may not claim it.
 */
constexpr int kMaxPriority = 98;

/**
 * @brief Highest priority undoPLC accepts for a Master task.
 */
constexpr int kMaxMasterPriority = 99;

/**
 * @brief Group the tasks by PLC instance, preserving declaration order.
 * @details The first appearance of a `plc` value fixes its position in the
 * result, and the tasks of a group keep the order they were declared in, which
 * is the order the generated runtime registers (and starts) its workers.
 */
std::vector<PlcGroup> groupByPlc(const TaskConfig& config);

} // namespace st2cpp::task