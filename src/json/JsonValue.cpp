/**
 * @file JsonValue.cpp
 * @brief JSON parser and serializer implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "json/JsonValue.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace st2cpp::json {

namespace {

/**
 * @brief Internal recursive-descent parser over a UTF-8 text buffer.
 * @details Tracks 1-based line/column for diagnostics. Supports the full JSON
 * grammar: objects, arrays, strings with standard escapes (including \uXXXX),
 * numbers with optional fraction/exponent, and the true/false/null literals.
 */
class Parser
{
public:
   explicit Parser(const std::string& input) : s_(input), pos_(0), line_(1), column_(1) {}

   JsonValue parseDocument()
   {
      skipWhitespace();
      JsonValue root = parseValue();
      skipWhitespace();
      if (pos_ != s_.size()) {
         fail("unexpected trailing characters after JSON value");
      }
      return root;
   }

private:
   const std::string& s_;
   size_t pos_;
   size_t line_;
   size_t column_;

   [[noreturn]] void fail(const std::string& message) { throw JsonParseError(message, line_, column_); }

   char peek() const
   {
      if (pos_ < s_.size()) {
         return s_[pos_];
      }
      return '\0';
   }

   void advance()
   {
      if (pos_ < s_.size()) {
         if (s_[pos_] == '\n') {
            ++line_;
            column_ = 1;
         } else {
            ++column_;
         }
         ++pos_;
      }
   }

