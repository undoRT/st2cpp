/**
 * @file ScopeManager.h
 * @brief The symbol scope stack used while emitting a POU
 * @details The generator walks nested scopes as it emits: the global scope, one
 * per function block or program, then one per method or nested block. A name has
 * to resolve to the C++ type it was declared with, not to the type of an
 * unrelated symbol of the same name further out, so the lookup walks inward out
 * and stops at the first hit. The manager also carries the AT address of a
 * variable, the per-parameter temporary counter that keeps two output
 * temporaries from colliding, and the base class a `SUPER^` call resolves to.
 *
 * It is intentionally free of the output streams so that both the declaration
 * and the body emitters can share one stack.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Manages the symbol scope stack for code generation.
 *
 * Each scope holds:
 * - variable name → C++ type
 * - variable name → AT address string (if any)
 * - temporary counters for output parameters
 * - base class name for SUPER^ calls
 *
 * Lookup searches from the innermost scope outward.
 */
class ScopeManager
{
public:
   struct VarInfo
   {
      std::string type;
      std::optional<std::string> atAddress;
      bool isFunctionLocal = false;
   };

   // --- Scope lifecycle ---
   void pushScope(); ///< Enter a new scope (POU, method, etc.)
   void popScope();  ///< Leave the current scope

   // --- Variable registration ---
   void addVariable(const std::string& name, const std::string& cppType);
   void addATVariable(const std::string& name, const std::string& atAddress);

   // --- Lookup (innermost first) ---
   std::optional<std::string> lookupVariable(const std::string& name) const;
   std::optional<std::string> lookupATAddress(const std::string& name) const;
   std::optional<VarInfo> lookupVariableInfo(const std::string& name) const;

   // --- Base class for SUPER^ ---
   void setBaseClass(const std::string& base);
   std::string getBaseClass() const;

   // --- Temporary counters (for output parameters) ---
   int getNextTempCounter(const std::string& baseName);

   // --- To analyze function AT Statement variables ---
   void setFunctionScope(bool isFunc);
   bool isFunctionScope() const;
   void setLocalToFunction(bool isLocal);
   bool isLocalToFunction() const;

private:
   struct Scope
   {
      std::unordered_map<std::string, std::string> vars;
      std::unordered_map<std::string, std::string> atAddrs;
      std::unordered_map<std::string, int> tempCounters;
      std::string baseClass;
      bool isFunctionScope = false;
      bool isLocalToFunction = false;
   };

   std::vector<Scope> m_scopes; // index 0 = global scope
};
