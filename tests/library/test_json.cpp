/**
 * @file test_json.cpp
 * @brief Tests for the minimal JSON module (st2cpp::json)
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <optional>
#include "json/JsonValue.h"

using namespace st2cpp::json;

// ============================================================================
// Parsing basics
// ============================================================================

TEST(Json, ParseNull)
{
   JsonValue v = parse("null");
   EXPECT_EQ(v.type, JsonType::Null);
}

TEST(Json, ParseBool)
{
   EXPECT_EQ(parse("true").type, JsonType::Bool);
   EXPECT_EQ(parse("true").boolean, true);
   EXPECT_EQ(parse("false").boolean, false);
}

TEST(Json, ParseIntegerNumber)
{
   JsonValue v = parse("42");
   ASSERT_EQ(v.type, JsonType::Number);
   EXPECT_EQ(v.asInt64(), std::optional<int64_t>(42));
   EXPECT_EQ(v.numberRaw, "42");
}

TEST(Json, ParseFloatNumber)
{
   JsonValue v = parse("3.25");
   ASSERT_EQ(v.type, JsonType::Number);
   EXPECT_DOUBLE_EQ(v.number, 3.25);
   EXPECT_EQ(v.numberRaw, "3.25");
   EXPECT_EQ(v.asInt64(), std::nullopt);
}

TEST(Json, ParseExponentNumber)
{
   JsonValue v = parse("-1.5e3");
   ASSERT_EQ(v.type, JsonType::Number);
   EXPECT_DOUBLE_EQ(v.number, -1500.0);
}

TEST(Json, ParseString)
{
   JsonValue v = parse("\"hello\"");
   ASSERT_EQ(v.type, JsonType::String);
   EXPECT_EQ(v.text, "hello");
}

TEST(Json, ParseStringEscapes)
{
   JsonValue v = parse(R"("a\"b\\c\/d\b\f\n\r\t")");
   ASSERT_EQ(v.type, JsonType::String);
   EXPECT_EQ(v.text, "a\"b\\c/d\b\f\n\r\t");
}

TEST(Json, ParseStringUnicode)
{
   JsonValue v = parse("\"caf\\u00e9\"");
   ASSERT_EQ(v.type, JsonType::String);
   EXPECT_EQ(v.text, "caf\xC3\xA9");
}

TEST(Json, ParseEmptyArray)
{
   JsonValue v = parse("[]");
   ASSERT_EQ(v.type, JsonType::Array);
   EXPECT_TRUE(v.array.empty());
}

TEST(Json, ParseArrayMixed)
{
   JsonValue v = parse("[1, \"two\", true, null, 2.5]");
   ASSERT_EQ(v.type, JsonType::Array);
   ASSERT_EQ(v.array.size(), 5u);
   EXPECT_EQ(v.array[1].text, "two");
   EXPECT_EQ(v.array[2].boolean, true);
   EXPECT_EQ(v.array[3].type, JsonType::Null);
}

TEST(Json, ParseObject)
{
   JsonValue v = parse("{\"id\": \"examplelib\", \"version\": 1}");
   ASSERT_EQ(v.type, JsonType::Object);
   EXPECT_EQ(v.members.size(), 2u);
   const JsonValue* id = v.find("id");
   ASSERT_NE(id, nullptr);
   EXPECT_EQ(id->text, "examplelib");
   const JsonValue* version = v.find("version");
   ASSERT_NE(version, nullptr);
   EXPECT_EQ(version->asInt64(), std::optional<int64_t>(1));
   EXPECT_EQ(v.find("missing"), nullptr);
}

TEST(Json, ParseNested)
{
   JsonValue v = parse("{\"outer\": {\"inner\": [1, {\"x\": 2}]}}");
   ASSERT_EQ(v.type, JsonType::Object);
   const JsonValue* outer = v.find("outer");
   ASSERT_NE(outer, nullptr);
   const JsonValue* inner = outer->find("inner");
   ASSERT_NE(inner, nullptr);
   ASSERT_EQ(inner->array.size(), 2u);
   EXPECT_EQ(inner->array[1].find("x")->asInt64(), std::optional<int64_t>(2));
}

TEST(Json, ParseWhitespace)
{
   JsonValue v = parse("  { \"a\" : 1 }  ");
   ASSERT_EQ(v.type, JsonType::Object);
   EXPECT_EQ(v.find("a")->asInt64(), std::optional<int64_t>(1));
}

TEST(Json, AsIntOnBoolNullObject)
{
   JsonValue b = parse("true");
   EXPECT_EQ(b.asInt64(), std::optional<int64_t>(1));
   JsonValue n = parse("null");
   EXPECT_EQ(n.asInt64(), std::nullopt);
   EXPECT_EQ(n.asString(), "null");
   EXPECT_EQ(n.asBool(), false);
   JsonValue o = parse("{\"k\": 1}");
   EXPECT_EQ(o.asInt64(), std::nullopt);
   EXPECT_EQ(o.asString(), "");
}

TEST(Json, AsStringTextualForms)
{
   JsonValue num = parse("100");
   EXPECT_EQ(num.asString(), "100");
   JsonValue f = parse("0.5");
   EXPECT_EQ(f.asString(), "0.5");
   JsonValue s = parse("\"T#500ms\"");
   EXPECT_EQ(s.asString(), "T#500ms");
   JsonValue b = parse("true");
   EXPECT_EQ(b.asString(), "true");
}

// ============================================================================
// Errors
// ============================================================================

TEST(Json, ErrorTrailingData)
{
   EXPECT_THROW(parse("{} {}"), JsonParseError);
}

TEST(Json, ErrorUnterminatedObject)
{
   EXPECT_THROW(parse("{\"a\": 1"), JsonParseError);
}

TEST(Json, ErrorUnterminatedString)
{
   EXPECT_THROW(parse("\"abc"), JsonParseError);
}

TEST(Json, ErrorBadEscape)
{
   EXPECT_THROW(parse("\"\\x\""), JsonParseError);
}

TEST(Json, ErrorUnclosedArray)
{
   EXPECT_THROW(parse("[1, 2"), JsonParseError);
}

TEST(Json, ErrorTrailingComma)
{
   EXPECT_THROW(parse("[1, 2,]"), JsonParseError);
}

TEST(Json, ErrorInvalidNumber)
{
   EXPECT_THROW(parse("1."), JsonParseError);
   EXPECT_THROW(parse("01"), JsonParseError);
   EXPECT_THROW(parse(".5"), JsonParseError);
}

TEST(Json, ErrorUnquotedKey)
{
   EXPECT_THROW(parse("{a: 1}"), JsonParseError);
}

TEST(Json, ErrorEmptyInput)
{
   EXPECT_THROW(parse(""), JsonParseError);
   EXPECT_THROW(parse("   "), JsonParseError);
}

TEST(Json, ErrorDuplicateKeyIsRejected)
{
   EXPECT_THROW(parse("{\"a\": 1, \"a\": 2}"), JsonParseError);
}

TEST(Json, ParseErrorHasPosition)
{
   try {
      parse("{\n  \"a\": 1,\n  \"b\": }");
      FAIL() << "expected JsonParseError";
   } catch (const JsonParseError& e) {
      EXPECT_GE(e.line(), 1);
      EXPECT_GE(e.column(), 1);
   }
}

// ============================================================================
// Dump / round-trip
// ============================================================================

TEST(Json, DumpCompactRoundTrip)
{
   const std::string input = "{\"b\": [1, 2.5, \"x\"], \"a\": true, \"n\": null}";
   JsonValue v = parse(input);
   std::string dumped = dump(v, 0);
   JsonValue v2 = parse(dumped);
   EXPECT_EQ(dump(v2, 0), dumped);
   EXPECT_EQ(v2.find("a")->boolean, true);
   EXPECT_EQ(v2.find("b")->array[1].number, 2.5);
   EXPECT_EQ(v2.find("n")->type, JsonType::Null);
}

TEST(Json, DumpPrettyRoundTrip)
{
   const std::string input = "{\"b\": [1, 2.5, \"x\"], \"a\": true, \"n\": null}";
   JsonValue v = parse(input);
   std::string pretty = dump(v, 2);
   JsonValue v2 = parse(pretty);
   EXPECT_EQ(dump(v2, 0), dump(v, 0));
}

TEST(Json, DumpEscapes)
{
   JsonValue v = parse(R"("a\"b\n")");
   std::string dumped = dump(v, 0);
   EXPECT_NE(dumped.find("\\n"), std::string::npos);
   JsonValue v2 = parse(dumped);
   EXPECT_EQ(v2.text, "a\"b\n");
}