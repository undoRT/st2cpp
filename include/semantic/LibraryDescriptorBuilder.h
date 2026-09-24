/**
 * @file LibraryDescriptorBuilder.h
 * @brief Builds a LibraryDescriptor from an analyzed Structured Text library
 *
 * The builder is the semantic-to-descriptor bridge, symmetric to
 * LibrarySymbolImporter (descriptor -> semantics):
 *
 *   TranslationUnit (decorated AST) + SemanticInfo (SymbolTable)
 *                 -> LibraryDescriptorBuilder -> LibraryDescriptor
 *
 * The TranslationUnit supplies the syntactic details the semantic model does
 * not (yet) keep — initializer expressions, explicit enum values, STRING[n]
 * lengths — while the SemanticInfo/SymbolTable is the authority for everything
 * already resolved (type identity, parameter directions, const-ness, external
 * library ownership via Symbol::isExternal / Symbol::externalLibraryId).
 *
 * The builder deliberately does NOT:
 *   - read or write JSON (that is LibrarySerializer/LibraryLoader);
 *   - touch the filesystem, ProjectConfig or LibraryLoader;
 *   - generate C++ or know the CodeGenerator;
 *   - invent C++ bindings (cppBinding is left empty; a later enrichment step
 *     may add it);
 *   - re-resolve symbols or duplicate SemanticAnalyzer logic.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "ast/AST.h"
#include "library/LibraryDescriptor.h"
#include "semantic/SemanticInfo.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::semantic {

/**
 * @brief Options controlling a library export.
 * @details Structured Text cannot express library identity, so id/name/version
 * (and the optional description) must be supplied externally (e.g. by a CLI).
 * `dependencyVersions` is the explicit version policy for automatically
 * derived dependencies: key = external library id (case-insensitive),
 * value = version constraint text (e.g. "^1.0.0"). Versions are never
 * invented: a used external library without a policy entry is an export error.
 */
struct LibraryExportOptions {
    std::string id;          // required, non-empty
    std::string name;        // required, non-empty
    std::string version;     // required, semantic version string
    std::string description; // optional

    /// External library id (case-insensitive) -> version constraint text.
    std::map<std::string, std::string> dependencyVersions;
};

/**
 * @brief A single export error.
 * @details `entity` identifies the offending declaration
 * (e.g. "enum State", "struct Point.x", "function Clamp",
 * "function block Ton", "global gCounter", "options", "dependencies");
 * `message` explains why it cannot be exported.
 */
struct LibraryExportError {
    std::string entity;
    std::string message;

    std::string toString() const {
        return entity.empty() ? message : entity + ": " + message;
    }
};

/**
 * @brief Result of an export attempt.
 * @details The descriptor is produced best-effort; ok() is true only when it
 * is present AND no error was found. Mirrors LibraryLoadResult semantics.
 */
struct LibraryExportResult {
    std::optional<st2cpp::library::LibraryDescriptor> descriptor;
    std::vector<LibraryExportError> errors;

    bool ok() const { return descriptor.has_value() && errors.empty(); }
};

/**
 * @brief TranslationUnit + SemanticInfo -> LibraryDescriptor converter.
 */
class LibraryDescriptorBuilder {
private:
    struct Impl;

public:
    /**
     * @brief Build a semantic-only LibraryDescriptor for an ST library.
     * @details Entities are emitted in TranslationUnit declaration order
     * (per descriptor section); dependencies are derived from the external
     * library references actually used and emitted sorted by id, so the same
     * input always yields the same descriptor. Constructs not representable
     * in the v1 descriptor (POINTER TO / REF_TO, multi-dimensional arrays,
     * STRING[n] lengths, aliases, subranges, PROGRAM / INTERFACE / METHOD /
     * EXTENDS / IMPLEMENTS, RETAIN, AT, non-literal initializers, ...)
     * produce explicit export errors, never silent loss.
     * @param tu The decorated translation unit (initializers, enum values)
     * @param info The semantic analysis result (SymbolTable authority)
     * @param options Library identity and dependency version policy
     * @return The export result (descriptor + collected errors)
     */
    static LibraryExportResult build(const TranslationUnit& tu,
                                     const SemanticInfo& info,
                                     const LibraryExportOptions& options);
};

} // namespace st2cpp::semantic
