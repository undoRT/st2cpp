/**
 * @file TaskConfigLoader.cpp
 * @brief TaskConfigLoader implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "task/TaskConfigLoader.h"
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace st2cpp::task {

namespace {

constexpr const char* kSupportedSchemaVersion = "1.0";

/**
 * @brief True when a string is empty or contains only whitespace.
 */
bool isBlank(const std::string& s)
{
   for (unsigned char c : s) {
      if (!std::isspace(c)) {
         return false;
      }
   }
   return true;
}

/**
 * @brief Case-folded key, because a task name is compared like IEC 61131-3
 *        identifiers are resolved: case-insensitively.
 */
std::string foldKey(const std::string& s)
{
   std::string key;
   key.reserve(s.size());
   for (unsigned char c : s) {
      if (!std::isspace(c)) {
         key.push_back(static_cast<char>(std::toupper(c)));
      }
   }
   return key;
}

/**
 * @brief Read a required non-empty string member.
 */
const std::string* readRequiredString(const JsonValue& node,
                                      const std::string& prefix,
                                      const char* member,
                                      std::vector<TaskConfigError>& errors)
{
   const std::string path = prefix + "." + member;
   const JsonValue* value = node.find(member);
   if (!value) {
      errors.push_back({path, std::string("missing required member '") + member + "'"});
      return nullptr;
   }
   if (!value->isString()) {
      errors.push_back({path, "must be a string"});
      return nullptr;
   }
   if (isBlank(value->text)) {
      errors.push_back({path, "must be a non-empty string"});
      return nullptr;
   }
   return &value->text;
}

/**
 * @brief Read an optional integer member, falling back to `fallback`.
 * @details A present-but-wrongly-typed member is an error, never a silent
 * fallback: the alternative is a task quietly running at the default cycle.
 */
bool readOptionalInt(const JsonValue& node,
                     const std::string& prefix,
                     const char* member,
                     long long fallback,
                     long long& out,
                     std::vector<TaskConfigError>& errors)
{
   const JsonValue* value = node.find(member);
   if (!value) {
      out = fallback;
      return true;
   }
   const std::optional<int64_t> parsed = value->asInt64();
   if (!parsed.has_value()) {
      errors.push_back({prefix + "." + member, "must be an integer"});
      return false;
   }
   out = *parsed;
   return true;
}

/**
 * @brief Parse and validate a single "tasks[i]" entry.
 */
void parseTaskEntry(const JsonValue& node, size_t index, std::vector<TaskConfigError>& errors, TaskEntry& out)
{
   const std::string prefix = "tasks[" + std::to_string(index) + "]";
   if (!node.isObject()) {
      errors.push_back({prefix, "expected an object"});
      return;
   }

   if (const std::string* name = readRequiredString(node, prefix, "name", errors)) {
      out.name = *name;
   }
   if (const std::string* plc = readRequiredString(node, prefix, "plc", errors)) {
      out.plc = *plc;
   }

   long long cycleMs = 0;
   if (readOptionalInt(node, prefix, "cycle_ms", 0, cycleMs, errors)) {
      if (cycleMs <= 0) {
         errors.push_back({prefix + ".cycle_ms", "must be a positive number of milliseconds"});
         cycleMs = 0;
      }
   }
   out.cycleMs = cycleMs;

   long long priority = 0;
   if (readOptionalInt(node, prefix, "priority", 0, priority, errors)) {
      if (priority < kMinPriority || priority > kMaxPriority) {
         errors.push_back({prefix + ".priority",
                           "must be between " + std::to_string(kMinPriority) + " and " + std::to_string(kMaxPriority)});
         priority = 0;
      }
   }
   out.priority = static_cast<int>(priority);

   long long cpuAffinity = -1;
   if (readOptionalInt(node, prefix, "cpu_affinity", -1, cpuAffinity, errors)) {
      if (cpuAffinity < -1) {
         errors.push_back({prefix + ".cpu_affinity", "must be -1 (automatic) or a CPU number >= 0"});
         cpuAffinity = -1;
      } else if (cpuAffinity > 0xFFFF) {
         errors.push_back({prefix + ".cpu_affinity", "must be -1 (automatic) or a CPU number <= 65535"});
         cpuAffinity = -1;
      }
   }
   out.cpuAffinity = static_cast<int>(cpuAffinity);

   const JsonValue* programs = node.find("programs");
   if (programs) {
      if (!programs->isArray()) {
         errors.push_back({prefix + ".programs", "must be an array of program names"});
      } else {
         for (size_t i = 0; i < programs->array.size(); ++i) {
            const std::string path = prefix + ".programs[" + std::to_string(i) + "]";
            const JsonValue& entry = programs->array[i];
            if (!entry.isString()) {
               errors.push_back({path, "must be a string"});
            } else if (isBlank(entry.text)) {
               errors.push_back({path, "must be a non-empty program name"});
            } else {
               out.programs.push_back(entry.text);
            }
         }
      }
   }
}

} // namespace

