/**
 * @file LibraryDescriptor.cpp
 * @brief LibraryDescriptor model implementation: versions, lookups
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "library/LibraryDescriptor.h"
#include <cctype>
#include <charconv>

namespace st2cpp::library {

namespace {

/**
 * @brief Split a string on the first occurrence of a separator.
 * @param s The input string
 * @param sep The separator character
 * @param head Receives the substring before the separator
 * @param tail Receives the substring after the separator (may be empty)
 * @return true when the separator was found
 */
bool splitOn(const std::string& s, char sep, std::string& head, std::string& tail)
{
   // Copy first: s may alias head/tail (e.g. splitOn(minor,'.',minor,patch)),
   // so all substr() calls must operate on the original content.
   const std::string copy = s;
   const size_t pos = copy.find(sep);
   if (pos == std::string::npos) {
      head = copy;
      tail.clear();
      return false;
   }
   head = copy.substr(0, pos);
   tail = copy.substr(pos + 1);
   return true;
}

/**
 * @brief Parse a plain non-negative decimal integer.
 */
bool parseDecimal(const std::string& text, int& out)
{
   if (text.empty()) {
      return false;
   }
   int value = 0;
   const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
   if (result.ec != std::errc() || result.ptr != text.data() + text.size() || value < 0) {
      return false;
   }
   out = value;
   return true;
}

/**
 * @brief Parse a version segment that may be a numeric identifier or a
 * wildcard ('x', 'X' or '*').
 */
bool parseVersionSegment(const std::string& segment, int& numeric, bool& wildcard)
{
   if (segment.empty()) {
      return false;
   }
   if (segment == "x" || segment == "X" || segment == "*") {
      wildcard = true;
      numeric = -1;
      return true;
   }
   wildcard = false;
   return parseDecimal(segment, numeric);
}

/**
 * @brief Parse MAJOR.MINOR.PATCH with optional '-prerelease' and '+build',
 * allowing wildcard segments (e.g. "1.x", "1.2.x").
 * @param input The version text
 * @param v Receives the parsed version
 * @param wildcard Set when any segment used a wildcard
 * @return false on malformed input
 */
bool parseVersionText(const std::string& input, Version& v, bool& wildcard)
{
   v = Version{};
   wildcard = false;

   std::string core = input;
   std::string build;
   splitOn(core, '+', core, build);
   v.build = build;

   std::string prerelease;
   splitOn(core, '-', core, prerelease);
   v.prerelease = prerelease;

   std::string major, minor, patch;
   if (!splitOn(core, '.', major, minor) || !splitOn(minor, '.', minor, patch) || patch.empty()) {
      return false;
   }

   bool wc = false;
   int m = 0;
   if (!parseVersionSegment(major, m, wc)) {
      return false;
   }
   v.major = m;
   wildcard = wildcard || wc;
   int mi = 0;
   wc = false;
   if (!parseVersionSegment(minor, mi, wc)) {
      return false;
   }
   v.minor = mi;
   wildcard = wildcard || wc;
   int p = 0;
   wc = false;
   if (!parseVersionSegment(patch, p, wc)) {
      return false;
   }
   v.patch = p;
   wildcard = wildcard || wc;

   // Wildcards and prerelease/build metadata do not mix well; reject.
   if (wildcard && (!v.prerelease.empty() || !v.build.empty())) {
      return false;
   }
   return true;
}

/**
 * @brief Parse a single constraint clause (e.g. "^1.0.0", ">=1.2", "1.x").
 */
bool parseClause(const std::string& text, VersionClause& clause)
{
   VersionOp op = VersionOp::Exact;
   std::string rest = text;
   if (text.rfind(">=", 0) == 0) {
      op = VersionOp::GreaterOrEqual;
      rest = text.substr(2);
   } else if (text.rfind("<=", 0) == 0) {
      op = VersionOp::LessOrEqual;
      rest = text.substr(2);
   } else if (text.rfind(">", 0) == 0) {
      op = VersionOp::Greater;
      rest = text.substr(1);
   } else if (text.rfind("<", 0) == 0) {
      op = VersionOp::Less;
      rest = text.substr(1);
   } else if (text.rfind("^", 0) == 0) {
      op = VersionOp::Caret;
      rest = text.substr(1);
   } else if (text.rfind("~", 0) == 0) {
      op = VersionOp::Tilde;
      rest = text.substr(1);
   } else if (!text.empty() && text[0] == '=') {
      op = VersionOp::Exact;
      rest = text.substr(1);
   }
   // detect a stray operator prefix we do not recognize (e.g. "@1.0.0")
   if (!rest.empty() && (isalpha(static_cast<unsigned char>(rest[0])) || rest[0] == '@')) {
      return false;
   }
   if (rest.empty()) {
      return false;
   }

   Version v;
   bool wildcard = false;
   if (!parseVersionText(rest, v, wildcard)) {
      return false;
   }
   if (wildcard) {
      clause.op = VersionOp::Wildcard;
   } else {
      clause.op = op;
   }
   clause.version = v;
   clause.valid = true;
   return true;
}

} // namespace

int Version::compare(const Version& other) const
{
   if (major != other.major) {
      return major < other.major ? -1 : 1;
   }
   if (minor != other.minor) {
      return minor < other.minor ? -1 : 1;
   }
   if (patch != other.patch) {
      return patch < other.patch ? -1 : 1;
   }
   const bool thisPre = !prerelease.empty();
   const bool otherPre = !other.prerelease.empty();
   if (thisPre != otherPre) {
      return thisPre ? -1 : 1; // a prerelease is lower than the release
   }
   if (thisPre && prerelease != other.prerelease) {
      return prerelease < other.prerelease ? -1 : 1;
   }
   return 0;
}

