/**
 * @file Diagnostics.h
 * @brief Diagnostic reporting system for semantic analysis
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <string>
#include <vector>
#include <ostream>
#include <cstdint>

namespace st2cpp::semantic {

/**
 * @brief Severity level of a diagnostic
 */
enum class DiagnosticSeverity {
    Error,   // Blocks code generation
    Warning, // Does not block, but signals potential issue
    Note     // Informational (e.g., "see declaration here")
};

/**
 * @brief Standardized diagnostic codes
 * 
 * Organized by category ranges:
 *  1000-1999: Declaration errors
 *  2000-2999: Type errors
 *  3000-3999: Call/argument errors
 *  4000-4999: Control flow errors
 *  5000-5999: Operator errors
 *  6000-6999: Struct/Array errors
 *  7000-7999: Pointer/Reference errors
 *  8000-8999: Inheritance/Interface errors
 *  9000-9999: Other/General
 */
enum class DiagnosticCode : uint16_t {
    // Declaration errors (1000-1999)
    DuplicateDeclaration         = 1001,
    UndeclaredIdentifier         = 1002,
    InvalidTypeName              = 1003,
    CircularInheritance          = 1004,
    InvalidExtends               = 1005,
    InvalidImplements            = 1006,
    DuplicateEnumValue           = 1007,
    MissingStructMember          = 1008,
    CircularDependency           = 1009,
    
    // Type errors (2000-2999)
    TypeMismatch                 = 2001,
    IncompatibleTypes            = 2002,
    InvalidAssignment            = 2003,
    InvalidArgumentType          = 2004,
    InvalidReturnType            = 2005,
    InvalidArrayIndex            = 2006,
    InvalidStructMember          = 2007,
    InvalidEnumerator            = 2008,
    NonBooleanCondition          = 2009,
    InvalidForControlVariable    = 2010,
    ForBoundsTypeMismatch        = 2011,
    CaseSelectorTypeMismatch     = 2012,
    CaseDuplicateValue           = 2013,
    
    // Call/argument errors (3000-3999)
    WrongArgumentCount           = 3001,
    MissingRequiredArgument      = 3002,
    DuplicateNamedArgument       = 3003,
    UnknownNamedArgument         = 3004,
    OutputArgumentNotLValue      = 3005,
    InOutArgumentNotLValue       = 3006,
    
    // Control flow (4000-4999)
    // Reserved
    
    // Operator errors (5000-5999)
    InvalidUnaryOperand          = 5001,
    InvalidBinaryOperands        = 5002,
    DivisionByZero               = 5003,
    
    // Struct/Array (6000-6999)
    DuplicateStructMemberInit    = 6001,
    ArrayDimensionMismatch       = 6002,
    ArrayBoundsNotConstant       = 6003,
    
    // Pointer/Reference (7000-7999)
    InvalidPointerDereference    = 7001,
    InvalidRefToUsage            = 7002,
    AdrOfNonLValue               = 7003,
    
    // Inheritance/Interface (8000-8999)
    OverrideMismatch             = 8001,
    FinalMethodOverride          = 8002,
    AbstractMethodNotImplemented = 8003,
    SuperCallNoBase              = 8004,
    
    // Other (9000-9999)
    UninitializedVariable        = 9001,
    UnusedVariable               = 9002,
    DeprecatedFeature            = 9003,
    UnsupportedConstruct         = 9004,
    InternalError                = 9999
};

/**
 * @brief Convert DiagnosticCode to string for display
 */
inline std::string diagnosticCodeToString(DiagnosticCode code) {
    switch (code) {
        case DiagnosticCode::DuplicateDeclaration:         return "DuplicateDeclaration";
        case DiagnosticCode::UndeclaredIdentifier:         return "UndeclaredIdentifier";
        case DiagnosticCode::InvalidTypeName:              return "InvalidTypeName";
        case DiagnosticCode::CircularInheritance:          return "CircularInheritance";
        case DiagnosticCode::InvalidExtends:               return "InvalidExtends";
        case DiagnosticCode::InvalidImplements:            return "InvalidImplements";
        case DiagnosticCode::DuplicateEnumValue:           return "DuplicateEnumValue";
        case DiagnosticCode::MissingStructMember:          return "MissingStructMember";
        case DiagnosticCode::CircularDependency:           return "CircularDependency";
        case DiagnosticCode::TypeMismatch:                 return "TypeMismatch";
        case DiagnosticCode::IncompatibleTypes:            return "IncompatibleTypes";
        case DiagnosticCode::InvalidAssignment:            return "InvalidAssignment";
        case DiagnosticCode::InvalidArgumentType:          return "InvalidArgumentType";
        case DiagnosticCode::InvalidReturnType:            return "InvalidReturnType";
        case DiagnosticCode::InvalidArrayIndex:            return "InvalidArrayIndex";
        case DiagnosticCode::InvalidStructMember:          return "InvalidStructMember";
        case DiagnosticCode::InvalidEnumerator:            return "InvalidEnumerator";
        case DiagnosticCode::NonBooleanCondition:          return "NonBooleanCondition";
        case DiagnosticCode::InvalidForControlVariable:    return "InvalidForControlVariable";
        case DiagnosticCode::ForBoundsTypeMismatch:        return "ForBoundsTypeMismatch";
        case DiagnosticCode::CaseSelectorTypeMismatch:     return "CaseSelectorTypeMismatch";
        case DiagnosticCode::CaseDuplicateValue:           return "CaseDuplicateValue";
        case DiagnosticCode::WrongArgumentCount:           return "WrongArgumentCount";
        case DiagnosticCode::MissingRequiredArgument:      return "MissingRequiredArgument";
        case DiagnosticCode::DuplicateNamedArgument:       return "DuplicateNamedArgument";
        case DiagnosticCode::UnknownNamedArgument:         return "UnknownNamedArgument";
        case DiagnosticCode::OutputArgumentNotLValue:      return "OutputArgumentNotLValue";
        case DiagnosticCode::InOutArgumentNotLValue:       return "InOutArgumentNotLValue";
        case DiagnosticCode::InvalidUnaryOperand:          return "InvalidUnaryOperand";
        case DiagnosticCode::InvalidBinaryOperands:        return "InvalidBinaryOperands";
        case DiagnosticCode::DivisionByZero:               return "DivisionByZero";
        case DiagnosticCode::DuplicateStructMemberInit:    return "DuplicateStructMemberInit";
        case DiagnosticCode::ArrayDimensionMismatch:       return "ArrayDimensionMismatch";
        case DiagnosticCode::ArrayBoundsNotConstant:       return "ArrayBoundsNotConstant";
        case DiagnosticCode::InvalidPointerDereference:    return "InvalidPointerDereference";
        case DiagnosticCode::InvalidRefToUsage:            return "InvalidRefToUsage";
        case DiagnosticCode::AdrOfNonLValue:               return "AdrOfNonLValue";
        case DiagnosticCode::OverrideMismatch:             return "OverrideMismatch";
        case DiagnosticCode::FinalMethodOverride:          return "FinalMethodOverride";
        case DiagnosticCode::AbstractMethodNotImplemented: return "AbstractMethodNotImplemented";
        case DiagnosticCode::SuperCallNoBase:              return "SuperCallNoBase";
        case DiagnosticCode::UninitializedVariable:        return "UninitializedVariable";
        case DiagnosticCode::UnusedVariable:               return "UnusedVariable";
        case DiagnosticCode::DeprecatedFeature:            return "DeprecatedFeature";
        case DiagnosticCode::UnsupportedConstruct:         return "UnsupportedConstruct";
        case DiagnosticCode::InternalError:                return "InternalError";
        default: return "UnknownCode(" + std::to_string(static_cast<uint16_t>(code)) + ")";
    }
}

