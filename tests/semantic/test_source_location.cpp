/**
 * @file test_source_location.cpp
 * @brief Tests for source locations carried across a merged workspace:
 *        per-entity file names, multi-file diagnostic snippets, and the
 *        precision of parse errors.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <string>
#include <set>
#include <utility>
#include <vector>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/Diagnostics.h"
#include "semantic/SemanticAnalyzer.h"

using namespace st2cpp;
using namespace st2cpp::semantic;

namespace {

/// Parse one .st snippet, tagging every entity with `file` the way the CLI does
/// when it builds a workspace-wide translation unit.
TranslationUnit parseAs(const std::string& st, const std::string& file)
{
   Lexer lexer(st, file);
   Parser parser(lexer.tokenize(), file);
   return parser.parseTranslationUnit();
}

} // namespace

// ============================================================================
// Parse error position
// ============================================================================

TEST(ParseErrorPosition, MissingSemicolonPointsAtEndOfPreviousToken)
{
   // The ';' is missing at the end of line 3, but the next token sits on line 4.
   // Reporting that token would blame END_VAR, which is not the mistake.
   const char* src = "PROGRAM Main\nVAR\n    x : INT\nEND_VAR\n    x := 1;\nEND_PROGRAM\n";
   try {
      parseAs(src, "t.st");
      FAIL() << "expected a ParseError";
   } catch (const ParseError& e) {
      EXPECT_EQ(e.line, 3u) << "should point at the line missing the ';'";
      EXPECT_EQ(e.col, 12u) << "should point just past INT";
      EXPECT_EQ(e.fileName, "t.st");
   }
}

TEST(ParseErrorPosition, StructMemberMissingSemicolon)
{
   const char* src = "TYPE\n    Rec : STRUCT\n        a : INT\n    END_STRUCT\nEND_TYPE\n";
   try {
      parseAs(src, "t.st");
      FAIL() << "expected a ParseError";
   } catch (const ParseError& e) {
      EXPECT_EQ(e.line, 3u);
      EXPECT_EQ(e.col, 16u);
   }
}

TEST(ParseErrorPosition, FirstTokenFailureFallsBackToOffendingToken)
{
   // Nothing consumed yet, so there is no insertion point to point at.
   const char* src = "GARBAGE\n";
   try {
      parseAs(src, "t.st");
      FAIL() << "expected a ParseError";
   } catch (const ParseError& e) {
      EXPECT_EQ(e.fileName, "t.st");
      EXPECT_GE(e.line, 1u);
   }
}

// ============================================================================
// Per-entity source file
// ============================================================================

TEST(SourceLocation, ParserTagsEveryTopLevelEntityWithItsFile)
{
   const char* src = "VAR_GLOBAL g : INT; END_VAR\n"
                     "TYPE E : (A, B); END_TYPE\n"
                     "TYPE R : STRUCT a : INT; END_STRUCT END_TYPE\n"
                     "TYPE Al : INT; END_TYPE\n"
                     "FUNCTION_BLOCK FB\nVAR_INPUT i : INT; END_VAR\nEND_FUNCTION_BLOCK\n"
                     "FUNCTION F : INT\nF := 1;\nEND_FUNCTION\n";
   TranslationUnit tu = parseAs(src, "unit.st");

   ASSERT_EQ(tu.globals.size(), 1u);
   EXPECT_EQ(tu.globals[0].fileName, "unit.st");
   ASSERT_EQ(tu.enums.size(), 1u);
   EXPECT_EQ(tu.enums[0].fileName, "unit.st");
   ASSERT_EQ(tu.structs.size(), 1u);
   EXPECT_EQ(tu.structs[0].fileName, "unit.st");
   ASSERT_EQ(tu.typeAliases.size(), 1u);
   EXPECT_EQ(tu.typeAliases[0].fileName, "unit.st");
   ASSERT_EQ(tu.pous.size(), 2u);
   EXPECT_EQ(tu.pous[0].fileName, "unit.st");
   EXPECT_EQ(tu.pous[1].fileName, "unit.st");
}

TEST(SourceLocation, VarSectionCarriesPosition)
{
   const char* src = "FUNCTION_BLOCK FB\nVAR_INPUT i : INT; END_VAR\nEND_FUNCTION_BLOCK\n";
   TranslationUnit tu = parseAs(src, "fb.st");
   ASSERT_EQ(tu.pous.size(), 1u);
   ASSERT_FALSE(tu.pous[0].varSections.empty());
   const VarSection& sec = tu.pous[0].varSections[0];
   EXPECT_EQ(sec.fileName, "fb.st");
   EXPECT_GT(sec.line, 0u) << "a variable section should record where it starts";
}

// ============================================================================
// Diagnostics across a merged workspace
// ============================================================================

TEST(SourceLocation, SemanticErrorNamesTheFileItComesFrom)
{
   // Two files merged into one unit, as --project-style does. The error is in
   // b_use.st, so that is the file the diagnostic must name, not the workspace.
   TranslationUnit tu;
   {
      TranslationUnit a = parseAs("TYPE MyInt : INT; END_TYPE\n", "a_defs.st");
      tu.typeAliases.insert(tu.typeAliases.end(), a.typeAliases.begin(), a.typeAliases.end());
   }
   {
      TranslationUnit b = parseAs("PROGRAM Main\nVAR\n    q : MyInt;\nEND_VAR\n    q := undefinedOne;\nEND_PROGRAM\n",
                                 "b_use.st");
      tu.pous.insert(tu.pous.end(), b.pous.begin(), b.pous.end());
   }

   SemanticAnalyzer analyzer;
   analyzer.setSourceName("ws"); // the workspace, as the CLI passes it
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);
   ASSERT_GT(info.diagnostics.totalCount(), 0u);

   bool sawUndeclared = false;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code != DiagnosticCode::UndeclaredIdentifier) {
         continue;
      }
      sawUndeclared = true;
      EXPECT_EQ(d.location.fileName, "b_use.st");
   }
   EXPECT_TRUE(sawUndeclared) << "expected an undeclared-identifier diagnostic";
}

TEST(SourceLocation, TypeAliasIsVisibleAcrossMergedFiles)
{
   // The bug this guards: the workspace merge used to drop type aliases, which
   // silently made them file-local.
   TranslationUnit tu;
   {
      TranslationUnit a = parseAs("TYPE MyInt : INT; END_TYPE\n", "a_defs.st");
      tu.typeAliases.insert(tu.typeAliases.end(), a.typeAliases.begin(), a.typeAliases.end());
   }
   {
      TranslationUnit b = parseAs("PROGRAM Main\nVAR\n    x : MyInt;\nEND_VAR\n    x := 1;\nEND_PROGRAM\n",
                                 "b_use.st");
      tu.pous.insert(tu.pous.end(), b.pous.begin(), b.pous.end());
   }

   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);
   EXPECT_EQ(info.diagnostics.errorCount(), 0u) << "a type alias declared in another file must resolve";
}

// ============================================================================
// Multi-source snippets
// ============================================================================

TEST(SourceLocation, SnippetIsCutFromTheFileTheLocationNames)
{
   const std::string defs = "TYPE MyInt : INT; END_TYPE\n";
   const std::string use = "PROGRAM Main\nVAR\n    x : MyInt;\nEND_VAR\n    x := missingThing;\nEND_PROGRAM\n";

   Diagnostics diag;
   diag.setSourceName("ws");
   diag.addSourceFile("a_defs.st", defs);
   diag.addSourceFile("b_use.st", use);

   SourceLocation loc;
   loc.fileName = "b_use.st";
   loc.line = 5;
   loc.column = 14;
   loc.endLine = 5;
   loc.endColumn = 26;
   diag.addError(DiagnosticCode::UndeclaredIdentifier, "unknown identifier 'missingThing'", loc);

   std::ostringstream out;
   diag.print(out);
   const std::string text = out.str();
   EXPECT_NE(text.find("b_use.st:5:14:"), std::string::npos) << text;
   // The snippet must come from b_use.st, not from the primary source.
   EXPECT_NE(text.find("x := missingThing;"), std::string::npos) << text;
}

TEST(SourceLocation, UnknownTypePointsAtTheTypeName)
{
   // The diagnostic must blame the type name, not the declaration mentioning
   // it: "coordinates : ST_Vector3" is wrong at the type, not at 'coordinates'.
   const char* src = "PROGRAM Main\n"
                     "VAR\n"
                     "    w : Missing;\n"
                     "END_VAR\n"
                     "END_PROGRAM\n";
   Lexer lexer(src, "t.st");
   Parser parser(lexer.tokenize(), "t.st");
   TranslationUnit tu = parser.parseTranslationUnit();

   SemanticAnalyzer analyzer;
   analyzer.setSourceName("t.st");
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);

   const Diagnostic* found = nullptr;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::InvalidTypeName) {
         found = &d;
      }
   }
   ASSERT_NE(found, nullptr) << "expected an unknown-type diagnostic";
   EXPECT_EQ(found->location.fileName, "t.st");
   EXPECT_EQ(found->location.line, 3u);
   // Column of 'Missing', not of 'w'.
   EXPECT_EQ(found->location.column, 9u);
}

TEST(SourceLocation, UnknownTypeInMethodAndArrayPointsAtTheTypeName)
{
   const char* src = "FUNCTION_BLOCK FB\n"
                     "METHOD PUBLIC M : Missing1\n"
                     "VAR_INPUT\n"
                     "    p : Missing2;\n"
                     "END_VAR\n"
                     "    M := TRUE;\n"
                     "END_METHOD\n"
                     "END_FUNCTION_BLOCK\n"
                     "FUNCTION F : Missing3\n"
                     "VAR\n"
                     "    v : ARRAY[0..3] OF Missing4;\n"
                     "END_VAR\n"
                     "END_FUNCTION\n";
   Lexer lexer(src, "t.st");
   Parser parser(lexer.tokenize(), "t.st");
   TranslationUnit tu = parser.parseTranslationUnit();

   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);

   std::vector<std::pair<uint32_t, uint32_t>> positions;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::InvalidTypeName) {
         positions.emplace_back(d.location.line, d.location.column);
      }
   }
   ASSERT_EQ(positions.size(), 4u) << "expected one diagnostic per missing type";

   // Every diagnostic must carry a real column, otherwise the snippet is
   // suppressed and the header degrades to a bare "file:".
   std::set<std::pair<uint32_t, uint32_t>> unique(positions.begin(), positions.end());
   EXPECT_EQ(unique.size(), 4u) << "each diagnostic should point at its own type name";
   for (const auto& pos : positions) {
      EXPECT_GT(pos.first, 0u);
      EXPECT_GT(pos.second, 0u);
   }
}

TEST(SourceLocation, SelfContainingTypeIsReportedAtTheOffendingMember)
{
   // A struct holding a value of its own type has no finite size. The
   // diagnostic must name the member and point at its declaration.
   const char* src = "TYPE Motor5 :\n"
                     "    STRUCT\n"
                     "        enabled : BOOL;\n"
                     "        Motor_ : Motor5;\n"
                     "    END_STRUCT\n"
                     "END_TYPE\n";
   Lexer lexer(src, "easy.st");
   Parser parser(lexer.tokenize(), "easy.st");
   TranslationUnit tu = parser.parseTranslationUnit();

   SemanticAnalyzer analyzer;
   analyzer.setSourceName("ws"); // workspace, as the CLI passes it
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);

   const Diagnostic* found = nullptr;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::CircularDependency) {
         found = &d;
      }
   }
   ASSERT_NE(found, nullptr) << "expected a circular-dependency diagnostic";
   EXPECT_NE(found->message.find("contains itself"), std::string::npos) << found->message;
   EXPECT_NE(found->message.find("Motor_"), std::string::npos) << found->message;
   // The file comes from the member's own declaration, not from the workspace.
   EXPECT_EQ(found->location.fileName, "easy.st");
   EXPECT_EQ(found->location.line, 4u);
   EXPECT_GT(found->location.column, 0u);
}

TEST(SourceLocation, MutualCycleNamesBothTypesAndTheMember)
{
   const char* src = "TYPE A :\n"
                     "    STRUCT\n"
                     "        b : B;\n"
                     "    END_STRUCT\n"
                     "END_TYPE\n"
                     "TYPE B :\n"
                     "    STRUCT\n"
                     "        a : A;\n"
                     "    END_STRUCT\n"
                     "END_TYPE\n";
   Lexer lexer(src, "mutual.st");
   Parser parser(lexer.tokenize(), "mutual.st");
   TranslationUnit tu = parser.parseTranslationUnit();

   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);

   const Diagnostic* found = nullptr;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::CircularDependency) {
         found = &d;
      }
   }
   ASSERT_NE(found, nullptr) << "expected a circular-dependency diagnostic";
   // Both ends of the cycle and the member closing it must be named.
   EXPECT_NE(found->message.find("'A'"), std::string::npos) << found->message;
   EXPECT_NE(found->message.find("'B'"), std::string::npos) << found->message;
   EXPECT_NE(found->message.find("'a'"), std::string::npos) << found->message;
   EXPECT_EQ(found->location.fileName, "mutual.st");
   EXPECT_GT(found->location.line, 0u);
}

TEST(SourceLocation, FallsBackToPrimarySourceWhenLocationHasNoFile)
{
   Diagnostics diag;
   diag.setSourceName("only.st");
   diag.setSourceText("PROGRAM Main\nEND_PROGRAM\n");

   SourceLocation loc;
   loc.line = 1;
   loc.column = 1;
   diag.addError(DiagnosticCode::UndeclaredIdentifier, "boom", loc);

   std::ostringstream out;
   diag.print(out);
   EXPECT_NE(out.str().find("only.st:1:1:"), std::string::npos) << out.str();
   EXPECT_NE(out.str().find("PROGRAM Main"), std::string::npos) << out.str();
}

// ============================================================================
// Inherited function block parameters
// ============================================================================

namespace {

/// Analyze ST and return the InvalidTypeName / WrongArgumentCount counts.
void analyzeCounts(const std::string& st, size_t& wrongArity, size_t& unknownType)
{
   Lexer lexer(st, "t.st");
   Parser parser(lexer.tokenize(), "t.st");
   TranslationUnit tu = parser.parseTranslationUnit();
   SemanticAnalyzer analyzer;
   SemanticInfo info = analyzer.analyze(tu, SemanticAnalyzer::Strictness::Permissive);
   wrongArity = 0;
   unknownType = 0;
   for (const auto& d : info.diagnostics.all()) {
      if (d.code == DiagnosticCode::WrongArgumentCount) ++wrongArity;
      if (d.code == DiagnosticCode::InvalidTypeName) ++unknownType;
   }
}

} // namespace

TEST(InheritedParams, CallWithBaseAndOwnParametersIsAccepted)
{
   // A derived block is callable with the base's parameters: the arity check
   // must see the whole interface, not only the derived block's own.
   const char* st = "FUNCTION_BLOCK Base\n"
                    "VAR_INPUT\n"
                    "    abilita : BOOL;\n"
                    "    soglia : INT;\n"
                    "END_VAR\n"
                    "VAR_OUTPUT\n"
                    "    allarme : BOOL;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "FUNCTION_BLOCK Derivato EXTENDS Base\n"
                    "VAR_INPUT\n"
                    "    gain : REAL;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "PROGRAM Main\n"
                    "VAR\n"
                    "    d : Derivato;\n"
                    "END_VAR\n"
                    "    d(abilita := TRUE, soglia := 10, gain := 1.0);\n"
                    "END_PROGRAM\n";
   size_t wrongArity = 0, unknownType = 0;
   analyzeCounts(st, wrongArity, unknownType);
   EXPECT_EQ(unknownType, 0u);
   EXPECT_EQ(wrongArity, 0u) << "inherited inputs must be accepted at the call site";
}

TEST(InheritedParams, PositionalCallBindsAgainstTheFullInterface)
{
   // Positional arguments pair against base-first ordering, so the base
   // interface has to stay the prefix of the derived one.
   const char* st = "FUNCTION_BLOCK Base\n"
                    "VAR_INPUT\n"
                    "    a : INT;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "FUNCTION_BLOCK Derivato EXTENDS Base\n"
                    "VAR_INPUT\n"
                    "    b : INT;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "PROGRAM Main\n"
                    "VAR\n"
                    "    d : Derivato;\n"
                    "END_VAR\n"
                    "    d(1, 2);\n"
                    "END_PROGRAM\n";
   size_t wrongArity = 0, unknownType = 0;
   analyzeCounts(st, wrongArity, unknownType);
   EXPECT_EQ(wrongArity, 0u);
}

TEST(InheritedParams, RedeclaredParameterIsNotDuplicated)
{
   // Redeclaring a base parameter in the derived block replaces it: the call
   // must not expect it twice.
   const char* st = "FUNCTION_BLOCK Base\n"
                    "VAR_INPUT\n"
                    "    comune : INT;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "FUNCTION_BLOCK Derivato EXTENDS Base\n"
                    "VAR_INPUT\n"
                    "    comune : DINT;\n"
                    "END_VAR\n"
                    "END_FUNCTION_BLOCK\n"
                    "PROGRAM Main\n"
                    "VAR\n"
                    "    d : Derivato;\n"
                    "END_VAR\n"
                    "    d(comune := 1);\n"
                    "END_PROGRAM\n";
   size_t wrongArity = 0, unknownType = 0;
   analyzeCounts(st, wrongArity, unknownType);
   EXPECT_EQ(wrongArity, 0u);
}
