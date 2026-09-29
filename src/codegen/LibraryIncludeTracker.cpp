/**
 * @file LibraryIncludeTracker.cpp
 * @brief Decides which external library headers the generated C++ must include
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/LibraryIncludeTracker.h"
#include "codegen/SemanticBridge.h"
#include "semantic/TypeSystem.h"

namespace st2cpp::codegen {

using namespace st2cpp::semantic;

/**
 * @brief Pre-scan of the translation unit that collects the include lines of
 *        the libraries that are actually used.
 *
 * Libraries are only included when used, never on every configuration load.
 * Usage means: any declaration whose type resolves to an external library type
 * (globals, POUs, struct members, FB instances) or any expression that
 * references an external symbol (function calls, FB invocations, globals,
 * constants, enumerators). Includes are deduplicated and sorted for a
 * deterministic header.
 */
void LibraryIncludeTracker::computeLibraryIncludeLines(const TranslationUnit& tu)
{
   m_libraryIncludeLines.clear();
   if (!m_semantic.semanticAvailable() || !m_semantic.info()->libraryRegistry) {
      return;
   }
   std::unordered_set<std::string> used;

   // 1) All declared types across the TU (global sections + POUs).
   for (const auto& sec : tu.globals) {
      for (const auto& d : sec.decls) {
         collectUsedLibrariesFromTypeRef(d.type, used);
      }
   }
   for (const auto& pou : tu.pous) {
      collectUsedLibrariesFromTypeRef(pou.returnType, used);
      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            collectUsedLibrariesFromTypeRef(d.type, used);
         }
      }
      for (const auto& m : pou.methods) {
         collectUsedLibrariesFromTypeRef(m.returnType, used);
         for (const auto& lv : m.localVars) {
            collectUsedLibrariesFromTypeRef(lv.type, used);
         }
      }
   }
   for (const auto& sd : tu.structs) {
      for (const auto& m : sd.members) {
         collectUsedLibrariesFromTypeRef(m.type, used);
      }
   }
   for (const auto& iface : tu.interfaces) {
      for (const auto& m : iface.methods) {
         collectUsedLibrariesFromTypeRef(m.returnType, used);
         for (const auto& p : m.parameters) {
            collectUsedLibrariesFromTypeRef(p.type, used);
         }
      }
   }

   // 2) Symbols referenced in expression bodies (function call targets, FB
   //    invocations, globals/constants/enumerators).
   for (const auto& pou : tu.pous) {
      for (const auto& stmt : pou.body) {
         collectUsedLibrariesFromStmt(*stmt, used);
      }
      for (const auto& m : pou.methods) {
         for (const auto& stmt : m.body) {
            collectUsedLibrariesFromStmt(*stmt, used);
         }
      }
   }

   // 3) Deterministic, deduplicated include list.
   std::vector<std::string> lines(used.begin(), used.end());
   std::sort(lines.begin(), lines.end());
   for (const std::string& line : lines) {
      m_libraryIncludeLines.push_back(line);
   }
}

/**
 * @brief Include code block for the pre-scanned libraries ("" when none).
 */
std::string LibraryIncludeTracker::libraryIncludeBlock() const
{
   if (m_libraryIncludeLines.empty()) {
      return "";
   }
   std::string block;
   for (const auto& line : m_libraryIncludeLines) {
      block += "#include \"" + line + "\"\n";
   }
   return block;
}

void LibraryIncludeTracker::recordUsedLibraryForSymbol(const st2cpp::semantic::Symbol& sym, std::unordered_set<std::string>& used) const
{
   if (!sym.isExternal || sym.externalLibraryId.empty()) {
      return;
   }
   const st2cpp::library::LibraryDescriptor* lib = m_semantic.info()->libraryRegistry->get(sym.externalLibraryId);
   if (lib && !lib->cppBinding.include.empty()) {
      used.insert(lib->cppBinding.include);
   }
   // External structs also require the libraries of their member types
   // (e.g. examplelib::Channel has a Limits field of corelib::Range): the
   // wrapper header for the struct references those types too.
   const auto* st = m_semantic.semanticSymTab();
   if (!st) {
      return;
   }
   // Depth guard: struct graphs must be walked but never cycled (a malicious or
   // pathological descriptor must not recurse indefinitely).
   static thread_local size_t depth = 0;
   if (++depth > 64) {
      --depth;
      return;
   }
   for (st2cpp::semantic::SymbolId memberId : sym.members) {
      if (const st2cpp::semantic::Symbol* m = st->get(memberId)) {
         recordUsedLibraryForTypeId(m->typeId, used);
      }
   }
   --depth;
}

