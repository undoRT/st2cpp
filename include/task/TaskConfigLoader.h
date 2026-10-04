/**
 * @file TaskConfigLoader.h
 * @brief Loads a Task Configuration from tasks.json and validates it
 *
 * The loader is the entry point of the pipeline
 *   tasks.json -> TaskConfigLoader -> TaskConfig (validated) -> RuntimeEmitter
 * It parses the JSON document (via the st2cpp::json module) and validates the
 * structure, collecting every problem it finds instead of failing on the first
 * one, so a malformed configuration reports all of its errors at once.
 *
 * Unlike the Project Configuration there is nothing to resolve against the
 * filesystem: every member is self-contained, so fromJson/fromString and
 * fromFile behave identically apart from opening the file.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "json/JsonValue.h"
#include "task/TaskConfig.h"
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::task {

using json::JsonValue;

/**
 * @brief JSON -> TaskConfig loader with validation.
 */
class TaskConfigLoader
{
public:
   /**
     * @brief Load from a parsed JSON document (structure validation only).
     */
   static TaskConfigLoadResult fromJson(const JsonValue& root);

   /**
     * @brief Load from a JSON text string (structure validation only).
     * @details Malformed JSON is captured as a single error.
     */
   static TaskConfigLoadResult fromString(const std::string& jsonText);

   /**
     * @brief Load from a tasks.json file on disk.
     */
   static TaskConfigLoadResult fromFile(const std::string& path);
};

} // namespace st2cpp::task