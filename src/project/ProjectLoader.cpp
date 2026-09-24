/**
 * @file ProjectLoader.cpp
 * @brief ProjectLoader implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "project/ProjectLoader.h"

namespace st2cpp::project {

ProjectLibraryResult ProjectLoader::load(const ProjectConfig& config)
{
   ProjectLibraryResult result;
   for (const auto& entry : config.libraries) {
      const std::string path = entry.resolvedPath.empty() ? entry.path : entry.resolvedPath;

      library::LibraryLoadResult libResult = library::LibraryLoader::fromFile(path);
      if (!libResult.ok()) {
         for (const auto& e : libResult.errors) {
            result.errors.push_back({entry.id, path, e.toString()});
         }
         continue;
      }

      const library::LibraryDescriptor& descriptor = *libResult.descriptor;

      if (library::LibraryDescriptor::makeKey(descriptor.id) != library::LibraryDescriptor::makeKey(entry.id)) {
         result.errors.push_back({entry.id, path,
            "descriptor id '" + descriptor.id + "' does not match the configured id '" + entry.id + "'"});
         continue;
      }

      if (entry.hasVersion) {
         const library::Version version = library::Version::parse(descriptor.version);
         if (!version.valid) {
            result.errors.push_back({entry.id, path,
               "descriptor declares an invalid version '" + descriptor.version + "'"});
            continue;
         }
         if (!entry.version.matches(version)) {
            result.errors.push_back({entry.id, path,
               "version " + descriptor.version + " of the library does not satisfy constraint '"
                  + entry.version.raw + "'"});
            continue;
         }
      }

      std::string registerError;
      library::LibraryDescriptor copy = descriptor;
      if (!result.registry.registerLibrary(std::move(copy), registerError)) {
         result.errors.push_back({entry.id, path, registerError});
         continue;
      }
   }
   return result;
}

} // namespace st2cpp::project