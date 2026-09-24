/**
 * @file ProjectConfigLoader.h
 * @brief Loads a Project Configuration from JSON and validates it
 *
 * The loader is the entry point of the pipeline
 *   Project JSON -> ProjectConfig (validated)
 * It parses the JSON document (via the st2cpp::json module) and validates the
 * structure. Path semantics:
 *   - absolute paths are used verbatim;
 *   - relative paths are resolved against the directory that contains the
 *     configuration file (never blindly against the process working directory);
 *   - fromJson/fromString have no base directory, so they only validate and
 *     keep the declared path; ProjectConfigLoader::fromFile additionally fills
 *     LibraryEntry::resolvedPath.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "json/JsonValue.h"
#include "project/ProjectConfig.h"
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::project {

using json::JsonValue;

/**
 * @brief JSON -> ProjectConfig loader with validation.
 */
class ProjectConfigLoader {
public:
    /**
     * @brief Load from a parsed JSON document (structure validation only).
     */
    static ProjectConfigLoadResult fromJson(const JsonValue& root);

    /**
     * @brief Load from a JSON text string (structure validation only).
     * @details Malformed JSON is captured as a single error.
     */
    static ProjectConfigLoadResult fromString(const std::string& jsonText);

    /**
     * @brief Load from a JSON file on disk.
     * @details Resolves every library relative path against the directory of
     * this file (LibraryEntry::resolvedPath).
     */
    static ProjectConfigLoadResult fromFile(const std::string& path);

    /**
     * @brief Resolve a library path against a base directory.
     * @param baseDir Directory of the configuration file (may be empty)
     * @param path The declared (absolute or relative) path
     * @return The absolute path as-is, or baseDir joined with the relative path
     */
    static std::string resolvePath(const std::string& baseDir, const std::string& path);

    /**
     * @brief Directory part of a file path ("" when empty, "." when bare).
     */
    static std::string dirName(const std::string& path);
};

} // namespace st2cpp::project