/**
 * @file IdentifierPolicy.h
 * @brief The identifier case policy shared by the code generator components
 * @details IEC 61131-3 identifiers are case-insensitive, so by default every
 * identifier written in the generated C++ is folded to uppercase and the
 * declaration and the reference always agree. --caseSensitive turns that off and
 * the spelling written in the ST is preserved instead.
 *
 * The policy is a value, not generator state: every component that has to spell
 * an identifier takes one, so a component can never drift from the spelling the
 * rest of the pipeline uses.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <algorithm>
#include <string>

namespace st2cpp::codegen {

/**
 * @brief How identifiers are spelled in the generated C++.
 */
struct IdentifierPolicy
{
   /// true preserves the original case, false folds every identifier to uppercase
   bool caseSensitive = false;

   /**
    * @brief Apply the policy to an identifier.
    * @param str The identifier as written in the ST source
    * @return The spelling to emit in the generated C++
    */
   std::string apply(const std::string& str) const
   {
      if (caseSensitive) {
         return str;
      }
      std::string result = str;
      std::transform(result.begin(), result.end(), result.begin(), ::toupper);
      return result;
   }

   /// Spelling for a value identifier (a variable, a function, a member).
   std::string ident(const std::string& str) const { return apply(str); }

   /// Spelling for a type name.
   std::string type(const std::string& str) const { return apply(str); }
};

} // namespace st2cpp::codegen
