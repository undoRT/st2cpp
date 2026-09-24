/**
 * @file ProjectLoader.h
 * @brief Loads every configured Library Descriptor into a LibraryRegistry
 *
 * Pipeline:
 *   ProjectConfig -> (per entry) LibraryLoader::fromFile -> LibraryDescriptor
 *                   -> LibraryRegistry
 *
 * The loader verifies, per entry:
 *   - the descriptor file exists and is valid JSON / a valid descriptor;
 *   - the descriptor id matches the configured id (case-insensitive);
 *   - when a version constraint is declared, the descriptor version satisfies it;
 *   - the library is not already registered (duplicate declaration is an error).
 *
 * The descriptor `dependencies` list is not resolved: full semver dependency
 * resolution remains out of scope (missing dependencies can be inspected with
 * LibraryRegistry::missingDependencies).
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "library/LibraryLoader.h"
#include "library/LibraryRegistry.h"
#include "project/ProjectConfig.h"
#include <string>
#include <vector>

namespace st2cpp::project {

/**
 * @brief A library-loading error with enough context for good diagnostics.
 */
struct ProjectLibraryError {
    std::string libraryId; // configured id (the offending entry)
    std::string path;      // resolved descriptor path
    std::string message;   // human-readable cause

    std::string toString() const {
        std::string out = "library '" + libraryId + "'";
        if (!path.empty()) {
            out += " (" + path + ")";
        }
        out += ": " + message;
        return out;
    }
};

/**
 * @brief Result of loading the configured libraries.
 */
struct ProjectLibraryResult {
    library::LibraryRegistry registry;   // libraries actually loaded
    std::vector<ProjectLibraryError> errors;

    bool ok() const { return errors.empty(); }
};

/**
 * @brief ProjectConfig -> LibraryRegistry loader.
 */
class ProjectLoader {
public:
    /**
     * @brief Load every configured library descriptor into a new registry.
     * @param config The validated project configuration
     * @return Registry with the loaded libraries plus aggregated errors
     */
    static ProjectLibraryResult load(const ProjectConfig& config);
};

} // namespace st2cpp::project