/**
 * @file BodyVisitor.cpp
 * @brief Body visitor implementation: name resolution and AST decoration
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/BodyVisitor.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "semantic/TypeSystem.h"
#include "semantic/IecTime.h"
using namespace st2cpp::semantic::TypeChecker;
#include "ast/AST.h"
#include <algorithm>

namespace st2cpp::semantic {

/**
 * @brief Construct a body visitor for name resolution over a translation unit.
 * @param symTab The symbol table to resolve identifiers against
 * @param diag The diagnostics collector for reported problems
 */
BodyVisitor::BodyVisitor(SymbolTable& symTab, Diagnostics& diag) : symTab_(symTab), diag_(diag) {}

/**
 * @brief Visit a whole translation unit, resolving POU bodies and global initializers.
 * @param tu The translation unit to visit
 */
void BodyVisitor::visitTranslationUnit(const TranslationUnit& tu)
{
   for (const auto& sec : tu.globals) {
      for (const auto& decl : sec.decls) {
         if (decl.initialValue) {
            visitExpression(*decl.initialValue);
         }
      }
   }

   for (const auto& pou : tu.pous) {
      visitPouBody(pou);
   }
}

/**
 * @brief Resolve the body of a POU, including its variable initializers and methods.
 * @param pou The POU whose body is visited
 */
void BodyVisitor::visitPouBody(const POU& pou)
{
   enterPou(pou);

   for (const auto& sec : pou.varSections) {
      for (const auto& decl : sec.decls) {
         if (decl.initialValue) {
            visitExpression(*decl.initialValue);
         }
      }
   }

   visitStatementList(pou.body);

   for (const auto& method : pou.methods) {
      visitMethodBody(method, contextStack_.back().scopeId, contextStack_.back().pouId);
   }

   leavePou();
}

/**
 * @brief Push the context for a POU body onto the context stack.
 * @details Looks up the POU symbol and records its scope, kind and function
 * return type, and resets the hasReturn_ flag for the new body.
 * @param pou The POU being entered
 */
void BodyVisitor::enterPou(const POU& pou)
{
   SymbolId pouSymId = symTab_.lookup(pou.name);
   if (pouSymId == 0) {
      return;
   }

   Symbol* pouSym = symTab_.get(pouSymId);
   if (!pouSym) {
      return;
   }

   Context ctx;
   ctx.scopeId = pouSym->scopeId;
   ctx.pouId = pouSymId;
   ctx.inFunctionBody = (pou.kind == POUKind::FUNCTION);
   ctx.returnTypeId = pouSym->returnTypeId;
   hasReturn_ = false;
   contextStack_.push_back(ctx);
}

/**
 * @brief Pop the innermost POU context from the context stack.
 */
void BodyVisitor::leavePou()
{
   if (!contextStack_.empty()) {
      contextStack_.pop_back();
   }
}

/**
 * @brief Resolve the body of a method of a function block.
 * @details Finds the method symbol in the FB's members, pushes a context for the
 * method (falling back to the FB scope), visits the parameter and local-variable
 * initializers and the statement list, then unwinds the context.
 * @param method The method whose body is visited
 * @param fbScopeId The scope of the containing function block
 * @param fbSymbolId The symbol of the containing function block
 */
void BodyVisitor::visitMethodBody(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId)
{
   SymbolId methodSymId = 0;
   Symbol* fbSym = symTab_.get(fbSymbolId);
   if (fbSym) {
      for (SymbolId memberId : fbSym->members) {
         Symbol* memberSym = symTab_.get(memberId);
         if (memberSym && memberSym->kind == SymbolKind::Method && memberSym->name == method.name) {
            methodSymId = memberSym->id;
            break;
         }
      }
   }

   SymbolId methodScopeId = 0;
   Symbol* methodSym = symTab_.get(methodSymId);
   if (methodSym) {
      methodScopeId = methodSym->scopeId;
   }

   Context ctx;
   ctx.scopeId = methodScopeId != 0 ? methodScopeId : fbScopeId;
   ctx.pouId = fbSymbolId;
   ctx.methodId = methodSymId;
   ctx.inFunctionBody = false;
   contextStack_.push_back(ctx);

   for (const auto& param : method.parameters) {
      if (param.initialValue) {
         visitExpression(*param.initialValue);
      }
   }

   for (const auto& localVar : method.localVars) {
      if (localVar.initialValue) {
         visitExpression(*localVar.initialValue);
      }
   }

   visitStatementList(method.body);

   leaveMethod();
}

/**
 * @brief Push a scope onto the visitor's stack (no-op).
 * @details The SymbolTable manages its own scope stack, so this hook does nothing.
 * @param scopeId The scope to push (ignored)
 */
void BodyVisitor::pushScope(ScopeId scopeId)
{
   // SymbolTable manages its own scope stack
}

/**
 * @brief Pop a scope from the visitor's stack (no-op).
 * @details The SymbolTable manages its own scope stack, so this hook does nothing.
 */
void BodyVisitor::popScope()
{
   // SymbolTable manages its own scope stack
}

/**
 * @brief Return the current scope of the innermost context.
 * @return The current ScopeId, or the global scope when no context is active
 */
ScopeId BodyVisitor::currentScope() const
{
   if (contextStack_.empty()) {
      return symTab_.globalScope();
   }
   return contextStack_.back().scopeId;
}

/**
 * @brief Enter a method context (delegated to visitMethodBody).
 * @details Method bodies are handled entirely by visitMethodBody, so this hook
 * is intentionally empty.
 * @param method The method being entered
 * @param fbScopeId The scope of the containing function block
 * @param fbSymbolId The symbol of the containing function block
 */
void BodyVisitor::enterMethod(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId)
{
   // Already handled in visitMethodBody
}

/**
 * @brief Pop the innermost method context from the context stack.
 */
