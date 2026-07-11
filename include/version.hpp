/**
 * @file version.hpp
 * @brief Version information for st2cpp compiler
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 Salvatore Bamundo
 */

#pragma once

#include <string>

// Version numbers following Semantic Versioning (semver.org)
#define ST2CPP_VERSION_MAJOR  0
#define ST2CPP_VERSION_MINOR  2
#define ST2CPP_VERSION_PATCH  0
#define ST2CPP_VERSION_PREREL ""

// Helper macros for stringification (workaround for MSVC)
#define ST2CPP_STRINGIFY_IMPL(x) #x
#define ST2CPP_STRINGIFY(x)      ST2CPP_STRINGIFY_IMPL(x)

// Version string for display
#define ST2CPP_VERSION_STRING \
   ST2CPP_STRINGIFY(ST2CPP_VERSION_MAJOR) \
   "." ST2CPP_STRINGIFY(ST2CPP_VERSION_MINOR) "." ST2CPP_STRINGIFY(ST2CPP_VERSION_PATCH) ST2CPP_VERSION_PREREL

// Build date (automatically updated by compiler)
#define ST2CPP_BUILD_DATE __DATE__ " " __TIME__

// Compiler information
#ifdef __clang__
#define ST2CPP_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#define ST2CPP_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
// For MSVC, _MSC_VER is a number, need to stringify it
#define ST2CPP_COMPILER "MSVC " ST2CPP_STRINGIFY(_MSC_VER)
#else
#define ST2CPP_COMPILER "Unknown"
#endif

/**
 * @brief Get version as a string
 * @return Version string (e.g., "1.0.0")
 */
inline std::string getVersion()
{
   return ST2CPP_VERSION_STRING;
}

/**
 * @brief Get full version information
 * @return Detailed version string with compiler and build date
 */
inline std::string getFullVersion()
{
   return std::string("st2cpp version ") + ST2CPP_VERSION_STRING + " (" + ST2CPP_COMPILER + ", built " + ST2CPP_BUILD_DATE + ")";
}

/**
 * @brief Get version numbers as tuple
 * @return Struct with major, minor, patch
 */
struct Version
{
   int major = ST2CPP_VERSION_MAJOR;
   int minor = ST2CPP_VERSION_MINOR;
   int patch = ST2CPP_VERSION_PATCH;

   std::string toString() const { return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch); }
};

inline Version getVersionNumbers()
{
   return Version{};
}