/**
 * @file RuntimeEmitter.h
 * @brief Emits the undoPLC runtime: one Master per PLC, one Worker per task
 * @details Turns a validated Task Configuration into the single translation
 * unit that actually runs the PLC. Every entry of `tasks` becomes an
 * UndoWorkerTaskBase subclass whose runWork() calls the `run()` of each program
 * it lists, in the declared order; every distinct `plc` value becomes one
 * UndoMasterTaskBase subclass that drives the cycle all of its workers share.
 *
 * The emitter emits only C++ text: it never resolves a CPU at generation time.
 * `cpu_affinity: -1` is emitted as a runtime lookup, because which cores are
 * isolated is a property of the machine the application will boot on.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include "codegen/EmissionContext.h"
#include "task/TaskConfig.h"
#include <string>
#include <vector>

namespace st2cpp::codegen {

/**
 * @class RuntimeEmitter
 * @brief Assembles the generated PLC runtime translation unit
 */
class RuntimeEmitter
{
public:
   explicit RuntimeEmitter(EmissionContext& ctx) : m_ctx(ctx) {}

   /**
    * @struct RuntimeResult
    * @brief The emitted runtime and every reason it could not be emitted.
    */
   struct RuntimeResult
   {
      std::string content;             // the runtime .cpp (empty when !ok())
      std::vector<std::string> errors; // e.g. a task naming a program that does not exist

      bool ok() const { return errors.empty(); }
   };

   /**
    * @brief Emit the runtime for `config` over the programs declared in `tu`.
    * @details Fails, rather than emits something that will not compile, when a
    * task names a program that the translation unit does not declare.
    */
   RuntimeResult generate(const TranslationUnit& tu, const task::TaskConfig& config);

private:
   /**
    * @brief C++ type name of a program, qualified with the ST namespace.
    */
   std::string programTypeName(const std::string& normalizedName) const;

   /**
    * @brief Fold an identifier the way the loader compares task names, so the
    *        `programs` of a tasks.json are matched against the POU names.
    */
   static std::string foldKey(const std::string& s);

   /// Sanitize a configured name into a C++ identifier fragment.
   static std::string sanitize(const std::string& s);

   EmissionContext& m_ctx;
};

} // namespace st2cpp::codegen