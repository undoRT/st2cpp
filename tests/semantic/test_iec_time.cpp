/**
 * @file test_iec_time.cpp
 * @brief Unit tests for IEC 61131-3 TIME literal parsing
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "semantic/IecTime.h"

using namespace st2cpp::semantic;

// ============================================================================
// Validity
// ============================================================================

TEST(IecTimeTest, PrefixVariants)
{
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#5s").value_or(-1), 5000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("t#5s").value_or(-1), 5000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("TIME#5s").value_or(-1), 5000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("time#5s").value_or(-1), 5000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("5s").value_or(-1), 5000);
}

TEST(IecTimeTest, Units)
{
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1ms").value_or(-1), 1);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1s").value_or(-1), 1000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1m").value_or(-1), 60000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1h").value_or(-1), 3600000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1d").value_or(-1), 86400000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1D2H").value_or(-1), 93600000);
}

TEST(IecTimeTest, CompositeAndDecimal)
{
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#1d2h3m4s5ms").value_or(-1), 93784005);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#2.5s").value_or(-1), 2500);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#0.001s").value_or(-1), 1);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#100ms").value_or(-1), 100);
}

TEST(IecTimeTest, Sign)
{
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#-30m").value_or(1), -1800000);
   EXPECT_EQ(iecTimeLiteralToMilliseconds("T#+5s").value_or(-1), 5000);
}

// ============================================================================
// Invalid literals
// ============================================================================

TEST(IecTimeTest, MalformedRejected)
{
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T#").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T#garbage").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T#5x").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T#s").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T##5s").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("TIME").has_value());
   EXPECT_FALSE(iecTimeLiteralToMilliseconds("T#5").has_value());
}