void LibraryIncludeTracker::recordUsedLibraryForTypeId(st2cpp::semantic::TypeId typeId, std::unordered_set<std::string>& used) const
{
   const auto* st = m_semantic.semanticSymTab();
   if (!st || typeId == 0) {
      return;
   }
   const st2cpp::semantic::TypeInfo* t = st->getType(typeId);
   if (!t) {
      return;
   }
   if (t->kind == st2cpp::semantic::TypeKind::Array) {
      recordUsedLibraryForTypeId(t->elementTypeId, used);
      return;
   }
   if (t->kind == st2cpp::semantic::TypeKind::Pointer || t->kind == st2cpp::semantic::TypeKind::Reference) {
      recordUsedLibraryForTypeId(t->pointedTypeId, used);
      return;
   }
   if (t->kind != st2cpp::semantic::TypeKind::Struct && t->kind != st2cpp::semantic::TypeKind::Enum
       && t->kind != st2cpp::semantic::TypeKind::FunctionBlock) {
      return;
   }
   const st2cpp::semantic::Symbol* sym = t->symbolId ? st->get(t->symbolId) : nullptr;
   if (!sym) {
      return;
   }
   recordUsedLibraryForSymbol(*sym, used);
}

void LibraryIncludeTracker::collectUsedLibrariesFromExpr(const Expr& expr, std::unordered_set<std::string>& used) const
{
   std::visit(
      [&](const auto& e) {
         using T = std::decay_t<decltype(e)>;
         const auto* st = m_semantic.semanticSymTab();

         if constexpr (std::is_same_v<T, IdentExpr>) {
            if (m_semantic.semanticAvailable() && e.symbolId != 0) {
               if (const st2cpp::semantic::Symbol* sym = st->get(e.symbolId)) {
                  recordUsedLibraryForSymbol(*sym, used);
                  if (sym->kind == st2cpp::semantic::SymbolKind::Variable) {
                     recordUsedLibraryForTypeId(sym->typeId, used);
                  }
               }
            }
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            if (m_semantic.semanticAvailable() && e.calleeSymbolId != 0) {
               if (const st2cpp::semantic::Symbol* callee = st->get(e.calleeSymbolId)) {
                  recordUsedLibraryForSymbol(*callee, used);
               }
            }
            if (e.callee) {
               collectUsedLibrariesFromExpr(*e.callee, used);
            }
            for (const auto& arg : e.args) {
               if (arg.value) {
                  collectUsedLibrariesFromExpr(*arg.value, used);
               }
            }
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            if (e.left) {
               collectUsedLibrariesFromExpr(*e.left, used);
            }
            if (e.right) {
               collectUsedLibrariesFromExpr(*e.right, used);
            }
         } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            if (e.operand) {
               collectUsedLibrariesFromExpr(*e.operand, used);
            }
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            if (e.object) {
               collectUsedLibrariesFromExpr(*e.object, used);
            }
         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            if (e.array) {
               collectUsedLibrariesFromExpr(*e.array, used);
            }
            for (const auto& idx : e.indices) {
               if (idx) {
                  collectUsedLibrariesFromExpr(*idx, used);
               }
            }
         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            if (e.pointer) {
               collectUsedLibrariesFromExpr(*e.pointer, used);
            }
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            collectUsedLibrariesFromTypeRef(e.targetType, used);
            if (e.operand) {
               collectUsedLibrariesFromExpr(*e.operand, used);
            }
         } else if constexpr (std::is_same_v<T, AdrExpr>) {
            if (e.operand) {
               collectUsedLibrariesFromExpr(*e.operand, used);
            }
         } else if constexpr (std::is_same_v<T, SizeofExpr>) {
            if (e.isType) {
               collectUsedLibrariesFromTypeRef(e.type, used);
            } else if (e.expr) {
               collectUsedLibrariesFromExpr(*e.expr, used);
            }
         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            for (const auto& el : e.elements) {
               if (el) {
                  collectUsedLibrariesFromExpr(*el, used);
               }
            }
         } else if constexpr (std::is_same_v<T, StructInitExpr>) {
            for (const auto& m : e.members) {
               if (m.value) {
                  collectUsedLibrariesFromExpr(*m.value, used);
               }
            }
         }
      },
      expr.node);
}