void BodyVisitor::leaveMethod()
{
   if (!contextStack_.empty()) {
      contextStack_.pop_back();
   }
}

// --- Statement Visiting ---

/**
 * @brief Visit each statement in a statement list in order.
 * @param stmts The list of statements to visit
 */
void BodyVisitor::visitStatementList(const std::vector<std::shared_ptr<Stmt>>& stmts)
{
   for (const auto& stmt : stmts) {
      visitStatement(*stmt);
   }
}

/**
 * @brief Dispatch a statement to its specific visitor by variant type.
 * @param stmt The statement to visit
 */
void BodyVisitor::visitStatement(const Stmt& stmt)
{
if (auto p = std::get_if<AssignStmt>(&stmt.node)) {
      visitAssignStmt(*p, stmt.line, stmt.col);
    } else if (auto p = std::get_if<ExprStmt>(&stmt.node)) {
      visitExprStmt(*p);
    } else if (auto p = std::get_if<IfStmt>(&stmt.node)) {
      visitIfStmt(*p);
    } else if (auto p = std::get_if<ForStmt>(&stmt.node)) {
      visitForStmt(*p, stmt.line, stmt.col);
   } else if (auto p = std::get_if<WhileStmt>(&stmt.node)) {
      visitWhileStmt(*p);
   } else if (auto p = std::get_if<RepeatStmt>(&stmt.node)) {
      visitRepeatStmt(*p);
   } else if (auto p = std::get_if<CaseStmt>(&stmt.node)) {
      visitCaseStmt(*p);
   } else if (auto p = std::get_if<ReturnStmt>(&stmt.node)) {
      visitReturnStmt(*p);
   } else if (auto p = std::get_if<ExitStmt>(&stmt.node)) {
      visitExitStmt(*p);
   } else if (auto p = std::get_if<ContinueStmt>(&stmt.node)) {
      visitContinueStmt(*p);
   } else if (auto p = std::get_if<EmptyStmt>(&stmt.node)) {
      visitEmptyStmt(*p);
   }
}

/**
 * @brief Visit an assignment statement and type-check it.
 * @details Visits both operands and reports an InvalidAssignment diagnostic when
 * the RHS type is not assignable to the LHS. In Strict mode an additional
 * diagnostic is emitted for permissive-OK but non-implicit IEC conversions.
 * @param stmt The assignment statement to visit
 * @param line The source line of the assignment
 */
void BodyVisitor::visitAssignStmt(const AssignStmt& stmt, uint32_t line, uint32_t col)
{
   std::vector<Expr*> lhsExprs;
   lhsExprs.push_back(stmt.lhs.get());
   for (auto& target : stmt.additionalTargets) {
      if (target) {
         lhsExprs.push_back(target.get());
      }
   }

   for (auto* lhsExpr : lhsExprs) {
      if (lhsExpr) {
         visitExpression(*lhsExpr);
      }
   }
   visitExpression(*stmt.rhs);

   for (auto* lhsExpr : lhsExprs) {
      if (!lhsExpr) {
         continue;
      }

      TypeId lhsId = lhsExpr->resolvedTypeId;
      TypeId rhsId = stmt.rhs->resolvedTypeId;
      const TypeInfo* lhsType = symTab_.getType(lhsId);
      const TypeInfo* rhsType = symTab_.getType(rhsId);

      if (lhsType && rhsType) {
         auto location = makeLocation(lhsExpr->line ? lhsExpr->line : line, lhsExpr->col ? lhsExpr->col : col);
         bool legacyOk = TypeChecker::checkAssignment(lhsType, rhsType, location, diag_);

         if (strict_ && legacyOk && !lhsType->isImplicitlyConvertibleFrom(rhsType)) {
            diag_.addError(DiagnosticCode::InvalidAssignment,
                           "strict IEC: " + rhsType->name + " is not implicitly convertible to " + lhsType->name
                              + " (value-losing or cross-family implicit assignment)",
                           location);
         }
      }
   }
}

/**
 * @brief Visit an expression statement.
 * @param stmt The expression statement to visit
 */
void BodyVisitor::visitExprStmt(const ExprStmt& stmt)
{
   visitExpression(*stmt.expr);
}

/**
 * @brief Visit an IF statement, checking that every condition is a BOOL.
 * @details Each branch condition that is not of boolean type is reported through
 * the NonBooleanCondition diagnostic.
 * @param stmt The IF statement to visit
 */
void BodyVisitor::visitIfStmt(const IfStmt& stmt)
{
   for (const auto& branch : stmt.branches) {
      if (branch.condition) {
         visitExpression(*branch.condition);
         TypeId condId = branch.condition->resolvedTypeId;
         const TypeInfo* condType = symTab_.getType(condId);
         if (condType && !condType->isBitType) {
            diag_.addError(DiagnosticCode::NonBooleanCondition,
                           "IF condition must be BOOL, got " + condType->name,
                           makeLocation(branch.condition->line, branch.condition->col));
         }
      }
      visitStatementList(branch.body);
   }
}

/**
 * @brief Visit a FOR loop, tracking loop nesting and checking control variable compatibility.
 * @details The control variable must be assignable from the FROM, TO and BY
 * expressions; violations are reported as diagnostics. Loop depth is updated so
 * EXIT/CONTINUE are validated.
 * @param stmt The FOR statement to visit
 * @param line The source line of the FOR statement
 */
