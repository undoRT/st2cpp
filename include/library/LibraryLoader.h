/**
 * @file LibraryLoader.h
 * @brief Loads a LibraryDescriptor from JSON and validates it
 *
 * The loader is the single entry point of the pipeline
 *   JSON -> LibraryDescriptor (validated)
 * It parses the JSON document (via the st2cpp::json module), deserializes it
 * into the LibraryDescriptor model and runs a full validation pass. Errors are
 * collected, not fail-fast, and each carries a descriptive path into the
 * descriptor plus a human-readable message.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "json/JsonValue.h"
#include "library/LibraryDescriptor.h"
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::library {

using json::JsonValue;

/**
 * @brief A single validation/deserialization error.
 * @details `path` locates the offending value inside the descriptor
 * (e.g. "constants[1].type" or "functions[0].cppBinding");
 * `message` explains the problem in diagnostic terms.
 */
struct LibraryLoadError {
    std::string path;
    std::string message;

    std::string toString() const {
        return path.empty() ? message : path + ": " + message;
    }
};

/**
 * @brief Result of a load attempt.
 * @details The descriptor is produced best-effort; ok() is true only when it
 * is present AND no error was found (validated descriptor).
 */
struct LibraryLoadResult {
    std::optional<LibraryDescriptor> descriptor;
    std::vector<LibraryLoadError> errors;

    bool ok() const { return descriptor.has_value() && errors.empty(); }
};

/**
 * @brief JSON -> LibraryDescriptor loader with validation.
 */
class LibraryLoader {
public:
    /**
     * @brief Load from a parsed JSON document.
     */
    static LibraryLoadResult fromJson(const JsonValue& root);

    /**
     * @brief Load from a JSON text string.
     * @details Malformed JSON is captured as a single error.
     */
    static LibraryLoadResult fromString(const std::string& jsonText);

    /**
     * @brief Load from a JSON file on disk.
     */
    static LibraryLoadResult fromFile(const std::string& path);
};

} // namespace st2cpp::library