/**
 * @brief Source location information
 */
struct SourceLocation {
    std::string fileName;
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t endLine = 0;
    uint32_t endColumn = 0;
    
    bool isValid() const { return line != 0; }
    
    std::string toString() const {
        if (!isValid()) return "<unknown>";
        std::string result = fileName;
        result += ':' + std::to_string(line);
        result += ':' + std::to_string(column);
        return result;
    }
};

/**
 * @brief Single diagnostic entry
 */
struct Diagnostic {
    DiagnosticSeverity severity;
    DiagnosticCode code;
    std::string message;
    SourceLocation location;
    std::vector<SourceLocation> relatedLocations; // e.g., "see declaration here"
    std::string suggestion; // Optional fix hint
    
    std::string toString() const {
        std::string sev;
        switch (severity) {
            case DiagnosticSeverity::Error:   sev = "error"; break;
            case DiagnosticSeverity::Warning: sev = "warning"; break;
            case DiagnosticSeverity::Note:    sev = "note"; break;
        }
        std::string result = location.toString() + ": " + sev + ": [" + diagnosticCodeToString(code) + "] " + message;
        if (!suggestion.empty()) {
            result += " (suggestion: " + suggestion + ")";
        }
        return result;
    }
};

/**
 * @brief Diagnostic collector and reporter
 * 
 * Accumulates diagnostics during semantic analysis.
 * Supports multiple errors per run (no early exit).
 */
class Diagnostics {
public:
    // Add diagnostics
    void addError(DiagnosticCode code, const std::string& message, const SourceLocation& location) {
        diagnostics_.push_back({DiagnosticSeverity::Error, code, message, location, {}, {}});
    }
    
    void addWarning(DiagnosticCode code, const std::string& message, const SourceLocation& location) {
        diagnostics_.push_back({DiagnosticSeverity::Warning, code, message, location, {}, {}});
    }
    
    void addNote(DiagnosticCode code, const std::string& message, const SourceLocation& location) {
        diagnostics_.push_back({DiagnosticSeverity::Note, code, message, location, {}, {}});
    }
    
    // Generic add with explicit severity
    void add(DiagnosticSeverity severity, DiagnosticCode code, const std::string& message, const SourceLocation& location) {
        diagnostics_.push_back({severity, code, message, location, {}, {}});
    }
    
    // Query
    bool hasErrors() const {
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::Error) return true;
        }
        return false;
    }
    
    size_t errorCount() const {
        size_t count = 0;
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::Error) ++count;
        }
        return count;
    }
    
    size_t warningCount() const {
        size_t count = 0;
        for (const auto& d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::Warning) ++count;
        }
        return count;
    }
    
    size_t totalCount() const { return diagnostics_.size(); }
    
    const std::vector<Diagnostic>& all() const { return diagnostics_; }
    
    // Clear all diagnostics
    void clear() { diagnostics_.clear(); }
    
    // Print formatted output (GCC/Clang style)
    void print(std::ostream& out) const {
        for (const auto& d : diagnostics_) {
            out << d.toString() << '\n';
            for (const auto& rel : d.relatedLocations) {
                out << rel.toString() << ": note: see here\n";
            }
            if (!d.suggestion.empty()) {
                out << "  suggestion: " << d.suggestion << '\n';
            }
        }
    }
    
    // Print summary
    void printSummary(std::ostream& out) const {
        out << "Diagnostics: " << errorCount() << " error(s), " << warningCount() << " warning(s)\n";
    }

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace st2cpp::semantic