void BodyVisitor::visitForStmt(const ForStmt& stmt, uint32_t line, uint32_t col)
{
   Context& ctx = contextStack_.back();
   int prevDepth = ctx.loopDepth;
   bool wasInLoop = ctx.inLoop;
   ctx.loopDepth++;
   ctx.inLoop = true;

   visitExpression(*stmt.from);
   visitExpression(*stmt.to);
   if (stmt.by) {
      visitExpression(*stmt.by);
   }

   SymbolId varSymId = resolveIdentifier(stmt.var);
   if (varSymId != 0) {
      Symbol* varSym = symTab_.get(varSymId);
      if (varSym) {
         const TypeInfo* varType = symTab_.getType(varSym->typeId);
         if (varType) {
            TypeId fromId = stmt.from->resolvedTypeId;
            TypeId toId = stmt.to->resolvedTypeId;
            const TypeInfo* fromType = symTab_.getType(fromId);
            const TypeInfo* toType = symTab_.getType(toId);
            if (fromType && varType && !varType->isAssignableFrom(fromType)) {
               diag_.addError(DiagnosticCode::InvalidForControlVariable,
                              "FOR control variable '" + stmt.var + "' type " + varType->name + " is not compatible with FROM expression "
                                 + fromType->name,
                              makeLocation(line, col));
            }
            if (toType && varType && !varType->isAssignableFrom(toType)) {
               diag_.addError(DiagnosticCode::ForBoundsTypeMismatch,
                              "FOR control variable '" + stmt.var + "' type " + varType->name + " is not compatible with TO expression "
                                 + toType->name,
                              makeLocation(line, col));
            }
            if (stmt.by) {
               TypeId byId = stmt.by->resolvedTypeId;
               const TypeInfo* byType = symTab_.getType(byId);
               if (byType && varType && !varType->isAssignableFrom(byType)) {
                  diag_.addError(DiagnosticCode::ForBoundsTypeMismatch,
                                 "FOR step expression type " + byType->name + " is not compatible with control variable " + varType->name,
                                 makeLocation(line, col));
               }
            }
         }
      }
   }
   visitStatementList(stmt.body);

   ctx.loopDepth = prevDepth;
   ctx.inLoop = wasInLoop;
}

/**
 * @brief Visit a WHILE loop, checking that its condition is a BOOL.
 * @details Updates loop depth for EXIT/CONTINUE validation.
 * @param stmt The WHILE statement to visit
 */
void BodyVisitor::visitWhileStmt(const WhileStmt& stmt)
{
   Context& ctx = contextStack_.back();
   int prevDepth = ctx.loopDepth;
   bool wasInLoop = ctx.inLoop;
   ctx.loopDepth++;
   ctx.inLoop = true;

   visitExpression(*stmt.condition);
   TypeId condId = stmt.condition->resolvedTypeId;
   const TypeInfo* condType = symTab_.getType(condId);
   if (condType && !condType->isBitType) {
      diag_.addError(DiagnosticCode::NonBooleanCondition,
                     "WHILE condition must be BOOL, got " + condType->name,
                     makeLocation(stmt.condition->line, stmt.condition->col));
   }
   visitStatementList(stmt.body);

   ctx.loopDepth = prevDepth;
   ctx.inLoop = wasInLoop;
}

/**
 * @brief Visit a REPEAT loop, checking that its condition is a BOOL.
 * @details Updates loop depth for EXIT/CONTINUE validation.
 * @param stmt The REPEAT statement to visit
 */
void BodyVisitor::visitRepeatStmt(const RepeatStmt& stmt)
{
   Context& ctx = contextStack_.back();
   int prevDepth = ctx.loopDepth;
   bool wasInLoop = ctx.inLoop;
   ctx.loopDepth++;
   ctx.inLoop = true;

   visitStatementList(stmt.body);
   visitExpression(*stmt.condition);
   TypeId condId = stmt.condition->resolvedTypeId;
   const TypeInfo* condType = symTab_.getType(condId);
   if (condType && !condType->isBitType) {
      diag_.addError(DiagnosticCode::NonBooleanCondition,
                     "REPEAT condition must be BOOL, got " + condType->name,
                     makeLocation(stmt.condition->line, stmt.condition->col));
   }

   ctx.loopDepth = prevDepth;
   ctx.inLoop = wasInLoop;
}

/**
 * @brief Visit a CASE statement, checking selector/label type compatibility.
 * @details Each branch value/range expression must be compatible with the
 * selector type; incompatible labels are reported as diagnostics.
 * @param stmt The CASE statement to visit
 */
void BodyVisitor::visitCaseStmt(const CaseStmt& stmt)
{
   visitExpression(*stmt.selector);
   TypeId selId = stmt.selector->resolvedTypeId;
   const TypeInfo* selType = symTab_.getType(selId);

   for (const auto& branch : stmt.branches) {
      if (branch.values.empty()) {
         continue;
      }
      for (const auto& val : branch.values) {
         if (val.low) {
            visitExpression(*val.low);
            TypeId lowId = val.low->resolvedTypeId;
            const TypeInfo* lowType = symTab_.getType(lowId);
            if (selType && lowType && !selType->isCompatibleWith(lowType, CompatContext::Comparison)) {
               diag_.addError(DiagnosticCode::CaseSelectorTypeMismatch,
                              "CASE label type " + lowType->name + " is not compatible with selector " + selType->name,
                              makeLocation(val.low->line, val.low->col));
            }
         }
         if (val.high) {
            visitExpression(*val.high);
            TypeId highId = val.high->resolvedTypeId;
            const TypeInfo* highType = symTab_.getType(highId);
            if (selType && highType && !selType->isCompatibleWith(highType, CompatContext::Comparison)) {
               diag_.addError(DiagnosticCode::CaseSelectorTypeMismatch,
                              "CASE label type " + highType->name + " is not compatible with selector " + selType->name,
                              makeLocation(val.high->line, val.high->col));
            }
         }
      }
      visitStatementList(branch.body);
   }
}

/**
 * @brief Visit a RETURN statement, checking the returned value type.
 * @details A RETURN with a value is rejected in procedures, FBs and methods
 * without a return type. Otherwise the expression must be assignable (and, in
 * Strict mode, implicitly convertible) to the declared return type. A bare
 * RETURN in a typed function is also reported.
 * @param stmt The RETURN statement to visit
 */
