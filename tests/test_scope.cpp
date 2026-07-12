/**
 * @file test_scope.cpp
 * @brief Implementation of test cases for the ScopeManager
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "codegen/CodeGenerator.h"

// ============================================================================
//  Scope Lifecycle Tests
// ============================================================================

/**
 * @brief Test pushing and popping scopes with variable shadowing
 */
TEST(ScopeManagerTest, PushPopScope)
{
   ScopeManager sm;
   sm.pushScope();
   sm.addVariable("x", "Int16");
   sm.addATVariable("y", "%IX0.0");
   EXPECT_EQ(sm.lookupVariable("x"), std::optional<std::string>("Int16"));
   EXPECT_EQ(sm.lookupATAddress("y"), std::optional<std::string>("%IX0.0"));

   sm.pushScope();
   sm.addVariable("x", "Bool"); // shadow
   EXPECT_EQ(sm.lookupVariable("x"), std::optional<std::string>("Bool"));
   sm.popScope();
   EXPECT_EQ(sm.lookupVariable("x"), std::optional<std::string>("Int16"));
}

/**
 * @brief Test that variable lookup works across multiple scopes
 */
TEST(ScopeManagerTest, LookupAcrossScopes)
{
   ScopeManager sm;

   // Global scope
   sm.pushScope();
   sm.addVariable("global_var", "Int32");
   sm.addATVariable("global_at", "%MW10");

   // Inner scope
   sm.pushScope();
   sm.addVariable("local_var", "Bool");
   sm.addATVariable("local_at", "%IX0.0");

   // Lookup from inner scope should find both local and global
   EXPECT_EQ(sm.lookupVariable("local_var"), std::optional<std::string>("Bool"));
   EXPECT_EQ(sm.lookupVariable("global_var"), std::optional<std::string>("Int32"));
   EXPECT_EQ(sm.lookupATAddress("local_at"), std::optional<std::string>("%IX0.0"));
   EXPECT_EQ(sm.lookupATAddress("global_at"), std::optional<std::string>("%MW10"));

   sm.popScope(); // pop inner

   // After popping inner, local variables should not be found
   EXPECT_EQ(sm.lookupVariable("local_var"), std::nullopt);
   EXPECT_EQ(sm.lookupATAddress("local_at"), std::nullopt);
   EXPECT_EQ(sm.lookupVariable("global_var"), std::optional<std::string>("Int32"));

   sm.popScope(); // pop global
}

/**
 * @brief Test that global scope persists after popping all scopes
 */
TEST(ScopeManagerTest, GlobalScopePersists)
{
   ScopeManager sm;

   // Global scope
   sm.pushScope();
   sm.addVariable("persistent", "Float");

   // Push and pop several scopes
   for (int i = 0; i < 5; ++i) {
      sm.pushScope();
      sm.addVariable("temp_" + std::to_string(i), "Int16");
      sm.popScope();
   }

   // Global variable should still be accessible
   EXPECT_EQ(sm.lookupVariable("persistent"), std::optional<std::string>("Float"));

   // Temporary variables should not be accessible
   EXPECT_EQ(sm.lookupVariable("temp_0"), std::nullopt);
   EXPECT_EQ(sm.lookupVariable("temp_4"), std::nullopt);
}

// ============================================================================
//  Function Local Flag Tests
// ============================================================================

/**
 * @brief Test setting and checking the function-local flag
 */
TEST(ScopeManagerTest, FunctionLocalFlag)
{
   ScopeManager sm;
   sm.pushScope();
   sm.setLocalToFunction(true);
   sm.addVariable("local", "Int16");
   sm.addATVariable("local", "%IW0");

   auto info = sm.lookupVariableInfo("local");
   ASSERT_TRUE(info.has_value());
   EXPECT_TRUE(info->isFunctionLocal);
   EXPECT_EQ(info->type, "Int16");
   EXPECT_EQ(info->atAddress, "%IW0");
}

/**
 * @brief Test that function-local flag is properly reset after popping scope
 */
TEST(ScopeManagerTest, FunctionLocalFlagReset)
{
   ScopeManager sm;

   sm.pushScope();
   sm.setLocalToFunction(true);
   EXPECT_TRUE(sm.isLocalToFunction());
   sm.popScope();

   // After popping, the flag should be false (default)
   sm.pushScope();
   EXPECT_FALSE(sm.isLocalToFunction());
   sm.popScope();
}