namespace {

/**
 * @brief Evaluate a single clause against a concrete version.
 * @details Wildcard clauses lock the non-wildcard components; caret locks the
 * left-most non-zero segment (>= base, < next); tilde locks major+minor;
 * relational operators compare numerically. Prerelease versions compare lower
 * than the same release (see Version::compare).
 */
bool clauseMatches(const VersionClause& clause, const Version& version)
{
   const Version& target = clause.version;
   const bool allWild = target.major < 0 && target.minor < 0 && target.patch < 0;
   switch (clause.op) {
   case VersionOp::Wildcard: {
      if (allWild) {
         return true; // "*"
      }
      if (target.major >= 0 && target.major != version.major) {
         return false;
      }
      if (target.minor >= 0 && target.minor != version.minor) {
         return false;
      }
      if (target.patch >= 0 && target.patch != version.patch) {
         return false;
      }
      return true;
   }
   case VersionOp::Exact:
      return target.compare(version) == 0;
   case VersionOp::Caret: {
      if (target.compare(version) > 0) {
         return false; // >= base
      }
      if (target.major == 0) {
         if (target.minor == 0) {
            // ^0.0.z : major and minor locked, patch >= base
            return version.major == 0 && version.minor == 0;
         }
         // ^0.y.z : major locked, minor locked, patch >= base
         return version.major == 0 && version.minor == target.minor;
      }
      // ^x.y.z : major locked, minor/patch >= base
      return version.major == target.major;
   }
   case VersionOp::Tilde:
      return target.compare(version) <= 0 && version.major == target.major && version.minor == target.minor;
   case VersionOp::GreaterOrEqual:
      return target.compare(version) <= 0;
   case VersionOp::Greater:
      return target.compare(version) < 0;
   case VersionOp::LessOrEqual:
      return target.compare(version) >= 0;
   case VersionOp::Less:
      return target.compare(version) > 0;
   }
   return false;
}

} // namespace

std::string Version::toString() const
{
   if (major < 0 || minor < 0 || patch < 0) {
      return "x";
   }
   std::string result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
   if (!prerelease.empty()) {
      result += "-" + prerelease;
   }
   if (!build.empty()) {
      result += "+" + build;
   }
   return result;
}

Version Version::parse(const std::string& text)
{
   Version v;
   bool wildcard = false;
   if (text.empty() || !parseVersionText(text, v, wildcard) || wildcard) {
      return Version{};
   }
   v.valid = true;
   return v;
}

VersionConstraint VersionConstraint::parse(const std::string& text)
{
   VersionConstraint constraint;
   constraint.raw = text;
   if (text.empty()) {
      return constraint;
   }
   if (text == "*" || text == "x" || text == "X") {
      VersionClause clause;
      clause.op = VersionOp::Wildcard;
      constraint.clauses.push_back(std::move(clause));
      constraint.valid = true;
      return constraint;
   }

   // Split the expression on whitespace into clauses
   bool ok = true;
   std::string clauseText;
   for (size_t i = 0; i <= text.size(); ++i) {
      const bool atEnd = (i == text.size());
      const char c = atEnd ? ' ' : text[i];
      if (c == ' ' || c == '\t') {
         if (!clauseText.empty()) {
            VersionClause clause;
            if (!parseClause(clauseText, clause)) {
               ok = false;
            } else {
               constraint.clauses.push_back(std::move(clause));
            }
            clauseText.clear();
         }
      } else {
         clauseText.push_back(c);
      }
   }
   if (ok && !constraint.clauses.empty()) {
      constraint.valid = true;
   }
   return constraint;
}

bool VersionConstraint::matches(const Version& version) const
{
   if (!valid || !version.valid) {
      return false;
   }
   for (const auto& clause : clauses) {
      if (!clauseMatches(clause, version)) {
         return false;
      }
   }
   return true;
}

std::string LibraryDescriptor::makeKey(const std::string& name)
{
   std::string key;
   key.reserve(name.size());
   for (unsigned char c : name) {
      key.push_back(static_cast<char>(std::toupper(c)));
   }
   return key;
}

const Constant* LibraryDescriptor::findConstant(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& c : constants) {
      if (makeKey(c.name) == key) {
         return &c;
      }
   }
   return nullptr;
}

const EnumTypeDef* LibraryDescriptor::findEnum(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& e : enums) {
      if (makeKey(e.name) == key) {
         return &e;
      }
   }
   return nullptr;
}

const StructTypeDef* LibraryDescriptor::findStruct(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& t : types) {
      if (makeKey(t.name) == key) {
         return &t;
      }
   }
   return nullptr;
}

const GlobalVariable* LibraryDescriptor::findGlobalVariable(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& g : globalVariables) {
      if (makeKey(g.name) == key) {
         return &g;
      }
   }
   return nullptr;
}

const FunctionDef* LibraryDescriptor::findFunction(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& f : functions) {
      if (makeKey(f.name) == key) {
         return &f;
      }
   }
   return nullptr;
}

const FunctionBlockDef* LibraryDescriptor::findFunctionBlock(const std::string& name) const
{
   const std::string key = makeKey(name);
   for (const auto& fb : functionBlocks) {
      if (makeKey(fb.name) == key) {
         return &fb;
      }
   }
   return nullptr;
}

const Dependency* LibraryDescriptor::findDependency(const std::string& id) const
{
   const std::string key = makeKey(id);
   for (const auto& d : dependencies) {
      if (makeKey(d.id) == key) {
         return &d;
      }
   }
   return nullptr;
}

} // namespace st2cpp::library