void BodyVisitor::visitReturnStmt(const ReturnStmt& stmt)
{
   hasReturn_ = true;
   if (stmt.expr) {
      visitExpression(*stmt.expr);

      TypeId exprTypeId = stmt.expr->resolvedTypeId;
      const TypeInfo* exprType = symTab_.getType(exprTypeId);

      Context& ctx = contextStack_.back();

      if (!ctx.inFunctionBody && ctx.returnTypeId == 0) {
         diag_.addError(DiagnosticCode::InvalidReturnType,
                        "RETURN with value in procedure/FB without return type",
                        makeLocation(stmt.expr->line, stmt.expr->col));
      } else if (ctx.returnTypeId != 0 && exprType) {
         const TypeInfo* returnType = symTab_.getType(ctx.returnTypeId);
         if (returnType && exprType) {
            if (!returnType->isAssignableFrom(exprType)) {
               diag_.addError(DiagnosticCode::InvalidReturnType,
                              "RETURN type " + exprType->name + " is not compatible with function return type " + returnType->name,
                              makeLocation(stmt.expr->line, stmt.expr->col));
            } else if (strict_ && !returnType->isImplicitlyConvertibleFrom(exprType)) {
               // Fase 6 IEC strictness: permissive-OK but non-implicit
               // (narrowing/cross-family) return conversions.
               diag_.addError(DiagnosticCode::InvalidReturnType,
                              "strict IEC: " + exprType->name + " is not implicitly convertible to function return type "
                                 + returnType->name,
                              makeLocation(stmt.expr->line, stmt.expr->col));
            }
         }
      }
   } else {
      Context& ctx = contextStack_.back();
      if (ctx.returnTypeId != 0) {
         diag_.addError(DiagnosticCode::InvalidReturnType, "Missing RETURN value in function with return type", makeLocation(0));
      }
   }
}

/**
 * @brief Visit an EXIT statement, which is only valid inside a loop.
 * @details Reports a diagnostic when no enclosing loop is active.
 * @param stmt The EXIT statement to visit
 */
void BodyVisitor::visitExitStmt(const ExitStmt& stmt)
{
   Context& ctx = contextStack_.back();
   if (!ctx.inLoop) {
      diag_.addError(DiagnosticCode::InvalidReturnType, "EXIT can only be used inside a loop", makeLocation(0));
   }
}

/**
 * @brief Visit a CONTINUE statement, which is only valid inside a loop.
 * @details Reports a diagnostic when no enclosing loop is active.
 * @param stmt The CONTINUE statement to visit
 */
void BodyVisitor::visitContinueStmt(const ContinueStmt&)
{
   Context& ctx = contextStack_.back();
   if (!ctx.inLoop) {
      diag_.addError(DiagnosticCode::InvalidReturnType, "CONTINUE can only be used inside a loop", makeLocation(0));
   }
}

/**
 * @brief Visit an empty statement (no-op).
 * @param stmt The empty statement to visit
 */
void BodyVisitor::visitEmptyStmt(const EmptyStmt&)
{
   // Nothing to visit
}

// --- Expression Visiting ---
// Architecture: AST Decoration Model
// visitExpression takes Expr& and decorates Expr::symbolId and Expr::resolvedTypeId
// based on the variant type. Child traversal is delegated to specific visitors.

/**
 * @brief Resolve and decorate an expression with its symbol and type.
 * @details Dispatches on the expression variant, resolving identifiers through
 * the scope chain, decorating literals with their resolved type and visiting
 * children for composite expressions. The ADR operator registers or reuses a
 * pointer TypeInfo for its operand.
 * @param expr The expression to resolve and decorate
 */
