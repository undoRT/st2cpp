/**
 * @file test_case_sensitivity.cpp
 * @brief Tests for the --caseSensitive identifier policy
 * @details Covers the three modes the policy has to keep apart: the default
 * IEC 61131-3 case-insensitive resolution, --caseSensitive in permissive mode
 * (a mis-cased reference still resolves, but warns), and --caseSensitive with
 * --strict, where a mis-cased reference is undeclared exactly as a C++ compiler
 * would reject it. Also covers the elementary type spellings, which the parser
 * folds to a BaseType enumerator, and the IEC base functions.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include "semantic/SemanticAnalyzer.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "ast/AST.h"

using namespace st2cpp::semantic;

class CaseSensitivityTest : public ::testing::Test {
protected:
    SemanticInfo analyze(const std::string& st, bool caseSensitive,
                         SemanticAnalyzer::Strictness strictness) {
        auto tu = TestHelper::parseST(st);
        SemanticAnalyzer analyzer;
        analyzer.setCaseSensitive(caseSensitive);
        return analyzer.analyze(tu, strictness);
    }

    // Default policy: IEC 61131-3, case-insensitive.
    SemanticInfo analyzeDefault(const std::string& st) {
        return analyze(st, false, SemanticAnalyzer::Strictness::Strict);
    }
    SemanticInfo analyzeSensitiveStrict(const std::string& st) {
        return analyze(st, true, SemanticAnalyzer::Strictness::Strict);
    }
    SemanticInfo analyzeSensitivePermissive(const std::string& st) {
        return analyze(st, true, SemanticAnalyzer::Strictness::Permissive);
    }

    static bool hasCode(const Diagnostics& d, DiagnosticCode code) {
        for (const auto& diag : d.all()) {
            if (diag.code == code) {
                return true;
            }
        }
        return false;
    }

    static bool hasCodeWithMessage(const Diagnostics& d, DiagnosticCode code, const std::string& needle) {
        for (const auto& diag : d.all()) {
            if (diag.code == code && diag.message.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

namespace {

const char* const kMismatchVar = R"(
    FUNCTION_BLOCK Test
        VAR
            name : INT;
        END_VAR
        name := 1;
        Name := 2;
    END_FUNCTION_BLOCK
)";

const char* const kElementaryType = R"(
    FUNCTION_BLOCK Test
        VAR
            b : dint;
        END_VAR
        b := 1;
    END_FUNCTION_BLOCK
)";

const char* const kBaseFunction = R"(
    FUNCTION_BLOCK Test
        VAR
            c : DINT;
        END_VAR
        c := int_to_dint(5);
    END_FUNCTION_BLOCK
)";

const char* const kMatchingCase = R"(
    FUNCTION_BLOCK Test
        VAR
            name : INT;
        END_VAR
        name := 1;
    END_FUNCTION_BLOCK
)";

} // namespace

// ============================================================================
// The default policy is unchanged: IEC 61131-3 folds identifiers to uppercase
// ============================================================================

TEST_F(CaseSensitivityTest, DefaultFoldsVariableCase) {
    auto info = analyzeDefault(kMismatchVar);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
    EXPECT_EQ(info.diagnostics.warningCount(), 0);
}

TEST_F(CaseSensitivityTest, DefaultFoldsElementaryTypeCase) {
    auto info = analyzeDefault(kElementaryType);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
    EXPECT_EQ(info.diagnostics.warningCount(), 0);
}

TEST_F(CaseSensitivityTest, DefaultFoldsBaseFunctionCase) {
    auto info = analyzeDefault(kBaseFunction);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
}

// ============================================================================
// --caseSensitive --strict: a mis-cased reference is undeclared, like C++
// ============================================================================

TEST_F(CaseSensitivityTest, StrictRejectsMisCasedVariable) {
    auto info = analyzeSensitiveStrict(kMismatchVar);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::UndeclaredIdentifier, "Name"))
        << "expected the mis-cased reference to be reported as undeclared";
}

TEST_F(CaseSensitivityTest, StrictAcceptsExactSpelling) {
    auto info = analyzeSensitiveStrict(kMatchingCase);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
    EXPECT_EQ(info.diagnostics.warningCount(), 0);
}

TEST_F(CaseSensitivityTest, StrictRejectsMisCasedElementaryType) {
    auto info = analyzeSensitiveStrict(kElementaryType);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::InvalidTypeName, "dint"))
        << "expected the mis-cased elementary type to be reported";
}

TEST_F(CaseSensitivityTest, StrictRejectsMisCasedBaseFunction) {
    auto info = analyzeSensitiveStrict(kBaseFunction);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::UndeclaredIdentifier, "int_to_dint"))
        << "expected the mis-cased IEC conversion function to be reported";
}

TEST_F(CaseSensitivityTest, StrictAcceptsCanonicalElementaryType) {
    const char* st = R"(
        FUNCTION_BLOCK Test
            VAR
                b : DINT;
            END_VAR
            b := 1;
        END_FUNCTION_BLOCK
    )";
    auto info = analyzeSensitiveStrict(st);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
}

// ============================================================================
// --caseSensitive permissive: the reference still resolves, but warns
// ============================================================================

TEST_F(CaseSensitivityTest, PermissiveResolvesMisCasedVariableWithWarning) {
    auto info = analyzeSensitivePermissive(kMismatchVar);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "a mis-cased reference must still resolve in permissive mode";
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::CaseMismatch, "'Name'"))
        << "expected a CaseMismatch warning naming the written spelling";
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::CaseMismatch, "'name'"))
        << "expected the warning to name the declaration it resolved to";
}

TEST_F(CaseSensitivityTest, PermissiveWarnsOnMisCasedElementaryType) {
    auto info = analyzeSensitivePermissive(kElementaryType);
    EXPECT_FALSE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCode(info.diagnostics, DiagnosticCode::CaseMismatch))
        << "expected a CaseMismatch warning for the elementary type";
}

TEST_F(CaseSensitivityTest, PermissiveIsSilentOnExactSpelling) {
    auto info = analyzeSensitivePermissive(kMatchingCase);
    EXPECT_EQ(info.diagnostics.warningCount(), 0)
        << "an exact spelling must not produce a warning";
}

// ============================================================================
// Names declared only once still resolve under every policy
// ============================================================================

TEST_F(CaseSensitivityTest, MixedCaseUseOfOneDeclarationResolves) {
    const char* st = R"(
        FUNCTION_BLOCK Test
            VAR
                Counter : INT;
            END_VAR
            Counter := 1;
        END_FUNCTION_BLOCK
    )";
    for (int policy = 0; policy < 3; ++policy) {
        SemanticAnalyzer analyzer;
        analyzer.setCaseSensitive(policy != 0);
        auto tu = TestHelper::parseST(st);
        auto info = analyzer.analyze(tu, policy == 2 ? SemanticAnalyzer::Strictness::Strict
                                                    : SemanticAnalyzer::Strictness::Permissive);
        EXPECT_FALSE(info.diagnostics.hasErrors()) << "policy " << policy;
        EXPECT_EQ(info.diagnostics.warningCount(), 0) << "policy " << policy;
    }
}

// ============================================================================
// Struct members and function names follow the same policy
// ============================================================================

TEST_F(CaseSensitivityTest, StrictRejectsMisCasedStructMember) {
    const char* st = R"(
        TYPE Point : STRUCT
            field : INT;
        END_STRUCT
        END_TYPE
        FUNCTION_BLOCK Test
            VAR
                p : Point;
            END_VAR
            p.field := 1;
            p.Field := 2;
        END_FUNCTION_BLOCK
    )";
    auto info = analyzeSensitiveStrict(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::MissingStructMember, "Field"))
        << "expected the mis-cased struct member to be reported";
}

TEST_F(CaseSensitivityTest, StrictAcceptsExactStructMember) {
    const char* st = R"(
        TYPE Point : STRUCT
            field : INT;
        END_STRUCT
        END_TYPE
        FUNCTION_BLOCK Test
            VAR
                p : Point;
            END_VAR
            p.field := 1;
        END_FUNCTION_BLOCK
    )";
    auto info = analyzeSensitiveStrict(st);
    EXPECT_FALSE(info.diagnostics.hasErrors())
        << "Diagnostics: " << info.diagnostics.errorCount() << " errors";
}

TEST_F(CaseSensitivityTest, StrictRejectsMisCasedFunctionName) {
    const char* st = R"(
        FUNCTION Compute : INT
        VAR_INPUT
            x : INT;
        END_VAR
            Compute := x;
        END_FUNCTION
        FUNCTION_BLOCK Test
            VAR
                r : INT;
            END_VAR
            r := Compute(1);
            r := compute(1);
        END_FUNCTION_BLOCK
    )";
    auto info = analyzeSensitiveStrict(st);
    EXPECT_TRUE(info.diagnostics.hasErrors());
    EXPECT_TRUE(hasCodeWithMessage(info.diagnostics, DiagnosticCode::UndeclaredIdentifier, "compute"))
        << "expected the mis-cased function reference to be reported";
}

// ============================================================================
// The table keys and the policy predicates themselves
// ============================================================================

TEST_F(CaseSensitivityTest, NormalizeKeyFollowsPolicy) {
    SymbolTable tab;
    EXPECT_EQ(tab.normalizeKey("name"), "NAME");
    tab.setCaseSensitive(true);
    EXPECT_EQ(tab.normalizeKey("name"), "name");
    EXPECT_EQ(tab.normalizeKey("NAME"), "NAME");
}

TEST_F(CaseSensitivityTest, CaseSensitiveDeclarationsAreDistinct) {
    SymbolTable tab;
    tab.setCaseSensitive(true);
    tab.setCaseFallback(false);
    const SymbolId lower = tab.declare("name", SymbolKind::Variable, 1);
    const SymbolId upper = tab.declare("NAME", SymbolKind::Variable, 1);
    EXPECT_NE(lower, 0u);
    EXPECT_NE(upper, 0u);
    EXPECT_NE(lower, upper) << "differing case must be two distinct symbols";
}

TEST_F(CaseSensitivityTest, NameMatchesFollowsPolicy) {
    SymbolTable tab;
    EXPECT_TRUE(tab.nameMatches("name", "NAME"));
    tab.setCaseSensitive(true);
    tab.setCaseFallback(false);
    EXPECT_FALSE(tab.nameMatches("name", "NAME")) << "strict: case matters";
    tab.setCaseFallback(true);
    EXPECT_TRUE(tab.nameMatches("name", "NAME")) << "permissive: case still resolves";
}

TEST_F(CaseSensitivityTest, AmbiguousCaseFallbackDoesNotGuess) {
    // Two declarations differing only by case make the fallback ambiguous, so
    // the reference must fail instead of resolving to an arbitrary one.
    SymbolTable tab;
    tab.setCaseSensitive(true);
    tab.setCaseFallback(true);
    tab.declare("name", SymbolKind::Variable, 1);
    tab.declare("NAME", SymbolKind::Variable, 1);
    EXPECT_EQ(tab.lookupGlobal("Name"), 0u);
}