   void skipWhitespace()
   {
      while (pos_ < s_.size()) {
         const char c = s_[pos_];
         if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
         } else {
            break;
         }
      }
   }

   void expect(char c)
   {
      if (peek() != c) {
         std::string msg = "expected '" + std::string(1, c) + "'";
         if (pos_ < s_.size()) {
            msg += ", got '" + std::string(1, peek()) + "'";
         } else {
            msg += ", reached end of input";
         }
         fail(msg);
      }
      advance();
   }

   JsonValue parseValue()
   {
      skipWhitespace();
      if (pos_ >= s_.size()) {
         fail("unexpected end of input, expected a JSON value");
      }
      const char c = peek();
      switch (c) {
      case '{':
         return parseObject();
      case '[':
         return parseArray();
      case '"':
         return parseString();
      case 't':
         expectLiteral("true");
         {
            JsonValue v;
            v.type = JsonType::Bool;
            v.boolean = true;
            return v;
         }
      case 'f':
         expectLiteral("false");
         {
            JsonValue v;
            v.type = JsonType::Bool;
            v.boolean = false;
            return v;
         }
      case 'n':
         expectLiteral("null");
         {
            JsonValue v;
            v.type = JsonType::Null;
            return v;
         }
      default:
         if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
         }
         fail(std::string("unexpected character '") + c + "' in JSON value");
      }
   }

   void expectLiteral(const char* literal)
   {
      const size_t len = std::strlen(literal);
      for (size_t i = 0; i < len; ++i) {
         if (pos_ >= s_.size() || s_[pos_] != literal[i]) {
            fail(std::string("invalid literal, expected '") + literal + "'");
         }
         advance();
      }
   }

   JsonValue parseNumber()
   {
      const size_t start = pos_;
      if (peek() == '-') {
         advance();
      }
      // integer part
      if (peek() == '0') {
         advance();
      } else if (peek() >= '1' && peek() <= '9') {
         while (pos_ < s_.size() && peek() >= '0' && peek() <= '9') {
            advance();
         }
      } else {
         fail("invalid number: expected digit");
      }
      // fraction
      if (peek() == '.') {
         advance();
         if (!(pos_ < s_.size() && peek() >= '0' && peek() <= '9')) {
            fail("invalid number: expected digit after decimal point");
         }
         while (pos_ < s_.size() && peek() >= '0' && peek() <= '9') {
            advance();
         }
      }
      // exponent
      if (pos_ < s_.size() && (peek() == 'e' || peek() == 'E')) {
         advance();
         if (pos_ < s_.size() && (peek() == '+' || peek() == '-')) {
            advance();
         }
         if (!(pos_ < s_.size() && peek() >= '0' && peek() <= '9')) {
            fail("invalid number: expected digit in exponent");
         }
         while (pos_ < s_.size() && peek() >= '0' && peek() <= '9') {
            advance();
         }
      }
      const std::string raw = s_.substr(start, pos_ - start);
      JsonValue v;
      v.type = JsonType::Number;
      v.numberRaw = raw;
      errno = 0;
      char* end = nullptr;
      const double value = std::strtod(raw.c_str(), &end);
      if (errno == ERANGE) {
         fail("number out of range");
      }
      v.number = value;
      return v;
   }

   JsonValue parseString()
   {
      JsonValue v;
      v.type = JsonType::String;
      expect('"');
      std::string out;
      out.reserve(16);
      while (true) {
         if (pos_ >= s_.size()) {
            fail("unterminated string");
         }
         const unsigned char c = static_cast<unsigned char>(peek());
         if (c == '"') {
            advance();
            break;
         }
         if (c < 0x20) {
            fail("unescaped control character in string");
         }
         if (c != '\\') {
            out.push_back(static_cast<char>(c));
            advance();
            continue;
         }
         // escape sequence
         advance();
         if (pos_ >= s_.size()) {
            fail("unterminated escape sequence");
         }
         const char e = peek();
         advance();
         switch (e) {
         case '"':
            out.push_back('"');
            break;
         case '\\':
            out.push_back('\\');
            break;
         case '/':
            out.push_back('/');
            break;
         case 'b':
            out.push_back('\b');
            break;
         case 'f':
            out.push_back('\f');
            break;
         case 'n':
            out.push_back('\n');
            break;
         case 'r':
            out.push_back('\r');
            break;
         case 't':
            out.push_back('\t');
            break;
         case 'u': {
            // \uXXXX -> UTF-8 (surrogate pairs handled)
            uint32_t cp = parseHex4();
            if (cp >= 0xD800 && cp <= 0xDBFF) {
               // high surrogate: expect \uDC00-\uDFFF
               if (peek() != '\\') {
                  fail("missing low surrogate in \\u escape");
               }
               advance();
               if (peek() != 'u') {
                  fail("missing low surrogate in \\u escape");
               }
               advance();
               const uint32_t low = parseHex4();
               if (low < 0xDC00 || low > 0xDFFF) {
                  fail("invalid low surrogate in \\u escape");
               }
               cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
               fail("unexpected low surrogate in \\u escape");
            }
            appendUtf8(out, cp);
            break;
         }
         default:
            fail(std::string("invalid escape sequence '\\") + e + "'");
         }
      }
      v.text = std::move(out);
      return v;
   }

   uint32_t parseHex4()
   {
      uint32_t value = 0;
      for (int i = 0; i < 4; ++i) {
         if (pos_ >= s_.size()) {
            fail("truncated \\u escape");
         }
         const char c = peek();
         advance();
         uint32_t nibble;
         if (c >= '0' && c <= '9') {
            nibble = static_cast<uint32_t>(c - '0');
         } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<uint32_t>(c - 'a' + 10);
         } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<uint32_t>(c - 'A' + 10);
         } else {
            fail("invalid hex digit in \\u escape");
         }
         value = (value << 4) | nibble;
      }
      return value;
   }

   static void appendUtf8(std::string& out, uint32_t cp)
   {
      if (cp <= 0x7F) {
         out.push_back(static_cast<char>(cp));
      } else if (cp <= 0x7FF) {
         out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
         out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp <= 0xFFFF) {
         out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
         out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
         out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
         out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
         out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
   }

   JsonValue parseArray()
   {
      JsonValue v;
      v.type = JsonType::Array;
      expect('[');
      skipWhitespace();
      if (peek() == ']') {
         advance();
         return v;
      }
      while (true) {
         v.array.push_back(parseValue());
         skipWhitespace();
         if (peek() == ',') {
            advance();
            skipWhitespace();
            if (peek() == ']') {
               fail("trailing comma in array");
            }
            continue;
         }
         if (peek() == ']') {
            advance();
            break;
         }
         fail("expected ',' or ']' in array");
      }
      return v;
   }

   JsonValue parseObject()
   {
      JsonValue v;
      v.type = JsonType::Object;
      expect('{');
      skipWhitespace();
      if (peek() == '}') {
         advance();
         return v;
      }
      while (true) {
         skipWhitespace();
         if (peek() != '"') {
            fail("expected string key in object");
         }
         JsonValue key = parseString();
         skipWhitespace();
         expect(':');
         JsonValue value = parseValue();
         for (const auto& existing : v.members) {
            if (existing.first == key.text) {
               fail("duplicate object key '" + key.text + "'");
            }
         }
         v.members.emplace_back(std::move(key.text), std::move(value));
         skipWhitespace();
         if (peek() == ',') {
            advance();
            skipWhitespace();
            if (peek() == '}') {
               fail("trailing comma in object");
            }
            continue;
         }
         if (peek() == '}') {
            advance();
            break;
         }
         fail("expected ',' or '}' in object");
      }
      return v;
   }
};

