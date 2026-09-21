/**
 * @file BodyVisitor.h
 * @brief Body visitor: name resolution and AST decoration for statement/expression bodies
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "ast/AST.h"
#include <vector>
#include <string>
#include <optional>
#include <unordered_map>

namespace st2cpp::semantic {

/**
 * @brief Visitor for body analysis phase (Sprint 2B)
 * 
 * Second pass: resolves names and decorates AST with semantic information.
 * Handles: name resolution, shadowing, scope traversal.
 * Does NOT perform type checking of expressions/statements (Sprint 3).
 * 
 * AST Decoration Model:
 * - Expr::symbolId is populated with the resolved SymbolId
 * - Expr::resolvedTypeId is populated with the resolved TypeId
 * - IdentExpr::symbolId is populated with the resolved SymbolId
 * - CallExpr::calleeSymbolId is populated with the resolved callee SymbolId
 * - MemberExpr::symbolId is populated when type information is available (Sprint 2C)
 * 
 * The AST is decorated in-place during traversal, making the results
 * directly accessible to downstream phases (Sprint 2C+, CodeGenerator).
 */
class BodyVisitor {
public:
    BodyVisitor(SymbolTable& symTab, Diagnostics& diag);
    
    /**
     * @brief Enable IEC-strict analysis mode
     *
     * In Strict mode the visitor enforces the IEC 61131-3 implicit-conversion
     * rules (isImplicitlyConvertibleFrom) as diagnostics: value-losing or
     * cross-family implicit assignments and binary operands are reported.
     * In Permissive mode (the default) the legacy permissive baseline is
     * preserved byte-for-byte, so this toggle NEVER alters emitted code.
     *
     * @param strict True to enable strict IEC checks
     */
    void setStrict(bool strict) { strict_ = strict; }

    bool isStrict() const { return strict_; }
    
    // Main entry points
    void visitTranslationUnit(const TranslationUnit& tu);
    
    // Accessors for statistics/debugging
    size_t getResolvedCount() const { return resolvedCount_; }
    size_t getUnresolvedCount() const { return unresolvedCount_; }

 private:
    SymbolTable& symTab_;
    Diagnostics& diag_;
    
    // Statistics
    size_t resolvedCount_ = 0;
    size_t unresolvedCount_ = 0;
    
    // Context stack for scope management
    struct Context {
        SymbolId scopeId = 0;
        SymbolId pouId = 0;
        SymbolId methodId = 0;
        bool inFunctionBody = false;
        TypeId returnTypeId = 0;
        int loopDepth = 0;
        bool inLoop = false;
    };
    std::vector<Context> contextStack_;
    
    // --- Return tracking ---
    bool hasReturn_ = false;
    
    // --- IEC strictness (Fase 6) ---
    bool strict_ = false;
    
    // --- POU Body Visiting ---
    void visitPouBody(const POU& pou);
    void visitMethodBody(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId);
    
    // --- Statement Visiting ---
    void visitStatementList(const std::vector<std::shared_ptr<Stmt>>& stmts);
    void visitStatement(const Stmt& stmt);
    void visitAssignStmt(const AssignStmt& stmt, uint32_t line);
    void visitExprStmt(const ExprStmt& stmt);
    void visitIfStmt(const IfStmt& stmt);
    void visitForStmt(const ForStmt& stmt, uint32_t line);
    void visitWhileStmt(const WhileStmt& stmt);
    void visitRepeatStmt(const RepeatStmt& stmt);
    void visitCaseStmt(const CaseStmt& stmt);
    void visitReturnStmt(const ReturnStmt& stmt);
    void visitExitStmt(const ExitStmt& stmt);
    void visitContinueStmt(const ContinueStmt& stmt);
    void visitEmptyStmt(const EmptyStmt& stmt);
    
    // --- Expression Visiting ---
    void visitExpression(Expr& expr);
    void visitIdentExpr(IdentExpr& expr);
    void visitMemberExpr(Expr& expr);
    void visitIndexExpr(Expr& expr);
    void visitBinaryExpr(Expr& expr);
    void visitUnaryExpr(Expr& expr);
    void visitCallExpr(Expr& expr);
    void visitSuperCallExpr(SuperCallExpr& expr);
    void visitLiteralExpr(LiteralExpr& expr);
    void visitBoolLitExpr(BoolLitExpr& expr);
    void visitAddressExpr(AddressExpr& expr);
    void visitDerefExpr(DerefExpr& expr);
    void visitCastExpr(CastExpr& expr);
    void visitSizeofExpr(SizeofExpr& expr);
    void visitAdrExpr(AdrExpr& expr);
    void visitArrayInitExpr(ArrayInitExpr& expr);
    void visitStructInitExpr(StructInitExpr& expr);
    
    // --- Name Resolution Helpers ---
    SymbolId resolveIdentifier(const std::string& name);
    SymbolId lookupInScopeChain(const std::string& name) const;

    // --- Array row types ---
    // Multi-dimensional arrays are modelled as arrays of rows, mirroring the
    // nested-STArray C++ target: indexing a rank>1 array yields a row type
    // holding the remaining dimensions (one entry per source array type).
    TypeId arrayRowType(const TypeInfo& arrayType);
    std::unordered_map<TypeId, TypeId> arrayRowCache_;
    
    // --- Scope Management ---
    void pushScope(ScopeId scopeId);
    void popScope();
    ScopeId currentScope() const;
    
    // --- POU/Method Context ---
    void enterPou(const POU& pou);
    void leavePou();
    void enterMethod(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId);
    void leaveMethod();
    
    // --- Literal Type Resolution ---
    TypeId resolveLiteralType(const LiteralExpr& lit) const;
    
    // --- Diagnostics ---
    void reportError(DiagnosticCode code, const std::string& msg, const SourceLocation& loc);
    void reportWarning(DiagnosticCode code, const std::string& msg, const SourceLocation& loc);
    SourceLocation makeLocation(uint32_t line, uint32_t col = 0) const;
};

} // namespace st2cpp::semantic
