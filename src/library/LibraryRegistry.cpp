/**
 * @file LibraryRegistry.cpp
 * @brief LibraryRegistry implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "library/LibraryRegistry.h"
#include <algorithm>

namespace st2cpp::library {

bool LibraryRegistry::registerLibrary(LibraryDescriptor descriptor, std::string& error)
{
   const std::string key = LibraryDescriptor::makeKey(descriptor.id);
   if (descriptor.id.empty()) {
      error = "cannot register a library with an empty id";
      return false;
   }
   if (byId_.find(key) != byId_.end()) {
      error = "library '" + descriptor.id + "' is already registered";
      return false;
   }
   byId_.emplace(key, std::move(descriptor));
   return true;
}

bool LibraryRegistry::registerLibrary(LibraryDescriptor descriptor)
{
   std::string ignored;
   return registerLibrary(std::move(descriptor), ignored);
}

const LibraryDescriptor* LibraryRegistry::get(const std::string& id) const
{
   const std::string key = LibraryDescriptor::makeKey(id);
   const auto it = byId_.find(key);
   return it == byId_.end() ? nullptr : &it->second;
}

bool LibraryRegistry::contains(const std::string& id) const
{
   return get(id) != nullptr;
}

bool LibraryRegistry::remove(const std::string& id)
{
   const std::string key = LibraryDescriptor::makeKey(id);
   return byId_.erase(key) != 0;
}

std::vector<std::string> LibraryRegistry::ids() const
{
   std::vector<std::string> result;
   result.reserve(byId_.size());
   for (const auto& entry : byId_) {
      result.push_back(entry.second.id);
   }
   return result;
}

std::vector<const LibraryDescriptor*> LibraryRegistry::all() const
{
   std::vector<const LibraryDescriptor*> result;
   result.reserve(byId_.size());
   for (const auto& entry : byId_) {
      result.push_back(&entry.second);
   }
   return result;
}

std::vector<const LibraryDescriptor*> LibraryRegistry::allOrdered() const
{
   std::vector<const LibraryDescriptor*> result = all();
   std::sort(result.begin(), result.end(), [](const LibraryDescriptor* a, const LibraryDescriptor* b) {
      return LibraryDescriptor::makeKey(a->id) < LibraryDescriptor::makeKey(b->id);
   });
   return result;
}

std::vector<std::string> LibraryRegistry::missingDependencies(const LibraryDescriptor& lib) const
{
   std::vector<std::string> missing;
   for (const Dependency& dep : lib.dependencies) {
      if (!contains(dep.id)) {
         missing.push_back(dep.id);
      }
   }
   return missing;
}

} // namespace st2cpp::library