/**
 * @brief Test function-local flag with nested scopes
 */
TEST(ScopeManagerTest, FunctionLocalFlagNested)
{
   ScopeManager sm;

   sm.pushScope();
   sm.setLocalToFunction(true);
   EXPECT_TRUE(sm.isLocalToFunction());

   sm.pushScope();
   // Inner scope does NOT inherit the flag from parent
   EXPECT_FALSE(sm.isLocalToFunction());

   // Set it explicitly in inner scope
   sm.setLocalToFunction(true);
   EXPECT_TRUE(sm.isLocalToFunction());

   sm.popScope();
   // Back to parent scope, flag is still true
   EXPECT_TRUE(sm.isLocalToFunction());

   sm.popScope();
}

// ============================================================================
//  Function Scope Tests
// ============================================================================

/**
 * @brief Test setting and checking the function scope flag
 */
TEST(ScopeManagerTest, FunctionScopeFlag)
{
   ScopeManager sm;
   sm.pushScope();
   sm.setFunctionScope(true);

   EXPECT_TRUE(sm.isFunctionScope());

   sm.setFunctionScope(false);
   EXPECT_FALSE(sm.isFunctionScope());

   sm.popScope();
}

/**
 * @brief Test function scope with variable lookup
 */
TEST(ScopeManagerTest, FunctionScopeVariableLookup)
{
   ScopeManager sm;

   sm.pushScope();
   sm.setFunctionScope(true);
   sm.addVariable("func_var", "Int32");
   sm.addATVariable("func_at", "%IW100");

   // Variables should be accessible even with function scope
   EXPECT_EQ(sm.lookupVariable("func_var"), std::optional<std::string>("Int32"));
   EXPECT_EQ(sm.lookupATAddress("func_at"), std::optional<std::string>("%IW100"));

   // Check that function scope is set
   EXPECT_TRUE(sm.isFunctionScope());

   sm.popScope();
}

// ============================================================================
//  Temporary Counter Tests
// ============================================================================

/**
 * @brief Test temporary counter generation
 */
TEST(ScopeManagerTest, TempCounter)
{
   ScopeManager sm;
   sm.pushScope();

   int count1 = sm.getNextTempCounter("temp");
   int count2 = sm.getNextTempCounter("temp");
   int count3 = sm.getNextTempCounter("other");

   EXPECT_EQ(count1, 1);
   EXPECT_EQ(count2, 2);
   EXPECT_EQ(count3, 1);

   sm.popScope();
}

/**
 * @brief Test that temp counters are reset when scope is popped
 */
TEST(ScopeManagerTest, TempCounterReset)
{
   ScopeManager sm;

   sm.pushScope();
   int count1 = sm.getNextTempCounter("temp");
   EXPECT_EQ(count1, 1);
   sm.popScope();

   sm.pushScope();
   int count2 = sm.getNextTempCounter("temp");
   EXPECT_EQ(count2, 1); // Should reset to 1 in new scope
   sm.popScope();
}

/**
 * @brief Test multiple temp counters in nested scopes
 */
TEST(ScopeManagerTest, TempCounterNested)
{
   ScopeManager sm;

   sm.pushScope();
   int count1 = sm.getNextTempCounter("x");
   int count2 = sm.getNextTempCounter("y");
   EXPECT_EQ(count1, 1);
   EXPECT_EQ(count2, 1);

   sm.pushScope();
   int count3 = sm.getNextTempCounter("x");
   int count4 = sm.getNextTempCounter("y");
   EXPECT_EQ(count3, 1); // Reset in inner scope
   EXPECT_EQ(count4, 1);
   sm.popScope();

   int count5 = sm.getNextTempCounter("x");
   EXPECT_EQ(count5, 2); // Continues from outer scope
   sm.popScope();
}

// ============================================================================
//  Base Class Tests (for SUPER^)
// ============================================================================

/**
 * @brief Test setting and getting base class
 */
TEST(ScopeManagerTest, BaseClass)
{
   ScopeManager sm;
   sm.pushScope();

   sm.setBaseClass("FB_Device");
   EXPECT_EQ(sm.getBaseClass(), "FB_Device");

   sm.popScope();
}

/**
 * @brief Test base class in nested scopes
 * 
 * NOTE: The ScopeManager does NOT automatically inherit base class
 * from parent scopes. Each scope must set it explicitly.
 */
