/**
 * @file CodegenTypes.h
 * @brief Value types shared by every code generation component
 * @details The parameters and signature of a function or function block call
 * are needed both by the code generator (to emit a call) and by the semantic
 * bridge (to recover the interface of an external block from the symbol table).
 * They live here, apart from the generator itself, so no component has to
 * include CodeGenerator.h just to name a type.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace st2cpp::codegen {

/**
 * @struct CodegenResult
 * @brief Holds the generated header and source text
 */
struct CodegenResult
{
   std::string headerCode; // .hpp
   std::string sourceCode; // .cpp
};

/**
 * @brief Type of generated file
 */
enum class GenFileType {
   HEADER, // .hpp
   SOURCE, // .cpp
   MASTER  // aggregator header
};

/**
 * @brief Information about a generated file
 */
struct GeneratedFile
{
   std::string name;    // File name without extension
   std::string content; // File content
   GenFileType type;    // Type of file
   std::string subdir;  // Subdirectory (empty for root)
};

/**
 * @brief Project generation mode
 */
enum class ProjectStyle {
   FLAT,   // All files in same directory (current behavior)
   MODULAR // Organized in subdirectories with master headers
};

/**
 * @brief Information about a function / FB parameter
 *
 * Stores the parameter name, its type and whether it is an input parameter.
 */
struct ParameterInfo
{
   std::string name;
   TypeRef type;
   bool isInput;
   std::shared_ptr<Expr> defaultValue;
   bool isOutputVar; // true for OUTPUT parameters (passed as pointer)

   ParameterInfo() = default;

   ParameterInfo(const std::string& n, const TypeRef& t, bool input, std::shared_ptr<Expr> def, bool outPtr = false)
      : name(n), type(t), isInput(input), defaultValue(def), isOutputVar(outPtr)
   {}
};

/**
 * @brief Simplified signature representation for functions and FBs
 *
 * Used by the code generator to map arguments when emitting calls.
 */
struct FunctionSignature
{
   std::string name;
   std::vector<ParameterInfo> parameters;
   TypeRef returnType;
};

/**
 * @brief External FB step info for an FB invocation statement.
 * @details isFb is set only for external-library FB calls; step is the
 * descriptor's cppBinding.call.
 */
struct ExternalFbCallInfo
{
   bool isFb = false;
   std::string step;
};

} // namespace st2cpp::codegen
