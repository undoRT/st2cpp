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
#include <map>
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
    ExternalSymbolCollision      = 1010, // same symbol exported by two libraries
    ExternalBindingIncomplete    = 1011, // external symbol whose C++ binding is malformed
    
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
    InvalidTimeLiteral           = 9005,
    SyntaxError                  = 9006,
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
        case DiagnosticCode::ExternalSymbolCollision:      return "ExternalSymbolCollision";
        case DiagnosticCode::ExternalBindingIncomplete:    return "ExternalBindingIncomplete";
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
        case DiagnosticCode::InvalidTimeLiteral:           return "InvalidTimeLiteral";
        case DiagnosticCode::SyntaxError:                  return "SyntaxError";
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
            printDiagnostic(out, d);
        }
        if (!diagnostics_.empty()) {
            printSummary(out);
        }
    }
    
    // Print summary
    void printSummary(std::ostream& out) const {
        const size_t errors = errorCount();
        const size_t warnings = warningCount();
        if (errors > 0 && warnings > 0) {
            out << errors << " error(s), " << warnings << " warning(s) generated.\n";
        } else if (errors > 0) {
            out << errors << (errors == 1 ? " error generated.\n" : " errors generated.\n");
        } else if (warnings > 0) {
            out << warnings << (warnings == 1 ? " warning generated.\n" : " warnings generated.\n");
        }
    }

    // Source context used for pretty printing: the real file name and the raw
    // text of the analyzed source (needed to render the code snippet/caret).
    void setSourceName(const std::string& name) { sourceName_ = name; }
    const std::string& sourceFileName() const { return sourceName_; }
    void setSourceText(const std::string& text) { sourceText_ = text; }
    const std::string& sourceText() const { return sourceText_; }

    // Register an additional source file so that diagnostics pointing into it
    // can render their snippet. Workspace analysis spans many .st files, so a
    // single sourceText is not enough: a location names its own file and the
    // snippet must be cut from that file, not from the primary one.
    void addSourceFile(const std::string& name, const std::string& text) {
        if (!name.empty()) {
            sources_[name] = text;
        }
    }

    // Resolve the text of the file a location points into, falling back to the
    // primary source when the location carries no file name.
    std::string textFor(const SourceLocation& loc) const {
        if (!loc.fileName.empty()) {
            auto it = sources_.find(loc.fileName);
            if (it != sources_.end()) return it->second;
        }
        if (!sourceText_.empty()) return sourceText_;
        auto it = sources_.find(sourceName_);
        return it != sources_.end() ? it->second : std::string();
    }

private:
    std::vector<Diagnostic> diagnostics_;
    std::map<std::string, std::string> sources_;
    std::string sourceName_ = "<input>";
    std::string sourceText_;

    static std::string severityName(DiagnosticSeverity sev) {
        switch (sev) {
            case DiagnosticSeverity::Error:   return "error";
            case DiagnosticSeverity::Warning: return "warning";
            case DiagnosticSeverity::Note:    return "note";
        }
        return "note";
    }

    // Extract the raw text of a 1-based source line.
    static std::string sourceLine(const std::string& text, uint32_t line) {
        if (text.empty() || line == 0) return "";
        size_t start = 0;
        for (uint32_t i = 1; i < line; ++i) {
            size_t nl = text.find('\n', start);
            if (nl == std::string::npos) return "";
            start = nl + 1;
        }
        size_t end = text.find('\n', start);
        std::string l = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        return l;
    }

    // Expand tabs to spaces so the source line and its caret stay aligned.
    static std::string expandTabs(const std::string& src) {
        std::string out;
        size_t disp = 0;
        for (char c : src) {
            if (c == '\t') {
                const size_t spaces = 4 - (disp % 4);
                out.append(spaces, ' ');
                disp += spaces;
            } else {
                out += c;
                ++disp;
            }
        }
        return out;
    }

    // Build the caret underline for a 1-based column (optionally spanning to
    // endColumn). Tabs before the column are taken into account.
    static std::string caretLine(const std::string& src, uint32_t col, uint32_t endColumn) {
        if (col == 0) return "";
        size_t disp = 0; // display column of `col` in the expanded line
        size_t idx = 0;
        while (idx < src.size() && idx + 1 < static_cast<size_t>(col)) {
            if (src[idx] == '\t') {
                disp = (disp / 4 + 1) * 4;
            } else {
                ++disp;
            }
            ++idx;
        }
        size_t span = 1;
        if (endColumn > col) {
            span = static_cast<size_t>(endColumn) - col;
        }
        return std::string(disp, ' ') + std::string(span, '^');
    }

    void printDiagnostic(std::ostream& out, const Diagnostic& d) const {
        // Header line: file:line:col: severity: message [Code]
        // A location that names no file falls back to the primary source name,
        // otherwise the header would start with a bare ":".
        const std::string displayFile =
            !d.location.fileName.empty() ? d.location.fileName
            : (!sourceName_.empty()        ? sourceName_
                                         : std::string("<input>"));
        if (d.location.isValid()) {
            out << displayFile << ':' << d.location.line << ':' << d.location.column << ": ";
        } else {
            out << displayFile << ": ";
        }
        out << severityName(d.severity) << ": " << d.message
            << " [" << diagnosticCodeToString(d.code) << "]\n";

        // Code snippet with a caret (only when a source position and the text
        // of the file that location points into are both available).
        const std::string text = textFor(d.location);
        if (d.location.isValid() && d.location.column > 0 && !text.empty()) {
            const std::string line = expandTabs(sourceLine(text, d.location.line));
            if (!line.empty()) {
                const std::string num = std::to_string(d.location.line);
                const std::string pad(num.size(), ' ');
                const std::string caret
                    = caretLine(line, d.location.column, d.location.endColumn);
                out << "  " << num << " | " << line << '\n';
                out << "  " << pad << " | " << caret << '\n';
            }
        }
        for (const auto& rel : d.relatedLocations) {
            out << rel.toString() << ": note: see here\n";
        }
        if (!d.suggestion.empty()) {
            out << "  suggestion: " << d.suggestion << '\n';
        }
    }
};

} // namespace st2cpp::semantic