void appendEscaped(std::string& out, const std::string& text)
{
   out.push_back('"');
   for (unsigned char c : text) {
      switch (c) {
      case '"':
         out += "\\\"";
         break;
      case '\\':
         out += "\\\\";
         break;
      case '\b':
         out += "\\b";
         break;
      case '\f':
         out += "\\f";
         break;
      case '\n':
         out += "\\n";
         break;
      case '\r':
         out += "\\r";
         break;
      case '\t':
         out += "\\t";
         break;
      default:
         if (c < 0x20) {
            char buf[8];
            const int n = std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out.append(buf, static_cast<size_t>(n));
         } else {
            out.push_back(static_cast<char>(c));
         }
      }
   }
   out.push_back('"');
}

void dumpValue(const JsonValue& v, const std::string& prefix, int indent, std::string& out)
{
   switch (v.type) {
   case JsonType::Null:
      out += "null";
      break;
   case JsonType::Bool:
      out += v.boolean ? "true" : "false";
      break;
   case JsonType::Number:
      out += v.numberRaw.empty() ? std::to_string(v.number) : v.numberRaw;
      break;
   case JsonType::String:
      appendEscaped(out, v.text);
      break;
   case JsonType::Array: {
      if (v.array.empty()) {
         out += "[]";
         break;
      }
      if (indent < 0) {
         // Compact: no whitespace between elements
         bool first = true;
         for (const JsonValue& elem : v.array) {
            if (!first) {
               out += ',';
            }
            first = false;
            dumpValue(elem, "", -1, out);
         }
         break;
      }
      const std::string pad(static_cast<size_t>(indent), ' ');
      const std::string childPrefix = prefix + pad;
      out += "[\n";
      for (size_t i = 0; i < v.array.size(); ++i) {
         out += childPrefix;
         dumpValue(v.array[i], childPrefix, indent, out);
         if (i + 1 < v.array.size()) {
            out += ',';
         }
         out += '\n';
      }
      out += prefix;
      out += ']';
      break;
   }
   case JsonType::Object: {
      if (v.members.empty()) {
         out += "{}";
         break;
      }
      if (indent < 0) {
         // Compact: no whitespace between members
         bool first = true;
         for (const auto& member : v.members) {
            if (!first) {
               out += ',';
            }
            first = false;
            appendEscaped(out, member.first);
            out += ':';
            dumpValue(member.second, "", -1, out);
         }
         break;
      }
      const std::string pad(static_cast<size_t>(indent), ' ');
      const std::string childPrefix = prefix + pad;
      out += "{\n";
      for (size_t i = 0; i < v.members.size(); ++i) {
         out += childPrefix;
         appendEscaped(out, v.members[i].first);
         out += ": ";
         dumpValue(v.members[i].second, childPrefix, indent, out);
         if (i + 1 < v.members.size()) {
            out += ',';
         }
         out += '\n';
      }
      out += prefix;
      out += '}';
      break;
   }
   }
}

} // namespace

JsonValue parse(const std::string& text)
{
   Parser parser(text);
   return parser.parseDocument();
}

std::string dump(const JsonValue& value, int indent)
{
   std::string out;
   dumpValue(value, "", indent, out);
   return out;
}

} // namespace st2cpp::json