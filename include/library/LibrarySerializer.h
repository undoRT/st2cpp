/**
 * @file LibrarySerializer.h
 * @brief Serializes a LibraryDescriptor back to JSON
 *
 * The serializer is the reverse side of LibraryLoader: it converts the
 * in-memory model into a canonical JSON document (v1.0). The canonical output
 * preserves the semantic content of the descriptor and is deterministic, so it
 * can be used for round-trip tests and comparisons; whitespace/formatting is
 * not guaranteed to match the input document.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "json/JsonValue.h"
#include "library/LibraryDescriptor.h"
#include <string>

namespace st2cpp::library {

/**
 * @brief LibraryDescriptor -> JSON converter.
 */
class LibrarySerializer {
public:
    /**
     * @brief Build the JSON document (st2cpp::json::JsonValue) for a descriptor.
     */
    static json::JsonValue toJsonValue(const LibraryDescriptor& desc);

    /**
     * @brief Serialize the descriptor to JSON text.
     * @param desc The descriptor to serialize
     * @param indent Indentation width per nesting level
     * @return The JSON text
     */
    static std::string toJson(const LibraryDescriptor& desc, int indent = 2);
};

} // namespace st2cpp::library