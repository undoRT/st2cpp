/**
 * @file ProjectConfigLoader.cpp
 * @brief ProjectConfigLoader implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "project/ProjectConfigLoader.h"
#include <cctype>
#include <fstream>
#include <sstream>

namespace st2cpp::project {

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
 * @brief Parse and validate a single "libraries[i]" entry.
 * @param node The entry JsonValue (object)
 * @param index The entry index (diagnostics)
 * @param errors The error collector
 * @param out The parsed entry
 */
void parseLibraryEntry(const JsonValue& node, size_t index, std::vector<ProjectConfigError>& errors, LibraryEntry& out)
{
   const std::string prefix = "libraries[" + std::to_string(index) + "]";
   if (!node.isObject()) {
      errors.push_back({prefix, "expected an object"});
      return;
   }

   const JsonValue* idNode = node.find("id");
   if (!idNode) {
      errors.push_back({prefix + ".id", "missing required member 'id'"});
   } else if (!idNode->isString() || isBlank(idNode->text)) {
      errors.push_back({prefix + ".id", "must be a non-empty string"});
   } else {
      out.id = idNode->text;
   }

   const JsonValue* versionNode = node.find("version");
   if (versionNode) {
      if (!versionNode->isString()) {
         errors.push_back({prefix + ".version", "must be a string"});
      } else {
         out.version = library::VersionConstraint::parse(versionNode->text);
         if (!out.version.valid) {
            errors.push_back({prefix + ".version",
               "invalid version constraint '" + versionNode->text + "'"});
         } else {
            out.hasVersion = true;
         }
      }
   }

   const JsonValue* pathNode = node.find("path");
   if (!pathNode) {
      errors.push_back({prefix + ".path", "missing required member 'path'"});
   } else if (!pathNode->isString() || isBlank(pathNode->text)) {
      errors.push_back({prefix + ".path", "must be a non-empty string"});
   } else {
      out.path = pathNode->text;
   }
}

} // namespace

ProjectConfigLoadResult ProjectConfigLoader::fromJson(const JsonValue& root)
{
   ProjectConfigLoadResult result;
   ProjectConfig cfg;

   if (!root.isObject()) {
      result.errors.push_back({"", "project configuration must be a JSON object"});
      return result;
   }

   const JsonValue* schemaNode = root.find("$schemaVersion");
   if (!schemaNode) {
      result.errors.push_back({"$schemaVersion", "missing required member '$schemaVersion'"});
   } else if (!schemaNode->isString()) {
      result.errors.push_back({"$schemaVersion", "must be a string"});
   } else if (schemaNode->text != kSupportedSchemaVersion) {
      result.errors.push_back({"$schemaVersion",
         "unsupported schema version '" + schemaNode->text + "' (supported: " + kSupportedSchemaVersion + ")"});
   } else {
      cfg.schemaVersion = schemaNode->text;
   }

   const JsonValue* nameNode = root.find("name");
   if (nameNode) {
      if (!nameNode->isString()) {
         result.errors.push_back({"name", "must be a string"});
      } else {
         cfg.name = nameNode->text;
      }
   }

   const JsonValue* libsNode = root.find("libraries");
   if (libsNode) {
      if (!libsNode->isArray()) {
         result.errors.push_back({"libraries", "must be an array"});
      } else {
         for (size_t i = 0; i < libsNode->array.size(); ++i) {
            LibraryEntry entry;
            parseLibraryEntry(libsNode->array[i], i, result.errors, entry);
            cfg.libraries.push_back(std::move(entry));
         }
      }
   }

   if (result.errors.empty()) {
      result.config = std::move(cfg);
   }
   return result;
}

ProjectConfigLoadResult ProjectConfigLoader::fromString(const std::string& jsonText)
{
   JsonValue root;
   try {
      root = json::parse(jsonText);
   } catch (const json::JsonParseError& e) {
      ProjectConfigLoadResult result;
      result.errors.push_back({"", std::string(e.what())});
      return result;
   }
   return fromJson(root);
}

std::string ProjectConfigLoader::dirName(const std::string& path)
{
   const size_t slash = path.find_last_of("/\\");
   if (slash == std::string::npos) {
      return ".";
   }
   if (slash == 0) {
      return "/";
   }
   return path.substr(0, slash);
}

std::string ProjectConfigLoader::resolvePath(const std::string& baseDir, const std::string& path)
{
   if (path.empty()) {
      return path;
   }
   // Absolute paths (POSIX, drive-letter and UNC on Windows) are used verbatim.
   const bool posixAbsolute = path[0] == '/' || path[0] == '\\';
   const bool driveAbsolute = path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
   if (posixAbsolute || driveAbsolute) {
      return path;
   }
   if (baseDir.empty() || baseDir == ".") {
      return path;
   }
   const char last = baseDir.back();
   if (last == '/' || last == '\\') {
      return baseDir + path;
   }
   return baseDir + "/" + path;
}

ProjectConfigLoadResult ProjectConfigLoader::fromFile(const std::string& path)
{
   std::ifstream file(path);
   if (!file.is_open()) {
      ProjectConfigLoadResult result;
      result.errors.push_back({"", "cannot open file '" + path + "'"});
      return result;
   }
   std::ostringstream buffer;
   buffer << file.rdbuf();

   ProjectConfigLoadResult result = fromString(buffer.str());
   if (!result.ok()) {
      return result;
   }

   const std::string baseDir = dirName(path);
   for (auto& entry : result.config->libraries) {
      entry.resolvedPath = resolvePath(baseDir, entry.path);
   }
   return result;
}

} // namespace st2cpp::project