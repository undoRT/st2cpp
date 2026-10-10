/**
 * @file DiagEmitter.h
 * @brief Emits the variable diagnostic table and its JSON manifest
 *
 * The undoPLC runtime that carries diagnostic data knows the world as a flat
 * array of undoDiag::Node records: one row per variable (or per member of a
 * variable) reachable from a program or from a global, naming the bytes that
 * belong to it and where they live inside its parent. The table and its JSON
 * manifest are pure data produced from the *same* symbol table the declarations
 * are generated from, so the reflection can never name a member that does not
 * exist or point at bytes the compiler moved elsewhere.
 *
 * A node's offset and size are emitted as C++ expressions - offsetof() and
 * sizeof() spelled against the generated structs - so the table is a constexpr
 * description of the layout the compiler actually chose, including across
 * EXTENDS chains. GCC warns about offsetof() on the non-standard-layout classes
 * that a base class or an FB-typed member produces, so the generated source
 * wraps the table in a suppressed -Winvalid-offsetof region: the warning cannot
 * be silenced per-initializer, and the compiler-computed offsets it protects are
 * the whole point of the file.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "codegen/EmissionContext.h"
#include "semantic/SymbolTable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace st2cpp::task {
struct TaskConfig;
} // namespace st2cpp::task

namespace st2cpp::codegen {

/**
 * @class DiagEmitter
 * @brief Walks the semantic symbol table and emits DiagVars.hpp/.cpp/.json
 *
 * The emitter owns no output state of the project: it returns the three texts
 * together with the node count, and the caller (the facade, or the project
 * emitter) decides where they land. It only reads the analysis through the
 * EmissionContext, so it runs equally well standalone and inside a modular
 * generation pass. Without an attached SemanticInfo it answers deterministically
 * with an empty table: there is nothing to reflect without a symbol table.
 */
class DiagEmitter
{
public:
   explicit DiagEmitter(EmissionContext& ctx) : m_ctx(ctx) {}

   /**
    * @struct GlobalRoot
    * @brief A reflectable root the runtime has to bind.
    */
   struct GlobalRoot
   {
      std::string path;     // canonical path, e.g. "GA"
      std::string cppRef;   // C++ expression naming the object, e.g. "undoCore::GA"
      bool byValue = false; // true: cppRef IS the base pointer (process-image area);
                            // false: cppRef names an object, bind &cppRef
   };

   /// One file trio plus the number of table rows it describes.
   struct DiagFiles
   {
      std::string header;                  // DiagVars.hpp
      std::string source;                  // DiagVars.cpp
      std::string manifest;                // DiagVars.json
      size_t nodeCount = 0;                // rows in the table (and in the manifest nodes array)
      std::vector<GlobalRoot> globalRoots; // roots a runtime has to bind (globals only)
   };

   /**
    * @brief Generate the diagnostic table and manifest for the current TU.
    * @param config When non-null and non-empty, program roots are emitted once
    *        per (PLC, task, program) instance, with paths "PLC.TASK.PROG", so a
    *        program instantiated by several tasks gets one distinguishable root
    *        each. When null or empty, one root per program *type* is emitted
    *        with the bare POU name, which is what an offline tool wants.
    * @return The three texts. Deterministic for the same analysis and config.
    */
   DiagFiles generate(const st2cpp::task::TaskConfig* config = nullptr);

   /**
    * @brief The globals a generated runtime has to bind, with the C++ spelling
    *        of each. Shared with the runtime emitter so the filter that decides
    *        what is reflectable lives in exactly one place.
    */
   std::vector<GlobalRoot> globalRoots() const;

private:
   // One row of the reflection model. Offsets and sizes are held as C++
   // expressions (the text emitted into DiagVars.cpp); the manifest receives a
   // numeric shadow only where the size is statically known, never the emitted
   // expression, because the manifest is data for tools and must not contain
   // code.
   struct StNode
   {
      std::string name;
      std::string path;
      uint32_t parent = 0;
      bool root = false;
      std::string kindExpr; // "undoDiag::NodeKind::Scalar" (emitted as-is)
      std::string kindName; // "Scalar" (manifest)
      uint8_t flags = 0;    // NodeFlags bitmask (Readable=1, Writable=2, Forceable=4)
      uint8_t dimCount = 0;
      int32_t low[4] = {0, 0, 0, 0};
      int32_t high[4] = {0, 0, 0, 0};
      std::string offsetExpr;
      std::string sizeExpr;
      std::string elemSizeExpr;
      std::string cppType;

      std::optional<uint32_t> numSize;
      std::optional<uint32_t> numElemSize;

      std::vector<uint32_t> children;
   };

   static constexpr uint8_t FLAG_READ = 1;
   static constexpr uint8_t FLAG_WRITE = 2;
   static constexpr uint8_t FLAG_FORCE = 4;
   static constexpr uint8_t MAX_DIMS = 4; // undoDiag::MAX_DIMS

   // ---- walk -----------------------------------------------------------------

   void emitProgramNode(const st2cpp::semantic::Symbol& progSym, const std::string& path);
   void emitGlobalNode(const st2cpp::semantic::Symbol& sym);
   void emitValueNode(const st2cpp::semantic::Symbol& sym,
                      uint32_t parent,
                      const std::string& containerCpp,
                      const std::string& parentPath);
   void emitStructMembers(uint32_t parent,
                          const st2cpp::semantic::Symbol& structSym,
                          const std::string& containerCpp,
                          const std::string& parentPath);
   void emitScopedMembers(uint32_t parent,
                          const st2cpp::semantic::Symbol& pouSym,
                          const std::string& containerCpp,
                          const std::string& parentPath);
   void emitArrayElement(uint32_t parent, const st2cpp::semantic::TypeInfo* elementType, const std::string& parentPath);
   void emitProcessImageRoots();

   // ---- queries --------------------------------------------------------------

   std::string cppName(const st2cpp::semantic::TypeInfo* t) const;
   std::optional<uint32_t> knownSize(const st2cpp::semantic::TypeInfo* t) const;
   bool memcpySafe(const st2cpp::semantic::TypeInfo* t, std::unordered_set<st2cpp::semantic::TypeId>& visited) const;
   bool isAddressableType(const st2cpp::semantic::TypeInfo* t) const;

   /// The single filter that decides whether a global becomes a table root. Every
   /// code path that offers globals to the runtime (generate(), globalRoots(),
   /// emitGlobalNode) goes through it, so the three can never diverge: a global
   /// that is a root is also bound, and one that is dropped has no row to bind.
   bool isGlobalReflectable(const st2cpp::semantic::Symbol& sym) const;

   /// Reorder m_nodes breadth-first so each node's children get consecutive ids,
   /// making the emitted firstChild/childCount range equal the real child set.
   void reorderBreadthFirst();

   uint32_t addNode(StNode node);
   const st2cpp::semantic::Symbol* get(st2cpp::semantic::SymbolId id) const;
   const st2cpp::semantic::Symbol* findProgram(const std::string& name) const;
   static std::string foldKey(const std::string& s);
   const st2cpp::semantic::SymbolTable* table() const { return m_ctx.semanticSymTab(); }

   EmissionContext& m_ctx;
   std::vector<StNode> m_nodes;
};

} // namespace st2cpp::codegen