std::vector<PlcGroup> groupByPlc(const TaskConfig& config)
{
   std::vector<PlcGroup> groups;
   std::map<std::string, size_t> indexOf;

   for (size_t i = 0; i < config.tasks.size(); ++i) {
      const TaskEntry& task = config.tasks[i];
      // Folded so "undoPLC" and "undoplc" are one PLC instance, the way the
      // identifier policy folds the rest of the pipeline.
      const std::string key = foldKey(task.plc);
      auto it = indexOf.find(key);
      if (it == indexOf.end()) {
         indexOf.emplace(key, groups.size());
         PlcGroup group;
         group.plc = task.plc;
         group.cycleMs = task.cycleMs;
         group.masterPriority = task.priority + 1;
         group.tasks.push_back(i);
         groups.push_back(std::move(group));
         continue;
      }

      PlcGroup& group = groups[it->second];
      group.tasks.push_back(i);
      if (task.cycleMs < group.cycleMs) {
         group.cycleMs = task.cycleMs;
      }
      if (task.priority + 1 > group.masterPriority) {
         group.masterPriority = task.priority + 1;
      }
   }

   // Tasks are capped at kMaxPriority, so the Master derived above always fits
   // in the SCHED_FIFO range without losing its strictly-higher guarantee.
   for (PlcGroup& group : groups) {
      if (group.masterPriority > kMaxMasterPriority) {
         group.masterPriority = kMaxMasterPriority;
      }
   }

   return groups;
}

TaskConfigLoadResult TaskConfigLoader::fromJson(const JsonValue& root)
{
   TaskConfigLoadResult result;
   TaskConfig cfg;

   if (!root.isObject()) {
      result.errors.push_back({"", "task configuration must be a JSON object"});
      return result;
   }

   // Optional: a document that omits it is read as the version this loader
   // implements, which keeps hand-written tasks.json free of boilerplate.
   if (const JsonValue* schemaNode = root.find("$schemaVersion")) {
      if (!schemaNode->isString()) {
         result.errors.push_back({"$schemaVersion", "must be a string"});
      } else if (schemaNode->text != kSupportedSchemaVersion) {
         result.errors.push_back(
            {"$schemaVersion", "unsupported schema version '" + schemaNode->text + "' (supported: " + kSupportedSchemaVersion + ")"});
      } else {
         cfg.schemaVersion = schemaNode->text;
      }
   }

   const JsonValue* tasksNode = root.find("tasks");
   if (!tasksNode) {
      result.errors.push_back({"tasks", "missing required member 'tasks'"});
      return result;
   }
   if (!tasksNode->isArray()) {
      result.errors.push_back({"tasks", "must be an array"});
      return result;
   }

   for (size_t i = 0; i < tasksNode->array.size(); ++i) {
      TaskEntry entry;
      parseTaskEntry(tasksNode->array[i], i, result.errors, entry);
      cfg.tasks.push_back(std::move(entry));
   }

   // A PLC instance runs one task per name: two entries sharing a name inside
   // the same `plc` would generate two C++ classes with the same spelling.
   std::map<std::string, std::set<std::string>> seenNames;
   for (size_t i = 0; i < cfg.tasks.size(); ++i) {
      const TaskEntry& task = cfg.tasks[i];
      if (task.name.empty() || task.plc.empty()) {
         continue; // already reported by parseTaskEntry
      }
      const std::string prefix = "tasks[" + std::to_string(i) + "]";
      const std::string key = foldKey(task.name);
      if (!seenNames[foldKey(task.plc)].insert(key).second) {
         result.errors.push_back({prefix + ".name", "duplicate task name '" + task.name + "' in plc '" + task.plc + "'"});
      }
   }

   if (result.errors.empty()) {
      result.config = std::move(cfg);
   }
   return result;
}

TaskConfigLoadResult TaskConfigLoader::fromString(const std::string& jsonText)
{
   JsonValue root;
   try {
      root = json::parse(jsonText);
   } catch (const json::JsonParseError& e) {
      TaskConfigLoadResult result;
      result.errors.push_back({"", std::string(e.what())});
      return result;
   }
   return fromJson(root);
}

TaskConfigLoadResult TaskConfigLoader::fromFile(const std::string& path)
{
   std::ifstream file(path);
   if (!file.is_open()) {
      TaskConfigLoadResult result;
      result.errors.push_back({"", "cannot open file '" + path + "'"});
      return result;
   }
   std::ostringstream buffer;
   buffer << file.rdbuf();
   return fromString(buffer.str());
}

} // namespace st2cpp::task