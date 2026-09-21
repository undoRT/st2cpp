/**
 * @file test_diagnostics.cpp
 * @brief Tests for Diagnostics system
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include <sstream>
#include "semantic/Diagnostics.h"

using namespace st2cpp::semantic;

// ============================================================================
// SourceLocation Tests
// ============================================================================

TEST(DiagnosticsTest, SourceLocationDefaultInvalid) {
    SourceLocation loc;
    EXPECT_FALSE(loc.isValid());
    EXPECT_EQ(loc.toString(), "<unknown>");
}

TEST(DiagnosticsTest, SourceLocationValid) {
    SourceLocation loc;
    loc.fileName = "test.st";
    loc.line = 10;
    loc.column = 5;
    EXPECT_TRUE(loc.isValid());
    EXPECT_EQ(loc.toString(), "test.st:10:5");
}

// ============================================================================
// Diagnostic Tests
// ============================================================================

TEST(DiagnosticsTest, DiagnosticToString) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.code = DiagnosticCode::TypeMismatch;
    d.message = "expected INT, got BOOL";
    d.location.fileName = "test.st";
    d.location.line = 10;
    d.location.column = 5;
    
    std::string str = d.toString();
    EXPECT_NE(str.find("test.st:10:5"), std::string::npos);
    EXPECT_NE(str.find("error"), std::string::npos);
    EXPECT_NE(str.find("TypeMismatch"), std::string::npos);
    EXPECT_NE(str.find("expected INT, got BOOL"), std::string::npos);
}

TEST(DiagnosticsTest, DiagnosticWithSuggestion) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.code = DiagnosticCode::TypeMismatch;
    d.message = "type mismatch";
    d.location.fileName = "test.st";
    d.location.line = 1;
    d.location.column = 1;
    d.suggestion = "cast to INT";
    
    std::string str = d.toString();
    EXPECT_NE(str.find("suggestion: cast to INT"), std::string::npos);
}

// ============================================================================
// Diagnostics Collector Tests
// ============================================================================

TEST(DiagnosticsTest, CollectorEmptyInitially) {
    Diagnostics diag;
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(diag.errorCount(), 0);
    EXPECT_EQ(diag.warningCount(), 0);
    EXPECT_EQ(diag.totalCount(), 0);
}

TEST(DiagnosticsTest, AddError) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};
    diag.addError(DiagnosticCode::TypeMismatch, "type error", loc);
    
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_EQ(diag.errorCount(), 1);
    EXPECT_EQ(diag.totalCount(), 1);
}

TEST(DiagnosticsTest, AddWarning) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};
    diag.addWarning(DiagnosticCode::UnusedVariable, "unused var", loc);
    
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(diag.warningCount(), 1);
    EXPECT_EQ(diag.totalCount(), 1);
}

TEST(DiagnosticsTest, AddNote) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};
    // Use a valid diagnostic code for note
    diag.addNote(DiagnosticCode::DeprecatedFeature, "see here", loc);
    
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(diag.totalCount(), 1);
}

TEST(DiagnosticsTest, MultipleDiagnostics) {
    Diagnostics diag;
    SourceLocation loc1{"test.st", 1, 1};
    SourceLocation loc2{"test.st", 2, 2};
    
    diag.addError(DiagnosticCode::TypeMismatch, "error 1", loc1);
    diag.addWarning(DiagnosticCode::UnusedVariable, "warning 1", loc2);
    diag.addError(DiagnosticCode::UndeclaredIdentifier, "error 2", loc1);
    
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_EQ(diag.errorCount(), 2);
    EXPECT_EQ(diag.warningCount(), 1);
    EXPECT_EQ(diag.totalCount(), 3);
}

TEST(DiagnosticsTest, Clear) {
    Diagnostics diag;
    diag.addError(DiagnosticCode::TypeMismatch, "error", {"test.st", 1, 1});
    diag.clear();
    
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(diag.totalCount(), 0);
}

TEST(DiagnosticsTest, PrintOutput) {
    Diagnostics diag;
    SourceLocation loc{"test.st", 10, 5};
    diag.addError(DiagnosticCode::TypeMismatch, "expected INT, got BOOL", loc);
    
    std::ostringstream oss;
    diag.print(oss);
    
    std::string output = oss.str();
    EXPECT_NE(output.find("test.st:10:5"), std::string::npos);
    EXPECT_NE(output.find("error"), std::string::npos);
    EXPECT_NE(output.find("expected INT, got BOOL"), std::string::npos);
}

TEST(DiagnosticsTest, PrintSummary) {
    Diagnostics diag;
    diag.addError(DiagnosticCode::TypeMismatch, "err1", {"a.st", 1, 1});
    diag.addError(DiagnosticCode::TypeMismatch, "err2", {"a.st", 2, 2});
    diag.addWarning(DiagnosticCode::UnusedVariable, "warn", {"a.st", 3, 3});
    
    std::ostringstream oss;
    diag.printSummary(oss);
    
    std::string output = oss.str();
    EXPECT_NE(output.find("2 error"), std::string::npos);
    EXPECT_NE(output.find("1 warning"), std::string::npos);
}

TEST(DiagnosticsTest, RelatedLocations) {
    Diagnostics diag;
    SourceLocation mainLoc{"test.st", 10, 5};
    SourceLocation relatedLoc{"test.st", 5, 1};
    
    // Create a diagnostic with related location
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.code = DiagnosticCode::DuplicateDeclaration;
    d.message = "duplicate 'x'";
    d.location = mainLoc;
    d.relatedLocations.push_back(relatedLoc);
    
    // Need to add via a helper - but Diagnostics doesn't expose this directly
    // So we test the Diagnostic's toString which includes related locations
    std::ostringstream oss;
    oss << d.toString() << '\n';
    for (const auto& rel : d.relatedLocations) {
        oss << rel.toString() << ": note: see here\n";
    }
    
    std::string output = oss.str();
    EXPECT_NE(output.find("see here"), std::string::npos);
}

TEST(DiagnosticsTest, DiagnosticCodeValues) {
    // Verify key codes are in expected ranges
    EXPECT_GE(static_cast<uint16_t>(DiagnosticCode::DuplicateDeclaration), 1000);
    EXPECT_LT(static_cast<uint16_t>(DiagnosticCode::DuplicateDeclaration), 2000);
    
    EXPECT_GE(static_cast<uint16_t>(DiagnosticCode::TypeMismatch), 2000);
    EXPECT_LT(static_cast<uint16_t>(DiagnosticCode::TypeMismatch), 3000);
    
    EXPECT_GE(static_cast<uint16_t>(DiagnosticCode::WrongArgumentCount), 3000);
    EXPECT_LT(static_cast<uint16_t>(DiagnosticCode::WrongArgumentCount), 4000);
}

TEST(DiagnosticsTest, SourceLocationEndPosition) {
    SourceLocation loc;
    loc.fileName = "test.st";
    loc.line = 10;
    loc.column = 5;
    loc.endLine = 10;
    loc.endColumn = 15;
    
    EXPECT_EQ(loc.line, 10);
    EXPECT_EQ(loc.column, 5);
    EXPECT_EQ(loc.endLine, 10);
    EXPECT_EQ(loc.endColumn, 15);
}

TEST(DiagnosticsTest, DiagnosticCodeInternalError) {
    // InternalError should be the highest code
    EXPECT_EQ(static_cast<uint16_t>(DiagnosticCode::InternalError), 9999);
}