/**
 * @file test_codegen_semantic.cpp
 * @brief Tests verifying the semantic-driven CodeGenerator migration
 *
 * The CodeGenerator consumes SemanticInfo (decorated AST + symbol table) when
 * it is attached via CodeGenerator::setSemanticInfo(). These tests compare the
 * semantic-driven output against the legacy syntactic inference, and pin the
 * semantics-only behaviors (canonical BOOL operand resolution, validated
 * conversion casts, signature-based named argument reordering).
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "ast/AST.h"
#include "codegen/CodeGenerator.h"
#include "semantic/SemanticAnalyzer.h"
#include <regex>
#include <string>

class CodegenSemanticTest : public ::testing::Test
{
protected:
   bool hasMatch(const std::string& source, const std::string& pattern) const
   {
      try {
         return std::regex_search(source, std::regex(pattern));
      } catch (const std::regex_error&) {
         return source.find(pattern) != std::string::npos;
      }
   }

   void expectSource(const std::string& source, const std::string& pattern, const std::string& msg = "")
   {
      EXPECT_TRUE(hasMatch(source, pattern)) << msg << "\nFull source:\n" << source;
   }
};

// ============================================================================
// Test 1 — Struct-member BOOL operands: semantic path resolves BOOL, legacy
// heuristic treats the whole struct access as non-BOOL. With semantics the
// AND must lower to logical && (this is the migration driver case).
// ============================================================================
TEST_F(CodegenSemanticTest, StructMemberBoolLogicalAndWithSemantics)
{
   const std::string st = R"(
      TYPE S : STRUCT
         flag : BOOL;
         value : INT;
      END_STRUCT
      END_TYPE

      FUNCTION_BLOCK Test
         VAR
            s : S;
            b : BOOL;
         END_VAR
         b := s.flag AND TRUE;
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(B = S\.FLAG && true;)", "struct-member BOOL AND must be logical");
}

// ============================================================================
// Test 2 — The same source WITHOUT semantics keeps the legacy inference:
// the non-BOOL heuristic yields bitwise AND. This pins the degradation path.
// ============================================================================
TEST_F(CodegenSemanticTest, StructMemberBoolBitwiseAndLegacy)
{
   const std::string st = R"(
      TYPE S : STRUCT
         flag : BOOL;
         value : INT;
      END_STRUCT
      END_TYPE

      FUNCTION_BLOCK Test
         VAR
            s : S;
            b : BOOL;
         END_VAR
         b := s.flag AND TRUE;
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromST(st);
   expectSource(code.source, R"(B = S\.FLAG & true;)", "without semantics the heuristic stays bitwise");
}

// ============================================================================
// Test 3 — BOOL XOR lowers to logical != while integer XOR stays bitwise ^.
// ============================================================================
TEST_F(CodegenSemanticTest, XorBoolLogicalIntegerBitwiseWithSemantics)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            a : BOOL;
            b : BOOL;
            i : INT;
            j : INT;
            x : BOOL;
            y : INT;
         END_VAR
         x := a XOR b;
         y := i XOR j;
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(X = A != B;)", "BOOL XOR must be logical !=");
   expectSource(code.source, R"(Y = I \^ J;)", "INT XOR must stay bitwise ^");
}

// ============================================================================
// Test 4 — Plain BOOL AND works identically with and without semantics.
// ============================================================================
TEST_F(CodegenSemanticTest, BoolAndIdenticalInBothModes)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            a : BOOL;
            b : BOOL;
            r : BOOL;
         END_VAR
         r := a AND b;
      END_FUNCTION_BLOCK
   )";

   auto withSem = TestHelper::generateFromSTWithSemantics(st);
   auto withoutSem = TestHelper::generateFromST(st);
   expectSource(withSem.source, R"(R = A && B;)");
   expectSource(withoutSem.source, R"(R = A && B;)");
}

// ============================================================================
// Test 5 — Conversion casts: semantic path makes explicit the IEC implicit
// conversion for elementary numeric assignments (INT := REAL -> static_cast).
// Same-typed assignments must NOT get a cast.
// ============================================================================
TEST_F(CodegenSemanticTest, AssignmentConversionCastWithSemantics)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            iVar : INT;
            uiVar : UINT;
            rVar : REAL;
            same : INT;
         END_VAR
         iVar := 10.5;
         iVar := rVar;
         uiVar := iVar;
         same := iVar;
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(IVAR = static_cast<Int16>\(10\.5\);)", "REAL literal -> INT cast");
   expectSource(code.source, R"(IVAR = static_cast<Int16>\(RVAR\);)", "REAL -> INT cast");
   expectSource(code.source, R"(UIVAR = static_cast<UInt16>\(IVAR\);)", "INT -> UINT cast");
   EXPECT_FALSE(hasMatch(code.source, R"(SAME = static_cast<Int16>\(IVAR\))"));
}

// ============================================================================
// Test 6 — No conversion casts without semantic info (legacy exact copy).
// ============================================================================
TEST_F(CodegenSemanticTest, AssignmentNoCastLegacy)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            iVar : INT;
            rVar : REAL;
         END_VAR
         iVar := rVar;
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromST(st);
   EXPECT_TRUE(hasMatch(code.source, R"(IVAR = RVAR;)"));
   EXPECT_FALSE(hasMatch(code.source, "static_cast"));
}

// ============================================================================
// Test 7 — Named-argument calls reorder via the semantic symbol table
// (POU params are registered by the declarative phase) when the legacy
// signature map cannot see the callee.
// ============================================================================
TEST_F(CodegenSemanticTest, NamedArgumentsViaSemanticSignature)
{
   const std::string st = R"(
      FUNCTION Add : INT
         VAR_INPUT
            A : INT;
            B : INT;
         END_VAR
         Add := A + B;
      END_FUNCTION

      FUNCTION_BLOCK Test
         VAR
            rv : INT;
         END_VAR
         rv := Add(B := 2, A := 1);
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(RV = ADD\(1, 2\);)", "named args must be reordered by parameter list");
}

// ============================================================================
// Test 8 — The decorated AST must not change the signal of a plain function
// block invocation or its operator() emission.
// ============================================================================
TEST_F(CodegenSemanticTest, FunctionBlockCallUnchangedWithSemantics)
{
   const std::string st = R"(
      FUNCTION_BLOCK Child
         VAR
            v : INT;
         END_VAR
      END_FUNCTION_BLOCK

      FUNCTION_BLOCK Parent
         VAR
            c : Child;
         END_VAR
         c();
      END_FUNCTION_BLOCK
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(C\(\);)", "FB call unchanged");
   expectSource(code.source, R"(void PARENT::operator\(\)\(\))", "FB operator() emitted");
}

// ============================================================================
// Test 9 — Function RETURN handling is unaffected by semantics.
// ============================================================================
TEST_F(CodegenSemanticTest, FunctionReturnWithSemantics)
{
   const std::string st = R"(
      FUNCTION Test : REAL
         VAR_INPUT
            v : REAL;
         END_VAR
         Test := v * 2.0;
         RETURN;
      END_FUNCTION
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(return TEST_ret;)", "RETURN lowers to return <fn>_ret");
   expectSource(code.source, R"(Float TEST\(Float V\))", "return type Float with input parameter");
}

// ============================================================================
// Test 10 — Fase 1 (mapTypeId): attaching semantics must never change the
// emitted C++ type spelling for any TypeRef class. This pins the mapType() ==
// mapTypeId() equivalence by construction on every generator-visible case:
// elementary int/uint/real, BOOL, bit strings, TIME, STRING, NAMED struct/enum/
// FB, POINTER/REF_TO, and ARRAYs with non-zero and negative bounds (after the
// semantic array-bounds fix, mapTypeId resolves the real STArray bounds).
// ============================================================================
TEST_F(CodegenSemanticTest, DeclaredTypeSpellingIdenticalWithAndWithoutSemantics)
{
   const std::string st = R"(
      TYPE Point : STRUCT
         x : INT;
         y : REAL;
      END_STRUCT
      END_TYPE

      TYPE Color : (RED, GREEN, BLUE); END_TYPE

      FUNCTION_BLOCK Motor
         VAR
            speed : INT;
         END_VAR
      END_FUNCTION_BLOCK

      VAR_GLOBAL
         arr : ARRAY[2..10] OF INT;
         vals : ARRAY[-5..5] OF REAL;
         grid : ARRAY[1..3, 0..4] OF BOOL;
      END_VAR

      PROGRAM Main
         VAR
            p : Point;
            c : Color;
            m : Motor;
            b : BOOL;
            w : WORD;
            t : TIME;
            s : STRING;
            ptr : POINTER TO INT;
            ref : REF_TO REAL;
         END_VAR
      END_PROGRAM
   )";

   auto withSem = TestHelper::generateFromSTWithSemantics(st);
   auto withoutSem = TestHelper::generateFromST(st);

   // Array bounds must survive the semantic pipeline exactly like the legacy path.
   expectSource(withSem.header, R"(STArray<Int16, 2, 10> ARR)", "semantic ARRAY[2..10] bounds");
   expectSource(withSem.header, R"(STArray<Float, -5, 5> VALS)", "semantic ARRAY[-5..5] bounds");
   expectSource(withSem.header, R"(STArray<STArray<Bool, 0, 4>, 1, 3> GRID)", "semantic 2-D ARRAY bounds");
   // Named, pointer/reference and elementary spellings must be unchanged.
   expectSource(withSem.header, R"(POINT P)", "semantic struct member");
   expectSource(withSem.header, R"(COLOR C)", "semantic enum member");
   expectSource(withSem.header, R"(MOTOR M)", "semantic FB member");
   expectSource(withSem.header, R"(Int16\* PTR)", "semantic POINTER TO INT");
   expectSource(withSem.header, R"(Float& REF)", "semantic REF_TO REAL");
   expectSource(withSem.header, R"(String S)", "semantic STRING");
   expectSource(withSem.header, R"(Bool B)", "semantic BOOL");
   expectSource(withSem.header, R"(UInt16 W)", "semantic WORD maps to UInt16");

   // The whole translated module must be byte-identical: attaching semantics
   // must be invisible on declaration-only code (no cast/operator heuristics).
   EXPECT_EQ(withSem.header, withoutSem.header) << "semantic vs legacy header must match";
   EXPECT_EQ(withSem.source, withoutSem.source) << "semantic vs legacy source must match";
}


// ============================================================================
// Test 11 — Modular FB ordering (Fase 3): with semantics attached, the
// FunctionBlocks master include list follows fbTopoOrder. FB_Derived EXTENDS
// FB_Base but is declared BEFORE it in the source; the semantic dependency
// (base before derived) must win over the declaration order, so that
// `struct FB_DERIVED : public FB_BASE` appears after the base definition.
// ============================================================================
TEST_F(CodegenSemanticTest, ModularFbOrderingBaseBeforeDerivedWithSemantics)
{
   const std::string st = R"(
      FUNCTION_BLOCK FB_Derived EXTENDS FB_Base
         VAR
            extra : INT;
         END_VAR
      END_FUNCTION_BLOCK

      FUNCTION_BLOCK FB_Base
         VAR
            id : INT;
         END_VAR
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

   CodeGenerator gen;
   gen.setRuntimeHeader("undoCore/types.hpp");
   gen.setCaseSensitive(false);
   ProcessImageConfig pi;
   pi.inputBytes = 1024;
   pi.outputBytes = 1024;
   pi.markerBytes = 1024;
   pi.autoDetect = true;
   gen.setProcessImageConfig(pi);
   gen.setSemanticInfo(&info);

   auto files = gen.generateModularProject(tu, "/tmp/st2cpp_modular_order_test");

   std::string master;
   for (const auto& file : files) {
      if (file.name == "FunctionBlocks" && file.type == GenFileType::MASTER) {
         master = file.content;
      }
   }
   EXPECT_FALSE(master.empty());

size_t basePos = master.find("FB_BASE.hpp");
    size_t derivedPos = master.find("FB_DERIVED.hpp");
    EXPECT_NE(basePos, std::string::npos);
    EXPECT_NE(derivedPos, std::string::npos);
    EXPECT_LT(basePos, derivedPos) << "master must include FB_BASE before FB_DERIVED:\n" << master;
}

// ============================================================================
// Fase 6 — Strict mode (IEC 61131-3 implicit-conversion enforcement) and
// preservedSemantics. Every strict diagnostic is additive and reached ONLY
// when Strictness::Strict is requested; Permissive (the default) must match
// the golden path byte-for-byte (verified by the DeclaredTypeSpelling and
// golden suites above).
// ============================================================================

// ============================================================================
// Test 12 — Mixed-sign integer operands pass permissively (no diagnostics) but
// are reported in Strict mode. This pins the strategy: Strict adds diagnostics,
// Permissive stays the legacy-compatible baseline.
// ============================================================================
TEST_F(CodegenSemanticTest, BinaryMixedSignIntegerOperandsStrictVsPermissive)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            a : UINT;
            b : INT;
            r : UINT;
         END_VAR
         r := a + b;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer permissive;
   auto infoPerm = permissive.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(infoPerm.diagnostics.hasErrors())
       << "Permissive must accept INT/UINT binary operands silently";

   st2cpp::semantic::SemanticAnalyzer strict;
   auto infoStrict = strict.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_TRUE(infoStrict.diagnostics.hasErrors())
       << "Strict mode must report mixed-sign integer operands";
   ASSERT_NE(infoStrict.diagnostics.errorCount(), 0U);
   const auto& msg = infoStrict.diagnostics.all()[0].message;
   EXPECT_NE(msg.find("strict IEC"), std::string::npos) << msg;
}

// ============================================================================
// Test 13 — Integer -> Real binary operands stay valid in Strict mode
// (IEC 61131-3 implicit widening), while adding strict diagnostics must not
// alter the emitted code in Permissive mode.
// ============================================================================
TEST_F(CodegenSemanticTest, BinaryIntegerRealWideningAllowedAndPermissiveOutputUnchanged)
{
   const std::string st = R"(
      FUNCTION_BLOCK Test
         VAR
            i : INT;
            r : REAL;
         END_VAR
         r := i + r;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer strict;
   auto infoStrict = strict.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_FALSE(infoStrict.diagnostics.hasErrors())
       << "INT + REAL widens implicitly to REAL, must be valid in Strict mode";

   // Permissive codegen output must be the legacy emission: no cast, plain sum.
   auto code = TestHelper::generateFromSTWithSemantics(st);
   expectSource(code.source, R"(R = I \+ R;)");
   EXPECT_FALSE(hasMatch(code.source, "static_cast"));
}

// ============================================================================
// Test 14 — Call arguments: a UINT argument for an INT parameter is accepted
// permissively (C++ delegation) but reported in Strict mode.
// ============================================================================
TEST_F(CodegenSemanticTest, CallArgumentSignMismatchStrictVsPermissive)
{
   const std::string st = R"(
      FUNCTION F : INT
         VAR_INPUT
            X : INT;
         END_VAR
         F := X;
      END_FUNCTION

      FUNCTION_BLOCK Test
         VAR
            ui : UINT;
            r : INT;
         END_VAR
         r := F(ui);
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer permissive;
   auto infoPerm = permissive.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(infoPerm.diagnostics.hasErrors())
       << "Permissive must accept a UINT argument for an INT parameter";

   st2cpp::semantic::SemanticAnalyzer strict;
   auto infoStrict = strict.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_TRUE(infoStrict.diagnostics.hasErrors())
       << "Strict mode must report a non-implicit call argument conversion";
   bool found = false;
   for (const auto& d : infoStrict.diagnostics.all()) {
      if (d.message.find("strict IEC") != std::string::npos) found = true;
   }
   EXPECT_TRUE(found) << "expected a 'strict IEC' call-argument diagnostic";
}

// ============================================================================
// Test 15 — RETURN narrowing: REAL returned from an INT function is valid in
// Permissive mode but flagged in Strict mode.
// ============================================================================
TEST_F(CodegenSemanticTest, ReturnNarrowingStrictVsPermissive)
{
   const std::string st = R"(
      FUNCTION F : INT
         VAR_INPUT
            X : REAL;
         END_VAR
         RETURN X;
      END_FUNCTION
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer permissive;
   auto infoPerm = permissive.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(infoPerm.diagnostics.hasErrors())
       << "Permissive must accept REAL into INT RETURN (delegated to C++)";

   st2cpp::semantic::SemanticAnalyzer strict;
   auto infoStrict = strict.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_TRUE(infoStrict.diagnostics.hasErrors())
       << "Strict mode must report the narrowing RETURN";
   bool found = false;
   for (const auto& d : infoStrict.diagnostics.all()) {
      if (d.message.find("strict IEC") != std::string::npos) found = true;
   }
   EXPECT_TRUE(found) << "expected a 'strict IEC' return diagnostic";
}

// ============================================================================
// Test 16 — Index arity: a 1-D array indexed with two indices is accepted
// permissively (the C++ template still compiles) but reported in Strict mode.
// A REAL index, by contrast, is a hard error in BOTH modes.
// ============================================================================
TEST_F(CodegenSemanticTest, IndexArityAndRealIndexStrictness)
{
   // Excess indices (more than the array rank) are a structural type error:
   // after the first index a 1-D array yields its element, and indexing that
   // element again is invalid in every mode.
   const std::string arity = R"(
      FUNCTION_BLOCK Test
         VAR
            arr : ARRAY[1..10] OF INT;
            i : INT;
         END_VAR
         arr[i, i] := 0;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(arity, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer permissive;
   auto infoPerm = permissive.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_TRUE(infoPerm.diagnostics.hasErrors())
       << "Indexing the INT element of a 1-D array again must be an error";

   st2cpp::semantic::SemanticAnalyzer strict;
   auto infoStrict = strict.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_TRUE(infoStrict.diagnostics.hasErrors())
       << "Strict mode must report the excess index too";

   const std::string realIndex = R"(
      FUNCTION_BLOCK Test
         VAR
            arr : ARRAY[1..10] OF INT;
         END_VAR
         arr[1.5] := 0;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer2(realIndex, "<test>");
   auto tokens2 = lexer2.tokenize();
   Parser parser2(std::move(tokens2));
   auto tu2 = parser2.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer permissive2;
   auto infoPerm2 = permissive2.analyze(tu2, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_TRUE(infoPerm2.diagnostics.hasErrors())
       << "A REAL array index is a hard error even in Permissive mode";
}

// ============================================================================
// Test 17 — preservedSemantics: the analyzer record reflects whether it ran,
// the requested strictness, and whether every reference resolved.
// ============================================================================
TEST_F(CodegenSemanticTest, PreservedSemanticsRecordedByAnalyzer)
{
   const std::string clean = R"(
      FUNCTION_BLOCK Test
         VAR
            i : INT;
         END_VAR
         i := 1;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(clean, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Strict);
   EXPECT_TRUE(info.preservedSemantics.semanticModeApplied);
   EXPECT_EQ(info.preservedSemantics.strictness, st2cpp::semantic::AnalysisStrictness::Strict);
   EXPECT_GT(info.preservedSemantics.resolvedCount, 0U);
   EXPECT_EQ(info.preservedSemantics.unresolvedCount, 0U);
   EXPECT_TRUE(info.preservedSemantics.preserved());

   const std::string withUnknown = R"(
      FUNCTION_BLOCK Test
         VAR
            i : INT;
         END_VAR
         i := unknownIdentifier;
      END_FUNCTION_BLOCK
   )";

   Lexer lexer2(withUnknown, "<test>");
   auto tokens2 = lexer2.tokenize();
   Parser parser2(std::move(tokens2));
   auto tu2 = parser2.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer2;
   auto info2 = analyzer2.analyze(tu2, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_TRUE(info2.preservedSemantics.semanticModeApplied);
   EXPECT_GT(info2.preservedSemantics.unresolvedCount, 0U);
   EXPECT_FALSE(info2.preservedSemantics.preserved());
}

// ============================================================================
// Test 18 — resolvedATAddresses: the analyzer records every placement (AT)
// variable using the same keys the CodeGenerator looks up: plain "VAR" for
// globals, "FBName::VAR" for POU locals, normalized uppercase. This closes the
// SemanticInfo contract: the map is no longer a dead field.
// ============================================================================
TEST_F(CodegenSemanticTest, ResolvedAtAddressesRecordedByAnalyzer)
{
   const std::string st = R"(
      VAR_GLOBAL
         g_raw AT %IW0 : DINT;
      END_VAR

      FUNCTION_BLOCK FB_Sensor
         VAR
            status AT %QW0 : WORD;
         END_VAR
      END_FUNCTION_BLOCK

      FUNCTION F_Util : INT
         VAR
            tmp AT %MW5 : INT;
         END_VAR
         F_Util := tmp;
      END_FUNCTION
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

   // Matches the CodeGenerator keying: globals are plain names, POU locals are
   // "POUName::varName", all normalized to uppercase.
   ASSERT_EQ(info.resolvedATAddresses.size(), 3U);
   EXPECT_EQ(info.resolvedATAddresses.at("G_RAW"), "%IW0");
   EXPECT_EQ(info.resolvedATAddresses.at("FB_SENSOR::STATUS"), "%QW0");
   EXPECT_EQ(info.resolvedATAddresses.at("F_UTIL::TMP"), "%MW5");
}

// ============================================================================
// Test 19 — Modular exhaustive equivalence: a program exercising enums, structs,
// an interface, FB inheritance + composition chain, a function and a program is
// generated in project mode with AND without semantics. The file set and every
// byte must be identical, and the base-class dependency selected via the
// semantic record (FB_ChainC inherits FB_ChainB).
// ============================================================================
TEST_F(CodegenSemanticTest, ModularExhaustiveSemanticVsLegacyByteIdentical)
{
   const std::string st = R"(
      VAR_GLOBAL
         g_status AT %MW0 : DINT;
      END_VAR

      TYPE S_Data : STRUCT
         a : DINT;
         b : REAL;
      END_STRUCT
      END_TYPE

      TYPE E_Color : (RED, GREEN, BLUE); END_TYPE

      INTERFACE I_Remote
         METHOD Execute : BOOL
            VAR_INPUT
               x : DINT;
            END_VAR
         END_METHOD
      END_INTERFACE

      FUNCTION_BLOCK FB_ChainA
         VAR
            s : S_Data;
            col : E_Color;
            cnt : DINT;
         END_VAR
         cnt := cnt + 1;
         s.a := cnt;
      END_FUNCTION_BLOCK

      FUNCTION_BLOCK FB_ChainB
         VAR
            a1 : FB_ChainA;
         END_VAR
         a1();
      END_FUNCTION_BLOCK

      FUNCTION_BLOCK FB_ChainC EXTENDS FB_ChainB
         VAR
            b1 : FB_ChainB;
            extra : INT;
         END_VAR
         b1();
         extra := 1;
      END_FUNCTION_BLOCK

      FUNCTION F_Calc : LREAL
         VAR_INPUT
            x : LREAL;
         END_VAR
         F_Calc := x * 2.0;
      END_FUNCTION

      PROGRAM ProgP
         VAR
            c : FB_ChainC;
            v : INT;
         END_VAR
         c();
         v := 0;
      END_PROGRAM
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   // Legacy generateModularProject (no SemanticInfo attached)
   CodeGenerator legacyGen;
   legacyGen.setRuntimeHeader("undoCore/types.hpp");
   legacyGen.setCaseSensitive(false);
   ProcessImageConfig pi;
   pi.inputBytes = 1024;
   pi.outputBytes = 1024;
   pi.markerBytes = 1024;
   pi.autoDetect = true;
   legacyGen.setProcessImageConfig(pi);
   auto legacyFiles = legacyGen.generateModularProject(tu, "/tmp/st2cpp_modular_exhaustive_legacy");

   // Semantic-driven generateModularProject
   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

   CodeGenerator semanticGen;
   semanticGen.setRuntimeHeader("undoCore/types.hpp");
   semanticGen.setCaseSensitive(false);
   semanticGen.setProcessImageConfig(pi);
   semanticGen.setSemanticInfo(&info);
   auto semanticFiles = semanticGen.generateModularProject(tu, "/tmp/st2cpp_modular_exhaustive_semantic");

   auto keyOf = [](const GeneratedFile& f) {
      return f.subdir + "::" + f.name + "::" + std::to_string(static_cast<int>(f.type));
   };

   std::map<std::string, std::string> legacyByKey;
   for (const auto& f : legacyFiles) legacyByKey[keyOf(f)] = f.content;
   std::map<std::string, std::string> semanticByKey;
   for (const auto& f : semanticFiles) semanticByKey[keyOf(f)] = f.content;

   ASSERT_EQ(legacyByKey.size(), semanticByKey.size())
       << "legacy file set: " << legacyByKey.size() << ", semantic file set: " << semanticByKey.size();

   for (const auto& [key, legacyContent] : legacyByKey) {
      auto it = semanticByKey.find(key);
      ASSERT_NE(it, semanticByKey.end()) << "semantic run missing file: " << key;
      EXPECT_EQ(legacyContent, it->second) << "content differs for file: " << key;
   }
   for (const auto& [key, semanticContent] : semanticByKey) {
      ASSERT_TRUE(legacyByKey.count(key)) << "semantic run produced extra file: " << key;
   }

   // The master FB include order must place the base before every derived FB.
   const int masterT = static_cast<int>(GenFileType::MASTER);
   const int headerT = static_cast<int>(GenFileType::HEADER);
   const auto masterIt = semanticByKey.find("::FunctionBlocks::" + std::to_string(masterT));
   ASSERT_NE(masterIt, semanticByKey.end());
   const std::string& master = masterIt->second;
   size_t basePos = master.find("FB_CHAINB.hpp");
   size_t derivedPos = master.find("FB_CHAINC.hpp");
   size_t firstPos = master.find("FB_CHAINA.hpp");
   EXPECT_NE(firstPos, std::string::npos);
   EXPECT_NE(basePos, std::string::npos);
   EXPECT_NE(derivedPos, std::string::npos);
   EXPECT_LT(firstPos, basePos);
   EXPECT_LT(basePos, derivedPos);

// The derived FB header must include its base via the semantic fbBaseClass.
    const auto chainCIter = semanticByKey.find("FunctionBlocks::FB_CHAINC::" + std::to_string(headerT));
    ASSERT_NE(chainCIter, semanticByKey.end());
    expectSource(chainCIter->second, "FB_CHAINB.hpp", "derived FB must include its semantic base");
}

// ============================================================================
// Forward reference: a struct whose member is an FB declared LATER in the ST.
// Analysis must succeed and the generated header must define the FB before the
// struct (complex structs with FB members are emitted after the function
// blocks), so the output is valid C++.
// ============================================================================
TEST_F(CodegenSemanticTest, StructContainsFBLaterIsDefinedAfterFb)
{
   const std::string st = R"(
      TYPE S_Report :
         STRUCT
            count : INT;
            ctrl  : FB_Counter;
         END_STRUCT
      END_TYPE

      FUNCTION_BLOCK FB_Counter
         VAR
            value : INT;
         END_VAR
      END_FUNCTION_BLOCK
   )";

   Lexer lexer(st, "<test>");
   auto tokens = lexer.tokenize();
   Parser parser(std::move(tokens));
   auto tu = parser.parseTranslationUnit();

   st2cpp::semantic::SemanticAnalyzer analyzer;
   auto info = analyzer.analyze(tu, st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive);
   EXPECT_FALSE(info.diagnostics.hasErrors()) << "Diagnostics: " << info.diagnostics.errorCount() << " errors";

   CodeGenerator gen;
   gen.setRuntimeHeader("undoCore/types.hpp");
   gen.setCaseSensitive(false);
   ProcessImageConfig pi;
   pi.inputBytes = 1024;
   pi.outputBytes = 1024;
   pi.markerBytes = 1024;
   pi.autoDetect = true;
   gen.setProcessImageConfig(pi);
   gen.setSemanticInfo(&info);

auto files = gen.generateModularProject(tu, "/tmp/st2cpp_struct_fb_later");
   // Concatenate every generated file in generation order: the FB header
   // (with `struct FB_COUNTER`) is emitted before the complex-struct header
   // GVLs.hpp (`struct S_REPORT`), which itself #includes FunctionBlocks.hpp.
   std::string all;
   for (const auto& file : files) {
      all += file.content + "\n";
   }

   size_t fbPos = all.find("struct FB_COUNTER");
   size_t structPos = all.find("struct S_REPORT");
   EXPECT_NE(fbPos, std::string::npos);
   EXPECT_NE(structPos, std::string::npos);
   EXPECT_LT(fbPos, structPos) << "FB_COUNTER must be defined before S_REPORT:\n" << all;
}

// ============================================================================
// Named type aliases and array type aliases must lower to the canonical C++
// type in the semantic-driven codegen (Issue: alias names were emitted verbatim).
// ============================================================================
TEST_F(CodegenSemanticTest, TypeAliasAndArrayAliasLowerToCanonicalType)
{
   const std::string st = R"(
      TYPE MyInt : INT; END_TYPE
      TYPE MyReal : REAL; END_TYPE
      TYPE MyCounter : ARRAY[0..9] OF INT; END_TYPE
      TYPE MyGrid : ARRAY[1..3, 1..4] OF REAL; END_TYPE
      TYPE Sensor : STRUCT
         id : MyInt;
         value : MyReal;
      END_STRUCT END_TYPE
      TYPE SensorArr : ARRAY[0..5] OF Sensor; END_TYPE

      PROGRAM Test
         VAR
            a : MyInt := 42;
            b : MyCounter;
            g : MyGrid;
            sa : SensorArr;
            s : Sensor;
         END_VAR
         b[0] := a;
         g[2, 3] := 1.5;
         sa[0].id := a;
         s.id := a;
      END_PROGRAM
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);

   // Program variables are emitted in the header in the semantic path.
   // Array alias of a scalar -> STArray<Int16, 0, 9>.
   expectSource(code.header, R"(STArray<Int16, 0, 9>\s+B)");
   // 2D array alias -> nested STArray.
   expectSource(code.header, R"(STArray<STArray<Float, 1, 4>, 1, 3>\s+G)");
   // Array of struct alias -> STArray<SENSOR, 0, 5>.
   expectSource(code.header, R"(STArray<SENSOR, 0, 5>\s+SA)");
   // Alias-to-scalar member inside the struct keeps the canonical type.
   expectSource(code.header, R"(Int16\s+ID)");
   // Struct member accesses through the alias and the real struct.
   expectSource(code.source, R"(B\[0\]\s*=\s*A)");
   expectSource(code.source, R"(SA\[0\]\.ID\s*=\s*A)");
   expectSource(code.source, R"(S\.ID\s*=\s*A)");

   // No alias spelling must leak into the generated code.
   EXPECT_EQ(code.source.find("MYCOUNTER"), std::string::npos)
       << "array alias name must not be emitted verbatim";
   EXPECT_EQ(code.header.find("MYCOUNTER"), std::string::npos)
       << "array alias name must not be emitted verbatim";
}

// ============================================================================
// FOR loops reusing a declared control variable must NOT declare it again with
// `auto`, keeping the existing variable; undeclared counters still get `auto`.
// ============================================================================
TEST_F(CodegenSemanticTest, ForLoopReusesDeclaredControlVariable)
{
   const std::string st = R"(
      PROGRAM Test
         VAR
            i : INT;
            sum : INT := 0;
         END_VAR
         sum := 0;
         FOR i := 0 TO 10 BY 2 DO
            sum := sum + i;
         END_FOR
      END_PROGRAM
   )";

   auto code = TestHelper::generateFromSTWithSemantics(st);

   // Declared control variable: no `auto` (would shadow the IEC variable).
   expectSource(code.source, R"(for\s*\(\s*I\s*=\s*0\s*;\s*I\s*<=\s*10\s*;\s*I\s*\+=\s*2\s*\))");
   EXPECT_EQ(code.source.find("for (auto I"), std::string::npos)
       << "declared control variable must not be redeclared with auto";
}