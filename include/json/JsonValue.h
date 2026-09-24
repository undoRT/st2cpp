/**
 * @file JsonValue.h
 * @brief Minimal self-contained JSON DOM, parser and serializer
 *
 * The JSON module is a small, dependency-free implementation used by the
 * Library Descriptor pipeline (JSON file -> LibraryDescriptor). It provides a
 * type-erased document model (JsonValue), a recursive-descent parser with
 * line/column diagnostics (JsonParseError) and a serializer with configurable
 * indentation. Object member order is preserved on both parse and dump.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace st2cpp::json {

/**
 * @brief Discriminant of a JsonValue node.
 */
enum class JsonType : uint8_t {
   Null,
   Bool,
   Number,
   String,
   Array,
   Object
};

/**
 * @brief Error thrown by parse() when the input text is not valid JSON.
 * @details Carries the 1-based line and column where the error was detected,
 * so callers can build useful diagnostics. Derives from std::runtime_error;
 * the what() string already includes the position.
 */
class JsonParseError : public std::runtime_error
{
public:
   JsonParseError(const std::string& message, size_t line, size_t column)
      : std::runtime_error("JSON error at " + std::to_string(line) + ":" + std::to_string(column) + ": " + message), line_(line),
        column_(column)
   {}

   size_t line() const noexcept { return line_; }
   size_t column() const noexcept { return column_; }

private:
   size_t line_;
   size_t column_;
};

/**
 * @brief A single JSON document model node.
 * @details One struct discriminates on JsonType. Numbers keep both the parsed
 * double and the exact lexical token (numberRaw) so that serializing a
 * document round-trips values like "8", "0.0" or "1e3" verbatim. Object
 * members are stored as an ordered vector of key/value pairs.
 */
struct JsonValue
{
   JsonType type = JsonType::Null;
   bool boolean = false;
   double number = 0.0;
   std::string numberRaw;                                  // exact lexical form of a number
   std::string text;                                       // string contents (without quotes)
   std::vector<JsonValue> array;                           // array elements
   std::vector<std::pair<std::string, JsonValue>> members; // object members

   // ---- type predicates ----
   bool isNull() const { return type == JsonType::Null; }
   bool isBool() const { return type == JsonType::Bool; }
   bool isNumber() const { return type == JsonType::Number; }
   bool isString() const { return type == JsonType::String; }
   bool isArray() const { return type == JsonType::Array; }
   bool isObject() const { return type == JsonType::Object; }

   /**
     * @brief Look up a member by key; returns the first match or nullptr.
     */
   const JsonValue* find(const std::string& key) const
   {
      if (type != JsonType::Object) {
         return nullptr;
      }
      for (const auto& member : members) {
         if (member.first == key) {
            return &member.second;
         }
      }
      return nullptr;
   }

   /**
     * @brief Interpret the node as a signed 64-bit integer.
     * @details Valid only for Number nodes whose lexical form is integral
     * (no fraction/exponent), and for Bool (false/true -> 0/1).
     */
   std::optional<int64_t> asInt64() const
   {
      if (type == JsonType::Bool) {
         return boolean ? 1 : 0;
      }
      if (type != JsonType::Number) {
         return std::nullopt;
      }
      const std::string& raw = numberRaw.empty() ? text : numberRaw;
      if (raw.empty()) {
         return std::nullopt;
      }
      size_t i = 0;
      if (raw[i] == '-') {
         ++i;
      }
      bool integral = i < raw.size();
      for (; i < raw.size(); ++i) {
         if (!(raw[i] >= '0' && raw[i] <= '9')) {
            integral = false;
            break;
         }
      }
      if (!integral) {
         return std::nullopt;
      }
      return static_cast<int64_t>(number);
   }

   /**
     * @brief Interpret the node as a double (Number only).
     */
   std::optional<double> asDouble() const
   {
      if (type != JsonType::Number) {
         return std::nullopt;
      }
      return number;
   }

   /**
     * @brief Canonical textual form of a scalar node.
     * @details Strings yield their contents, numbers their exact lexical form,
     * booleans "true"/"false", null "null". Used by deserializers that need a
     * scalar string representation (suffixes such as "T#500ms", enumerators,
     * numeric literals).
     */
   std::string asString() const
   {
      switch (type) {
      case JsonType::String:
         return text;
      case JsonType::Number:
         return numberRaw.empty() ? std::to_string(number) : numberRaw;
      case JsonType::Bool:
         return boolean ? "true" : "false";
      case JsonType::Null:
         return "null";
      case JsonType::Array:
      case JsonType::Object:
         return std::string();
      }
      return std::string();
   }

   /**
     * @brief Interpret the node as a boolean (Bool nodes only).
     */
   bool asBool() const { return type == JsonType::Bool && boolean; }
};

/**
 * @brief Parse a JSON document.
 * @param text The input JSON text
 * @return The root value
 * @throws JsonParseError on malformed input (position included)
 */
JsonValue parse(const std::string& text);

/**
 * @brief Serialize a JSON document to text.
 * @param value The document to serialize
 * @param indent Indentation width per nesting level; negative for compact
 * @return The JSON text
 */
std::string dump(const JsonValue& value, int indent = 0);

} // namespace st2cpp::json