void BodyVisitor::visitExpression(Expr& expr)
{
   if (auto p = std::get_if<IdentExpr>(&expr.node)) {
      SymbolId symId = resolveIdentifier(p->name);
      p->symbolId = symId;
      expr.symbolId = symId;
      if (symId != 0) {
         resolvedCount_++;
         Symbol* sym = symTab_.get(symId);
         if (sym) {
            expr.resolvedTypeId = sym->typeId;
         }
      } else {
         unresolvedCount_++;
         reportError(DiagnosticCode::UndeclaredIdentifier, "unknown identifier '" + p->name + "'", makeLocation(expr.line, expr.col));
      }
   } else if (auto p = std::get_if<LiteralExpr>(&expr.node)) {
      TypeId typeId = resolveLiteralType(*p);
      expr.resolvedTypeId = typeId;
      if (typeId != 0) {
         resolvedCount_++;
      }
      if (p->suffix == "TIME") {
         if (!iecTimeLiteralToMilliseconds(p->value)) {
            reportError(DiagnosticCode::InvalidTimeLiteral,
                        "invalid TIME literal '" + p->value + "' (expected e.g. T#5s, T#100ms, T#1d2h)",
                        makeLocation(expr.line, expr.col));
         }
      }
   } else if (auto p = std::get_if<BoolLitExpr>(&expr.node)) {
      TypeId boolTypeId = symTab_.getTypeIdByName("BOOL");
      expr.resolvedTypeId = boolTypeId;
      resolvedCount_++;
   } else if (auto p = std::get_if<CallExpr>(&expr.node)) {
      visitCallExpr(expr);
      if (p->calleeSymbolId != 0) {
         expr.symbolId = p->calleeSymbolId;
         Symbol* calleeSym = symTab_.get(p->calleeSymbolId);
         if (calleeSym) {
            expr.resolvedTypeId = calleeSym->returnTypeId;
         }
      }
   } else if (auto p = std::get_if<MemberExpr>(&expr.node)) {
      visitMemberExpr(expr);
   } else if (auto p = std::get_if<IndexExpr>(&expr.node)) {
      visitIndexExpr(expr);
   } else if (auto p = std::get_if<BinaryExpr>(&expr.node)) {
      visitBinaryExpr(expr);
   } else if (auto p = std::get_if<UnaryExpr>(&expr.node)) {
      visitUnaryExpr(expr);
   } else if (auto p = std::get_if<SuperCallExpr>(&expr.node)) {
      visitSuperCallExpr(*p);
   } else if (auto p = std::get_if<AddressExpr>(&expr.node)) {
      visitAddressExpr(*p);
   } else if (auto p = std::get_if<DerefExpr>(&expr.node)) {
      visitDerefExpr(*p);
   } else if (auto p = std::get_if<CastExpr>(&expr.node)) {
      visitCastExpr(*p);
   } else if (auto p = std::get_if<SizeofExpr>(&expr.node)) {
      visitSizeofExpr(*p);
   } else if (auto p = std::get_if<AdrExpr>(&expr.node)) {
      visitAdrExpr(*p);
      // ADR(x) decorates the enclosing expression with the pointer type
      TypeId operandTypeId = p->operand->resolvedTypeId;
      const TypeInfo* operandType = symTab_.getType(operandTypeId);
      if (operandType && operandTypeId != 0) {
         // Reuse an already-registered pointer type when available, otherwise register one.
         std::string ptrName = "POINTER TO " + operandType->name;
         TypeId ptrTypeId = symTab_.getTypeIdByName(ptrName);
         if (ptrTypeId == 0) {
            TypeInfo ptrType;
            ptrType.kind = TypeKind::Pointer;
            ptrType.name = ptrName;
            ptrType.pointedTypeId = operandTypeId;
            ptrType.isNumeric = false;
            ptrType.sizeInBytes = operandType->sizeInBytes;
            ptrType.alignment = operandType->alignment;
            ptrTypeId = symTab_.registerPointerType(ptrType);
         }
         expr.resolvedTypeId = ptrTypeId;
      }
   } else if (auto p = std::get_if<ArrayInitExpr>(&expr.node)) {
      visitArrayInitExpr(*p);
   } else if (auto p = std::get_if<StructInitExpr>(&expr.node)) {
      visitStructInitExpr(*p);
   }
}

/**
 * @brief Visit an identifier expression (no-op).
 * @details Decoration of identifiers is handled by visitExpression; this hook
 * exists for potential separate processing.
 * @param expr The identifier expression to visit
 */
void BodyVisitor::visitIdentExpr(IdentExpr& expr)
{
   // Child traversal only - decoration is handled by visitExpression
   // This method exists for potential future use where IdentExpr
   // needs separate processing beyond what visitExpression does
}

/**
 * @brief Resolve a member access on an object expression.
 * @details Visits the object subexpression, then resolves the member via
 * TypeChecker::checkMemberAccess and decorates the expression with the member's
 * symbol and type. A missing member is reported as a diagnostic.
 * @param expr The member expression to visit
 */
void BodyVisitor::visitMemberExpr(Expr& expr)
{
   auto& member = std::get<MemberExpr>(expr.node);
   visitExpression(*member.object);

   TypeId objTypeId = member.object->resolvedTypeId;
   const TypeInfo* objType = symTab_.getType(objTypeId);
   if (objType) {
      SourceLocation loc = makeLocation(expr.line, expr.col);
      MemberResult result = TypeChecker::checkMemberAccess(*objType, member.member, symTab_, diag_, loc);
      if (result.symbolId != 0) {
         member.symbolId = result.symbolId;
         expr.symbolId = result.symbolId;
         expr.resolvedTypeId = result.typeId;
      } else {
         diag_.addError(DiagnosticCode::MissingStructMember, "member '" + member.member + "' not found in " + objType->name, loc);
      }
   }
}

/**
 * @brief Resolve an array index expression.
 * @details Visits the array and index subexpressions, then consumes the indices
 * one step at a time. Indexing a rank-1 array yields the element type; indexing
 * a rank>1 array yields a row type that carries the remaining dimensions, so
 * both comma form `A[i,j]` and sequential form `A[i][j]` resolve correctly
 * (multi-dimensional arrays are arrays of rows, mirroring the nested-STArray
 * C++ target). Out-of-bounds constant literals are reported.
 * @param expr The index expression to visit
 */
void BodyVisitor::visitIndexExpr(Expr& expr)
{
   auto& index = std::get<IndexExpr>(expr.node);
   visitExpression(*index.array);
   for (auto& idx : index.indices) {
      visitExpression(*idx);
   }

   SourceLocation loc = makeLocation(expr.line, expr.col);
   TypeId curTypeId = index.array->resolvedTypeId;
   const TypeInfo* curType = symTab_.getType(curTypeId);

   if (curType && index.indices.empty()) {
      diag_.addError(DiagnosticCode::InvalidArrayIndex, "array requires at least one index", loc);
      return;
   }
   if (!curType) {
      return;
   }

   bool resolved = true;
   for (size_t k = 0; k < index.indices.size() && curType; ++k) {
      // Object must be an array at every step.
      if (curType->kind != TypeKind::Array) {
         diag_.addError(DiagnosticCode::InvalidArrayIndex,
                        "index access requires array type, got " + curType->name, loc);
         resolved = false;
         break;
      }
      const TypeInfo* idxType = symTab_.getType(index.indices[k]->resolvedTypeId);
      if (!idxType || !idxType->isNumeric || idxType->isReal) {
         diag_.addError(DiagnosticCode::InvalidArrayIndex,
                        "array index must be an integer type, got " + (idxType ? idxType->name : "unknown"), loc);
         resolved = false;
         break;
      }

      // Bounds checking for constant literal indices
      if (auto lit = std::get_if<LiteralExpr>(&index.indices[k]->node)) {
         try {
            int val = std::stoi(lit->value);
            const auto& dim = curType->dimensions.front();
            if (val < dim.low || val > dim.high) {
               diag_.addError(DiagnosticCode::InvalidArrayIndex,
                              "array index " + std::to_string(val) + " out of bounds [" + std::to_string(dim.low) + ".."
                                 + std::to_string(dim.high) + "]",
                              loc);
            }
         } catch (...) {
         }
      }

      // One index consumed: rank-1 yields the element, higher ranks the row.
      if (curType->dimensions.size() > 1) {
         curTypeId = arrayRowType(*curType);
         curType = symTab_.getType(curTypeId);
      } else {
         curTypeId = curType->elementTypeId;
         curType = symTab_.getType(curTypeId);
      }
   }

   if (resolved && curType) {
      expr.resolvedTypeId = curTypeId;
   }
}