TEST(ScopeManagerTest, BaseClassNested)
{
   ScopeManager sm;

   sm.pushScope();
   sm.setBaseClass("FB_Base");
   EXPECT_EQ(sm.getBaseClass(), "FB_Base");

   sm.pushScope();
   // Inner scope does NOT inherit base class from parent
   // It starts empty by default
   EXPECT_EQ(sm.getBaseClass(), "");

   // Set base class explicitly in inner scope
   sm.setBaseClass("FB_Derived");
   EXPECT_EQ(sm.getBaseClass(), "FB_Derived");

   sm.popScope();
   // Back to parent scope, base class is restored
   EXPECT_EQ(sm.getBaseClass(), "FB_Base");

   sm.popScope();
}

/**
 * @brief Test that base class is empty by default
 */
TEST(ScopeManagerTest, BaseClassEmpty)
{
   ScopeManager sm;
   sm.pushScope();
   EXPECT_EQ(sm.getBaseClass(), "");
   sm.popScope();
}

// ============================================================================
//  Variable Info Tests
// ============================================================================

/**
 * @brief Test lookupVariableInfo with complete information
 */
TEST(ScopeManagerTest, VariableInfoComplete)
{
   ScopeManager sm;
   sm.pushScope();

   sm.addVariable("var1", "Int16");
   sm.addATVariable("var1", "%IX0.0");
   sm.setLocalToFunction(true);

   auto info = sm.lookupVariableInfo("var1");
   ASSERT_TRUE(info.has_value());
   EXPECT_EQ(info->type, "Int16");
   EXPECT_EQ(info->atAddress, "%IX0.0");
   EXPECT_TRUE(info->isFunctionLocal);

   sm.popScope();
}

/**
 * @brief Test lookupVariableInfo for variable without AT address
 */
TEST(ScopeManagerTest, VariableInfoWithoutAT)
{
   ScopeManager sm;
   sm.pushScope();

   sm.addVariable("plain_var", "Float");
   sm.setLocalToFunction(false);

   auto info = sm.lookupVariableInfo("plain_var");
   ASSERT_TRUE(info.has_value());
   EXPECT_EQ(info->type, "Float");
   EXPECT_FALSE(info->atAddress.has_value());
   EXPECT_FALSE(info->isFunctionLocal);

   sm.popScope();
}

/**
 * @brief Test lookupVariableInfo for non-existent variable
 */
TEST(ScopeManagerTest, VariableInfoNonExistent)
{
   ScopeManager sm;
   sm.pushScope();

   sm.addVariable("existing", "Int32");

   auto info = sm.lookupVariableInfo("non_existing");
   EXPECT_FALSE(info.has_value());

   sm.popScope();
}

// ============================================================================
//  Edge Cases Tests
// ============================================================================

/**
 * @brief Test that lookups return nullopt for empty scope
 */
TEST(ScopeManagerTest, LookupEmptyScope)
{
   ScopeManager sm;
   sm.pushScope();

   EXPECT_EQ(sm.lookupVariable("anything"), std::nullopt);
   EXPECT_EQ(sm.lookupATAddress("anything"), std::nullopt);

   sm.popScope();
}

/**
 * @brief Test that popping global scope doesn't remove it
 */
TEST(ScopeManagerTest, PopGlobalScope)
{
   ScopeManager sm;

   // Global scope is always present
   sm.pushScope();
   sm.addVariable("global_var", "Int32");

   sm.popScope(); // This would pop the global scope, but it's kept

   // Global variable should still exist
   // Note: The ScopeManager keeps at least one scope (the global one)
   sm.pushScope();
   EXPECT_EQ(sm.lookupVariable("global_var"), std::optional<std::string>("Int32"));
   sm.popScope();
}

/**
 * @brief Test multiple AT addresses for the same variable in different scopes
 */
TEST(ScopeManagerTest, MultipleATAddresses)
{
   ScopeManager sm;

   sm.pushScope(); // global
   sm.addATVariable("shared", "%MW10");
   EXPECT_EQ(sm.lookupATAddress("shared"), std::optional<std::string>("%MW10"));

   sm.pushScope(); // inner
   sm.addATVariable("shared", "%IX0.0");
   // Inner scope shadows the global one
   EXPECT_EQ(sm.lookupATAddress("shared"), std::optional<std::string>("%IX0.0"));
   sm.popScope();

   // Back to global scope
   EXPECT_EQ(sm.lookupATAddress("shared"), std::optional<std::string>("%MW10"));

   sm.popScope();
}