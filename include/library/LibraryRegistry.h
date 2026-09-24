/**
 * @file LibraryRegistry.h
 * @brief Registry of loaded libraries keyed by normalized id
 *
 * The registry holds the set of libraries loaded through the LibraryLoader.
 * It is the layer that the SemanticAnalyzer consumes so
 * that the analyzer never reads JSON directly: the analyzer will query the
 * registry for LibraryDescriptor objects resolved by id.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "library/LibraryDescriptor.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace st2cpp::library {

/**
 * @brief Contains the registered libraries, retrievable by id
 * (case-insensitive, IEC 61131-3 style).
 */
class LibraryRegistry {
public:
    /**
     * @brief Register a library.
     * @param descriptor The validated library descriptor to register
     * @param error Receives a description when registration fails
     * @return true on success; false when the id is already registered
     */
    bool registerLibrary(LibraryDescriptor descriptor, std::string& error);

    bool registerLibrary(LibraryDescriptor descriptor);

    /**
     * @brief Look up a library by id (case-insensitive).
     * @return pointer to the descriptor, or nullptr when not present
     */
    const LibraryDescriptor* get(const std::string& id) const;

    bool contains(const std::string& id) const;

    bool remove(const std::string& id);

    void clear() { byId_.clear(); }

    size_t size() const { return byId_.size(); }

    std::vector<std::string> ids() const;

    std::vector<const LibraryDescriptor*> all() const;

    /**
     * @brief All registered libraries ordered by normalized id.
     * @details Iteration order of `all()` is unspecified (hash map); this
     * helper gives the deterministic order used by the semantic importer so
     * that symbol collisions between libraries resolve consistently.
     * @return descriptors sorted by uppercase id
     */
    std::vector<const LibraryDescriptor*> allOrdered() const;

    /**
     * @brief Ids of dependencies declared by `lib` that are not registered.
     */
    std::vector<std::string> missingDependencies(const LibraryDescriptor& lib) const;

private:
    std::unordered_map<std::string, LibraryDescriptor> byId_;
};

} // namespace st2cpp::library