/**
 * @brief Build (and cache) the row type of a rank>1 array.
 * @details Synthesises an anonymous ARRAY TypeInfo holding all dimensions but
 * the first, keeping the same element type. The result is cached per source
 * array type and registered once, so repeated partial indexing does not
 * duplicate entries.
 * @param arrayType The rank>1 array whose first dimension is being consumed
 * @return The TypeId of the row type
 */
TypeId BodyVisitor::arrayRowType(const TypeInfo& arrayType)
{
   auto it = arrayRowCache_.find(arrayType.id);
   if (it != arrayRowCache_.end()) {
      return it->second;
   }

   TypeInfo row;
   row.kind = TypeKind::Array;
   row.elementTypeId = arrayType.elementTypeId;
   row.name = "ARRAY";
   row.isNumeric = false;
   row.dimensions.assign(arrayType.dimensions.begin() + 1, arrayType.dimensions.end());
   size_t elemCount = 1;
   for (const auto& dim : row.dimensions) {
      elemCount *= static_cast<size_t>(dim.high - dim.low + 1);
   }
   if (const TypeInfo* elemType = symTab_.getType(row.elementTypeId)) {
      row.sizeInBytes = elemType->sizeInBytes * elemCount;
   }
   TypeId rowId = symTab_.registerArrayType(row);
   arrayRowCache_[arrayType.id] = rowId;
   return rowId;
}

/**
 * @brief Resolve a binary expression and type-check its operands.
 * @details Visits both operands, computes the result type via
 * TypeChecker::checkBinaryOp and decorates the expression. In Strict mode an
 * additional diagnostic is emitted when the operands are not implicitly
 * convertible to each other.
 * @param expr The binary expression to visit
 */
void BodyVisitor::visitBinaryExpr(Expr& expr)
{
   auto& bin = std::get<BinaryExpr>(expr.node);
   visitExpression(*bin.left);
   visitExpression(*bin.right);

   TypeId leftId = bin.left->resolvedTypeId;
   TypeId rightId = bin.right->resolvedTypeId;
   const TypeInfo* leftType = symTab_.getType(leftId);
   const TypeInfo* rightType = symTab_.getType(rightId);

   if (leftType && rightType) {
      TypeId resultType = TypeChecker::checkBinaryOp(bin.op, leftType, rightType, makeLocation(expr.line, expr.col), diag_, symTab_);
      expr.resolvedTypeId = resultType;

      // Fase 6 IEC strictness: operands of an operator must be implicitly
      // convertible to each other (widening only). checkBinaryOp already
      // reported unsupported combinations; this additive diagnostic targets
      // the permissive-OK pairs that IEC 61131-3 rejects (mixed-signed
      // integers, Integer<->BitString, Integer<->BOOL arithmetics). It is
      // NEVER reached in Permissive mode, so the golden path is unchanged.
      if (strict_ && resultType != 0 && !leftType->isImplicitlyConvertibleFrom(rightType)
          && !rightType->isImplicitlyConvertibleFrom(leftType)) {
         diag_.addError(DiagnosticCode::InvalidBinaryOperands,
                        "strict IEC: operands " + leftType->name + " and " + rightType->name + " of '" + bin.op
                           + "' are not implicitly convertible to each other",
                        makeLocation(expr.line, expr.col));
      }
   }
}

/**
 * @brief Resolve a unary expression and type-check its operand.
 * @details Visits the operand, computes the result type via
 * TypeChecker::checkUnaryOp and decorates the expression.
 * @param expr The unary expression to visit
 */
void BodyVisitor::visitUnaryExpr(Expr& expr)
{
   auto& un = std::get<UnaryExpr>(expr.node);
   visitExpression(*un.operand);

   TypeId operandId = un.operand->resolvedTypeId;
   const TypeInfo* operandType = symTab_.getType(operandId);

   if (operandType) {
      TypeId resultType = TypeChecker::checkUnaryOp(un.op, operandType, makeLocation(expr.line, expr.col), diag_, symTab_);
      expr.resolvedTypeId = resultType;
   }
}

/**
 * @brief Resolve a call expression, resolving the callee and validating arguments.
 * @details Visits the callee and arguments, records the callee symbol, decorates
 * the expression with the callee's return type and type-checks the arguments. In
 * Strict mode arguments that are permissive-OK but not implicitly convertible to
 * their parameter are additionally reported.
 * @param expr The call expression to visit
 */
