/**
 * @file IecTime.h
 * @brief IEC 61131-3 TIME literal parsing helpers
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

namespace st2cpp::semantic {

/**
 * @brief Parse an IEC 61131-3 TIME literal into its total value in milliseconds.
 * @details Accepts the forms produced by the lexer: an optional `T#`, `t#`,
 * `TIME#` or `time#` prefix followed by one or more `<value><unit>`
 * concatenated segments (IEC short form), e.g. `T#5s`, `TIME#100ms`,
 * `T#1d2h3m4s5ms`, `T#2.5s`, `T#-30m`. The prefix is optional for leniency.
 * Units: d (day), h (hour), m (minute), s (second), ms (millisecond),
 * matched case-insensitively.
 * @param text The TIME literal text as tokenized by the lexer
 * @return The total value in milliseconds, or std::nullopt when the literal
 * is malformed (empty body, unknown unit, missing number, stray characters).
 */
inline std::optional<int64_t> iecTimeLiteralToMilliseconds(const std::string& text)
{
   size_t i = 0;
   const size_t n = text.size();

   // Strip an optional T# / TIME# prefix (case-insensitive).
   const auto hasPrefix = [&](const char* p) {
      size_t len = 0;
      while (p[len] != '\0') {
         ++len;
      }
      if (i + len > n) {
         return false;
      }
      for (size_t k = 0; k < len; ++k) {
         if (std::tolower(static_cast<unsigned char>(text[i + k]))
             != std::tolower(static_cast<unsigned char>(p[k]))) {
            return false;
         }
      }
      i += len;
      return true;
   };
   if (hasPrefix("TIME#")) {
      // already consumed
   } else if (hasPrefix("T#")) {
      // already consumed
   } else {
      i = 0; // no prefix: parse from the start
   }

   if (i >= n) {
      return std::nullopt; // empty body
   }

   bool negative = false;
   if (text[i] == '+' || text[i] == '-') {
      negative = text[i] == '-';
      ++i;
   }

   long double totalMs = 0.0L;
   bool anyPart = false;

   while (i < n) {
      // A numeric value (integer or decimal).
      const size_t numStart = i;
      while (i < n && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
         ++i;
      }
      if (i == numStart) {
         return std::nullopt; // missing number
      }
      const std::string numStr = text.substr(numStart, i - numStart);
      char* endp = nullptr;
      const long double value = std::strtold(numStr.c_str(), &endp);
      if (endp != numStr.c_str() + numStr.size()) {
         return std::nullopt;
      }

      // The mandatory unit.
      const size_t unitStart = i;
      while (i < n && std::isalpha(static_cast<unsigned char>(text[i]))) {
         ++i;
      }
      if (i == unitStart) {
         return std::nullopt; // missing unit
      }
      const std::string unit = text.substr(unitStart, i - unitStart);

      long double divisor = 0.0L;
      if (unit == "ms" || unit == "mS" || unit == "Ms" || unit == "MS") {
         divisor = 1.0L;
      } else if (unit == "s" || unit == "S") {
         divisor = 1000.0L;
      } else if (unit == "m" || unit == "M") {
         divisor = 60000.0L;
      } else if (unit == "h" || unit == "H") {
         divisor = 3600000.0L;
      } else if (unit == "d" || unit == "D") {
         divisor = 86400000.0L;
      } else {
         return std::nullopt; // unknown unit
      }

      totalMs += value * divisor;
      anyPart = true;
   }

   if (!anyPart) {
      return std::nullopt;
   }
   long double result = negative ? -totalMs : totalMs;
   // Round to the nearest millisecond instead of truncating (0.001s -> 1ms).
   if (result > 0.0L) {
      result += 0.5L;
   } else if (result < 0.0L) {
      result -= 0.5L;
   }
   return static_cast<int64_t>(result);
}

} // namespace st2cpp::semantic