/**
 * @file ProjectConfig.h
 * @brief Data model of a Project Configuration (JSON -> C++ object model)
 *
 * The Project Configuration describes "quali librerie usa questo progetto e
 * dove trovarle" and is deliberately separate from the Library Descriptor,
 * which describes "che cosa contiene una libreria". The configuration is
 * consumed by the ProjectLoader, which loads each Library Descriptor into a
 * LibraryRegistry, and ultimately by the SemanticAnalyzer through the bridge
 * provided by the registry.
 *
 * Version constraints reuse the Library Descriptor model (VersionConstraint)
 * so that the constraint language and its parsing stay in one place.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "library/LibraryDescriptor.h"
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::project {

/**
 * @brief One library referenced by a project.
 * @details `path` is the path as declared in the configuration. `resolvedPath`
 * is set by ProjectConfigLoader::fromFile: absolute paths are kept verbatim,
 * relative paths are resolved against the directory of the configuration file.
 */
struct LibraryEntry {
    std::string id;                 // required, non-empty
    library::VersionConstraint version; // parsed constraint (valid when declared)
    bool hasVersion = false;        // true when a "version" member was declared
    std::string path;               // as declared
    std::string resolvedPath;       // set by fromFile (base-dir aware)
};

/**
 * @brief A validated Project Configuration (schema version 1.0).
 */
struct ProjectConfig {
    std::string schemaVersion;      // supported value: "1.0"
    std::string name;               // project display name (may be empty)
    std::vector<LibraryEntry> libraries;
};

/**
 * @brief A single configuration validation/deserialization error.
 * @details `path` locates the offending member inside the document
 * (e.g. "libraries[1].path"); `message` explains the problem.
 */
struct ProjectConfigError {
    std::string path;
    std::string message;

    std::string toString() const {
        return path.empty() ? message : path + ": " + message;
    }
};

/**
 * @brief Result of a configuration load attempt.
 */
struct ProjectConfigLoadResult {
    std::optional<ProjectConfig> config;
    std::vector<ProjectConfigError> errors;

    bool ok() const { return config.has_value() && errors.empty(); }
};

} // namespace st2cpp::project