void LibraryIncludeTracker::collectUsedLibrariesFromStmt(const Stmt& stmt, std::unordered_set<std::string>& used) const
{
   std::visit(
      [&](const auto& s) {
         using T = std::decay_t<decltype(s)>;
         if constexpr (std::is_same_v<T, AssignStmt>) {
            if (s.lhs) {
               collectUsedLibrariesFromExpr(*s.lhs, used);
            }
            for (const auto& target : s.additionalTargets) {
               if (target) {
                  collectUsedLibrariesFromExpr(*target, used);
               }
            }
            if (s.rhs) {
               collectUsedLibrariesFromExpr(*s.rhs, used);
            }
         } else if constexpr (std::is_same_v<T, ExprStmt>) {
            if (s.expr) {
               collectUsedLibrariesFromExpr(*s.expr, used);
            }
         } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            if (s.expr) {
               collectUsedLibrariesFromExpr(*s.expr, used);
            }
         } else if constexpr (std::is_same_v<T, IfStmt>) {
            for (const auto& b : s.branches) {
               if (b.condition) {
                  collectUsedLibrariesFromExpr(*b.condition, used);
               }
               for (const auto& bs : b.body) {
                  collectUsedLibrariesFromStmt(*bs, used);
               }
            }
         } else if constexpr (std::is_same_v<T, ForStmt>) {
            if (s.from) {
               collectUsedLibrariesFromExpr(*s.from, used);
            }
            if (s.to) {
               collectUsedLibrariesFromExpr(*s.to, used);
            }
            if (s.by) {
               collectUsedLibrariesFromExpr(*s.by, used);
            }
            for (const auto& b : s.body) {
               collectUsedLibrariesFromStmt(*b, used);
            }
         } else if constexpr (std::is_same_v<T, CaseStmt>) {
            if (s.selector) {
               collectUsedLibrariesFromExpr(*s.selector, used);
            }
            for (const auto& cb : s.branches) {
               for (const auto& v : cb.values) {
                  if (v.low) {
                     collectUsedLibrariesFromExpr(*v.low, used);
                  }
                  if (v.high) {
                     collectUsedLibrariesFromExpr(*v.high, used);
                  }
               }
               for (const auto& bs : cb.body) {
                  collectUsedLibrariesFromStmt(*bs, used);
               }
            }
         } else if constexpr (std::is_same_v<T, WhileStmt>) {
            if (s.condition) {
               collectUsedLibrariesFromExpr(*s.condition, used);
            }
            for (const auto& b : s.body) {
               collectUsedLibrariesFromStmt(*b, used);
            }
         } else if constexpr (std::is_same_v<T, RepeatStmt>) {
            if (s.condition) {
               collectUsedLibrariesFromExpr(*s.condition, used);
            }
            for (const auto& b : s.body) {
               collectUsedLibrariesFromStmt(*b, used);
            }
         }
      },
      stmt.node);
}

void LibraryIncludeTracker::collectUsedLibrariesFromTypeRef(const TypeRef& tr, std::unordered_set<std::string>& used) const
{
   const auto* st = m_semantic.semanticSymTab();
   if (!st || tr.base != BaseType::NAMED || tr.name.empty()) {
      return;
   }
   const st2cpp::semantic::TypeId tid = st->getTypeIdByName(tr.name);
   if (tid != 0) {
      recordUsedLibraryForTypeId(tid, used);
   }
}

} // namespace st2cpp::codegen