void BodyVisitor::visitCallExpr(Expr& expr)
{
   auto& call = std::get<CallExpr>(expr.node);
   visitExpression(*call.callee);

   if (auto p = std::get_if<IdentExpr>(&call.callee->node)) {
      SymbolId calleeId = resolveIdentifier(p->name);
      call.calleeSymbolId = calleeId;
   }

   for (auto& arg : call.args) {
      visitExpression(*arg.value);
   }

   // FB instances (plain local/global variables, struct members, and array
   // elements) are called via their instance type: validate against the FB
   // symbol, which owns the VAR_INPUT/VAR_OUTPUT parameter list.
   const TypeInfo* calleeType = symTab_.getType(call.callee->resolvedTypeId);
   bool isFbCall = calleeType != nullptr && calleeType->kind == TypeKind::FunctionBlock && calleeType->symbolId != 0;

   Symbol* calleeSym = nullptr;
   if (isFbCall) {
      calleeSym = symTab_.get(calleeType->symbolId);
      expr.resolvedTypeId = calleeType->id;
   } else if (call.calleeSymbolId != 0) {
      calleeSym = symTab_.get(call.calleeSymbolId);
   }

   if (calleeSym) {
      // Type-name construction/conversion: CALLING a TYPE symbol with a single
      // argument (e.g. `INT(32767)`, `REAL(x)`) is an explicit IEC conversion
      // (cast), not a function call, so no parameter list is validated.
      const bool isTypeCast = !isFbCall && calleeSym->kind == SymbolKind::Type;
      if (isTypeCast) {
         if (call.args.size() != 1) {
            diag_.addError(DiagnosticCode::WrongArgumentCount,
                           "type conversion '" + calleeSym->name + "' requires exactly 1 argument, got "
                              + std::to_string(call.args.size()),
                           makeLocation(expr.line, expr.col));
         } else {
            expr.resolvedTypeId = calleeSym->typeId;
         }
         return;
      }

      if (!isFbCall) {
         expr.resolvedTypeId = calleeSym->returnTypeId;
      }

      std::vector<SymbolId> paramSymbols;
      for (SymbolId paramId : calleeSym->params) {
         paramSymbols.push_back(paramId);
      }
      SourceLocation loc = makeLocation(expr.line, expr.col);
      bool callOk = TypeChecker::checkCallArguments(paramSymbols, call.args, loc, diag_, symTab_, isFbCall);

      // Fase 6 IEC strictness: report permissive-OK arguments that are not
      // implicitly convertible to their parameter type (narrowing, sign or
      // family changes). Positional arguments pair by order, named ones by
      // name. Only reached in Strict mode; Permissive output is unchanged.
      if (strict_ && callOk) {
         size_t positional = 0;
         for (size_t i = 0; i < call.args.size(); ++i) {
            const CallExpr::Arg& arg = call.args[i];
            const Symbol* paramSym = nullptr;
            if (!arg.named) {
               if (positional < paramSymbols.size()) {
                  paramSym = symTab_.get(paramSymbols[positional]);
               }
               positional++;
            } else {
               for (SymbolId pid : paramSymbols) {
                  const Symbol* p = symTab_.get(pid);
                  if (p && p->name == arg.name) {
                     paramSym = p;
                     break;
                  }
               }
            }
            if (!paramSym) {
               continue;
            }
            const TypeInfo* paramType = symTab_.getType(paramSym->typeId);
            const Expr* argExpr = arg.value.get();
            const TypeInfo* argType = argExpr ? symTab_.getType(argExpr->resolvedTypeId) : nullptr;
            if (paramType && argType && !paramType->isImplicitlyConvertibleFrom(argType)) {
               diag_.addError(DiagnosticCode::InvalidArgumentType,
                              "strict IEC: argument of type " + argType->name + " for parameter '" + paramSym->name
                                 + "' is not implicitly convertible to " + paramType->name,
                              makeLocation(arg.line, arg.col));
            }
         }
      }
   }
}

/**
 * @brief Visit a super() call, resolving its arguments.
 * @param expr The super call expression to visit
 */
void BodyVisitor::visitSuperCallExpr(SuperCallExpr& expr)
{
   for (auto& arg : expr.args) {
      visitExpression(*arg.value);
   }
}

/**
 * @brief Visit a literal expression (no-op).
 * @details Decoration of literals is handled by visitExpression.
 * @param expr The literal expression to visit
 */
void BodyVisitor::visitLiteralExpr(LiteralExpr& expr)
{
   // Decoration handled by visitExpression
}

/**
 * @brief Visit a boolean literal expression (no-op).
 * @details Decoration of boolean literals is handled by visitExpression.
 * @param expr The boolean literal expression to visit
 */
void BodyVisitor::visitBoolLitExpr(BoolLitExpr& expr)
{
   // Decoration handled by visitExpression
}

/**
 * @brief Visit an address-of expression (no-op).
 * @details Address expressions carry no names to resolve.
 * @param expr The address expression to visit
 */
void BodyVisitor::visitAddressExpr(AddressExpr& expr)
{
   // Address expressions don't need name resolution
}

/**
 * @brief Visit a dereference expression, resolving its pointer operand.
 * @param expr The dereference expression to visit
 */
void BodyVisitor::visitDerefExpr(DerefExpr& expr)
{
   visitExpression(*expr.pointer);
}

/**
 * @brief Visit a cast expression, resolving its operand.
 * @param expr The cast expression to visit
 */
void BodyVisitor::visitCastExpr(CastExpr& expr)
{
   visitExpression(*expr.operand);
}

/**
 * @brief Visit a sizeof expression, resolving its optional operand.
 * @param expr The sizeof expression to visit
 */
void BodyVisitor::visitSizeofExpr(SizeofExpr& expr)
{
   if (expr.expr) {
      visitExpression(*expr.expr);
   }
}

/**
 * @brief Visit an ADR expression, resolving its operand.
 * @param expr The ADR expression to visit
 */
void BodyVisitor::visitAdrExpr(AdrExpr& expr)
{
   visitExpression(*expr.operand);
}

/**
 * @brief Visit an array initializer, resolving each element.
 * @param expr The array initializer expression to visit
 */
void BodyVisitor::visitArrayInitExpr(ArrayInitExpr& expr)
{
   for (auto& elem : expr.elements) {
      visitExpression(*elem);
   }
}

/**
 * @brief Visit a struct initializer, resolving each member value.
 * @param expr The struct initializer expression to visit
 */
