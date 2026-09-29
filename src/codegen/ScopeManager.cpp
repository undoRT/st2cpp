/**
 * @file ScopeManager.cpp
 * @brief Implementation of the symbol scope stack
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/ScopeManager.h"

/**
 * @brief Push a new scope onto the stack
 */
void ScopeManager::pushScope()
{
   m_scopes.push_back(Scope{});
}

/**
 * @brief Pop the current scope from the stack (keeps the global scope)
 */
void ScopeManager::popScope()
{
   if (m_scopes.size() > 1) { // keep the global scope
      m_scopes.pop_back();
   }
}

/**
 * @brief Add a variable to the current scope
 *
 * @param name The variable name
 * @param cppType The C++ type of the variable
 */
void ScopeManager::addVariable(const std::string& name, const std::string& cppType)
{
   if (!m_scopes.empty()) {
      m_scopes.back().vars[name] = cppType;
   }
}

/**
 * @brief Add an AT address variable to the current scope
 *
 * @param name The variable name
 * @param atAddress The AT address string
 */
void ScopeManager::addATVariable(const std::string& name, const std::string& atAddress)
{
   if (!m_scopes.empty()) {
      m_scopes.back().atAddrs[name] = atAddress;
   }
}

/**
 * @brief Look up a variable in the scope stack (innermost first)
 *
 * @param name The variable name
 * @return std::optional<std::string> The C++ type if found, otherwise std::nullopt
 */
std::optional<std::string> ScopeManager::lookupVariable(const std::string& name) const
{
   for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
      auto found = it->vars.find(name);
      if (found != it->vars.end()) {
         return found->second;
      }
   }
   return std::nullopt;
}

/**
 * @brief Look up an AT address variable in the scope stack (innermost first)
 *
 * @param name The variable name
 * @return std::optional<std::string> The AT address if found, otherwise std::nullopt
 */
std::optional<std::string> ScopeManager::lookupATAddress(const std::string& name) const
{
   for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
      auto found = it->atAddrs.find(name);
      if (found != it->atAddrs.end()) {
         return found->second;
      }
   }
   return std::nullopt;
}

/**
 * @brief Look up info for a variable
 *
 * @param name The variable name
 * @return std::optional<ScopeManager::VarInfo> The AT VarInfo if found, otherwise std::nullopt
 */
std::optional<ScopeManager::VarInfo> ScopeManager::lookupVariableInfo(const std::string& name) const
{
   for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
      auto varIt = it->vars.find(name);
      if (varIt != it->vars.end()) {
         VarInfo info;
         info.type = varIt->second;
         info.isFunctionLocal = it->isLocalToFunction;
         auto atIt = it->atAddrs.find(name);
         if (atIt != it->atAddrs.end()) {
            info.atAddress = atIt->second;
         }
         return info;
      }
   }
   return std::nullopt;
}

/**
 * @brief Set the base class name for the current scope (for SUPER^)
 *
 * @param base The base class name
 */
void ScopeManager::setBaseClass(const std::string& base)
{
   if (!m_scopes.empty()) {
      m_scopes.back().baseClass = base;
   }
}

/**
 * @brief Get the base class name from the current scope
 *
 * @return std::string The base class name, or empty string if not set
 */
std::string ScopeManager::getBaseClass() const
{
   return m_scopes.empty() ? "" : m_scopes.back().baseClass;
}

/**
 * @brief Get the next temporary counter value for a given base name
 *
 * @param baseName The base name for the temporary variable
 * @return int The next counter value
 */
int ScopeManager::getNextTempCounter(const std::string& baseName)
{
   if (m_scopes.empty()) {
      return 0;
   }
   auto& counters = m_scopes.back().tempCounters;
   return ++counters[baseName];
}

/**
 * @brief Set scoper for function/method
 * 
 * @param isFunc bool to indicate if it is a function or not
 */
void ScopeManager::setFunctionScope(bool isFunc)
{
   if (!m_scopes.empty()) {
      m_scopes.back().isFunctionScope = isFunc;
   }
}

/**
 * @brief Check method to understand if it is a function scope
 * 
 * @return true if it is a function scope, false otherwise
 */
bool ScopeManager::isFunctionScope() const
{
   return m_scopes.empty() ? false : m_scopes.back().isFunctionScope;
}

/**
 * @brief Set scoper local for function/method
 * 
 * @param isFunc bool to indicate if it is local to function or not
 */
void ScopeManager::setLocalToFunction(bool isLocal)
{
   if (!m_scopes.empty()) {
      m_scopes.back().isLocalToFunction = isLocal;
   }
}

/**
 * @brief Check method to understand if it is a local to function scope
 * 
 * @return true if it is local tofunction scope, false otherwise
 */
bool ScopeManager::isLocalToFunction() const
{
   return m_scopes.empty() ? false : m_scopes.back().isLocalToFunction;
}
