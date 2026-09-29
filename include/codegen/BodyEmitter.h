/**
 * @file BodyEmitter.h
 * @brief Expression and statement emission
 * @details Emits the body of a function block, function or program: every
 * statement form (assignment, IF, FOR, WHILE, REPEAT, CASE) and every
 * expression form, plus the address read/write helpers and the ordered struct
 * initializer they share. It reads and writes the shared `EmissionContext`.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "codegen/EmissionContext.h"

namespace st2cpp::codegen {

/**
 * @class BodyEmitter
 * @brief Turns statements and expressions into C++ text
 */
class BodyEmitter
{
public:
   explicit BodyEmitter(EmissionContext& ctx) : m_ctx(ctx) {}

   void genStmt(const Stmt& stmt);
   void genIf(const IfStmt& s);
   void genFor(const ForStmt& s);
   void genWhile(const WhileStmt& s);
   void genRepeat(const RepeatStmt& s);
   void genCase(const CaseStmt& s);

   std::string genExpr(const Expr& expr, BaseType typeHint = BaseType::VOID);

   std::string generateAddressAccess(const AddressExpr& addr);
   std::string generateAddressAccess(const AddressExpr& addr, const TypeRef* type) const;
   std::string generateAddressWrite(const AddressExpr& addr, const std::string& value);
   std::string generateAddressWrite(const AddressExpr& addr, const std::string& value, const TypeRef* type) const;

   std::string generateOrderedStructInit(const TypeRef& type, const std::shared_ptr<Expr>& initExpr);
   bool isBoolExpression(const std::shared_ptr<Expr>& expr) const;

private:
   EmissionContext& m_ctx;
};

} // namespace st2cpp::codegen