void BodyVisitor::visitStructInitExpr(StructInitExpr& expr)
{
   for (auto& member : expr.members) {
      visitExpression(*member.value);
   }
}

// --- Name Resolution ---

/**
 * @brief Resolve an identifier to a SymbolId via the scope chain.
 * @param name The identifier name to resolve
 * @return The resolved SymbolId, or 0 when not found
 */
SymbolId BodyVisitor::resolveIdentifier(const std::string& name)
{
   return lookupInScopeChain(name);
}

/**
 * @brief Look up an identifier through the context scope chain and the global scope.
 * @details Walks the innermost-to-outermost contexts and their parent scopes,
 * then the base-class chain of the enclosing FB, and finally falls back to a
 * recursive symbol table lookup followed by the external (library) scope.
 * Project-local identifiers always win over external library symbols, which are
 * only reachable when no local/global declaration exists.
 * @param name The identifier name to look up
 * @return The resolved SymbolId, or 0 when not found
 */
SymbolId BodyVisitor::lookupInScopeChain(const std::string& name) const
{
   if (contextStack_.empty()) {
      SymbolId result = symTab_.lookupRecursive(name);
      return (result != 0) ? result : symTab_.lookupExternal(name);
   }

   for (auto it = contextStack_.rbegin(); it != contextStack_.rend(); ++it) {
      ScopeId scopeId = it->scopeId;
      if (scopeId == 0) {
         continue;
      }

      const Scope* scope = symTab_.getScope(scopeId);
      if (!scope) {
         continue;
      }

      auto itSym = scope->symbols.find(SymbolTable::normalizeKey(name));
      if (itSym != scope->symbols.end()) {
         return itSym->second;
      }

      ScopeId parentId = scope->parentId;
      while (parentId != 0) {
         const Scope* parentScope = symTab_.getScope(parentId);
         if (!parentScope) {
            break;
         }
         auto itParent = parentScope->symbols.find(SymbolTable::normalizeKey(name));
         if (itParent != parentScope->symbols.end()) {
            return itParent->second;
         }
         parentId = parentScope->parentId;
      }
   }

   // Finally, a plain identifier may refer to a member inherited by the
   // enclosing FB from its base class(es). Such members live in the base
   // FB's scope, so walk the base-class chain.
   if (!contextStack_.empty()) {
      const Context& top = contextStack_.back();
      if (top.pouId != 0) {
         Symbol* pouSym = symTab_.get(top.pouId);
         while (pouSym && pouSym->kind == SymbolKind::FunctionBlock && pouSym->baseClassId != 0) {
            Symbol* baseSym = symTab_.get(pouSym->baseClassId);
            if (!baseSym) {
               break;
            }
            if (baseSym->scopeId != 0) {
               const Scope* bs = symTab_.getScope(baseSym->scopeId);
               if (bs) {
                  auto bit = bs->symbols.find(SymbolTable::normalizeKey(name));
                  if (bit != bs->symbols.end()) {
                     return bit->second;
                  }
               }
            }
            pouSym = baseSym;
         }
      }
   }

   // Last resort: recursive lookup, then the external (library) scope so that
   // library functions/FBs/variables resolve only when nothing local matches.
   SymbolId result = symTab_.lookupRecursive(name);
   return (result != 0) ? result : symTab_.lookupExternal(name);
}

// --- Literal Type Resolution ---

/**
 * @brief Resolve the TypeId of a literal expression.
 * @details A typed suffix (e.g., "LREAL#") selects the named type; otherwise the
 * value is classified as BOOL, STRING, REAL or INT by content.
 * @param lit The literal expression to classify
 * @return The resolved TypeId of the literal
 */
TypeId BodyVisitor::resolveLiteralType(const LiteralExpr& lit) const
{
   if (!lit.suffix.empty()) {
      std::string typeName = lit.suffix;
      if (typeName.back() == '#') {
         typeName.pop_back();
      }
      return symTab_.getTypeIdByName(typeName);
   }

   const std::string& val = lit.value;
   if (val == "TRUE" || val == "FALSE") {
      return symTab_.getTypeIdByName("BOOL");
   }

   // STRING literals: ST allows both '...' and "..." (codegen emits "...").
   if (val.size() >= 2 && ((val.front() == '\'' && val.back() == '\'') || (val.front() == '"' && val.back() == '"'))) {
      return symTab_.getTypeIdByName("STRING");
   }

   if (val.find('.') != std::string::npos || val.find('e') != std::string::npos || val.find('E') != std::string::npos) {
      return symTab_.getTypeIdByName("REAL");
   }

   if (val.find('#') != std::string::npos) {
      return symTab_.getTypeIdByName("INT");
   }

   return symTab_.getTypeIdByName("INT");
}

// --- Diagnostics ---

/**
 * @brief Report an error through the diagnostics collector.
 * @param code The diagnostic code
 * @param msg The diagnostic message
 * @param loc The source location of the problem
 */
void BodyVisitor::reportError(DiagnosticCode code, const std::string& msg, const SourceLocation& loc)
{
   diag_.addError(code, msg, loc);
}

/**
 * @brief Report a warning through the diagnostics collector.
 * @param code The diagnostic code
 * @param msg The diagnostic message
 * @param loc The source location of the problem
 */
void BodyVisitor::reportWarning(DiagnosticCode code, const std::string& msg, const SourceLocation& loc)
{
   diag_.addWarning(code, msg, loc);
}

/**
 * @brief Build a SourceLocation tagged as coming from the input file.
 * @param line The source line number
 * @param col The source column number (defaults to 0)
 * @return The constructed source location
 */
SourceLocation BodyVisitor::makeLocation(uint32_t line, uint32_t col) const
{
   SourceLocation loc;
   loc.line = line;
   loc.column = col;
   loc.fileName = diag_.sourceFileName();
   return loc;
}

} // namespace st2cpp::semantic
