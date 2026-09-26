/**
 * @file LibraryDescriptor.h
 * @brief Data model of a Library Descriptor (JSON -> C++ object model)
 *
 * The Library Descriptor is the serializable representation of an external
 * ST/C++ library (spec version 1.0). This header defines the in-memory model
 * produced by LibraryLoader and consumed by the LibraryRegistry. It is
 * deliberately independent from the SemanticAnalyzer: the two are bridged
 * through the Registry, keeping the analyzer decoupled from
 * the JSON layer.
 *
 * Type references reuse the IEC 61131-3 elementary type enum BaseType from
 * the AST so that the descriptor types stay conceptually compatible with the
 * existing type system.
 *
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

namespace st2cpp::library {

// ---------------------------------------------------------------------------
// Version & version constraints
// ---------------------------------------------------------------------------

/**
 * @brief Semantic version (semver core): MAJOR.MINOR.PATCH[-prerelease][+build]
 */
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease; // optional, without leading '-'
    std::string build;      // optional, without leading '+'
    bool valid = false;     // false when parse failed

    /**
     * @brief Parse a semantic version string.
     * @return A Version whose valid flag reflects the outcome
     */
    static Version parse(const std::string& text);

    /**
     * @brief Three-way order comparison against another version.
     * @details Compares MAJOR/MINOR/PATCH numerically and treats a prerelease
     * as lower than the same version without one (semver rule).
     * @param other The version to compare against
     * @return -1, 0 or 1
     */
    int compare(const Version& other) const;

    std::string toString() const;
};

/**
 * @brief Operator of a single version constraint clause.
 */
enum class VersionOp {
    Exact,           // "1.0.0", "=1.0.0"
    Caret,           // "^1.0.0"
    Tilde,           // "~1.2.0"
    GreaterOrEqual,  // ">=1.0.0"
    Greater,         // ">1.0.0"
    LessOrEqual,     // "<=1.0.0"
    Less,            // "<1.0.0"
    Wildcard         // "*", "1.x", "1.2.x"
};

/**
 * @brief One clause of a version constraint expression
 * (e.g. ">=1.0.0 <2.0.0" parses to two clauses).
 */
struct VersionClause {
    VersionOp op = VersionOp::Exact;
    Version version;             // wildcard components use -1
    bool valid = false;
};

/**
 * @brief A version constraint attached to a dependency.
 * @details The raw text is preserved verbatim; the parsed clause list is
 * present so that full dependency resolution can be implemented
 * without re-parsing the descriptor.
 */
struct VersionConstraint {
    std::string raw;                     // "1.0.0", "^1.0.0", ">=1.0.0 <2.0.0"
    std::vector<VersionClause> clauses;  // parsed clauses (valid when parse ok)
    bool valid = false;

    static VersionConstraint parse(const std::string& text);

    /**
     * @brief Check whether a concrete version satisfies every clause.
     * @details All clauses must hold (AND semantics). The evaluation is the
     * minimal subset needed by the project loader; the wildcard/caret/tilde
     * semantics documented in PROJECT_CONFIGURATION_SPEC.md apply.
     * @param version The concrete version to test
     * @return true when the version satisfies the constraint
     */
    bool matches(const Version& version) const;
};

// ---------------------------------------------------------------------------
// Type references
// ---------------------------------------------------------------------------

/**
 * @brief Discriminant of a TypeRef.
 */
enum class TypeRefKind {
    Primitive, // an IEC 61131-3 elementary type
    Named,     // a user-defined type (local or external via 'library')
    Array      // ARRAY[lowerBound..upperBound] OF elementType
};

/**
 * @brief Structured type reference.
 * @details Deliberately structured instead of a textual notation
 * ("ARRAY[0..7] OF Channel") so that resolution is unambiguous.
 */
struct TypeRef {
    TypeRefKind kind = TypeRefKind::Named;
    BaseType primitive = BaseType::VOID; // kind == Primitive
    std::string name;                    // kind == Named
    std::string library;                 // non-empty => external library id
    int lowerBound = 0;                  // kind == Array
    int upperBound = 0;                  // kind == Array
    std::shared_ptr<TypeRef> elementType; // kind == Array
};

// ---------------------------------------------------------------------------
// Initializers
// ---------------------------------------------------------------------------

/**
 * @brief Discriminant of an InitValue.
 */
enum class InitKind {
    None,    // no initializer
    Default, // default init of the type ("kind":"default")
    Scalar,  // scalar literal ("kind":"scalar","value":"8")
    List,    // positional list ("kind":"list","values":[...])
    Repeat,  // repeated element ("kind":"repeat","count":N,"value":{...})
    Sparse,  // range entries + optional default ("kind":"sparse")
    Struct   // member-wise init ("kind":"struct","values":[{member,value}])
};

/**
 * @brief A typed initializer tree.
 * @details Scalar values are carried as their exact textual form ("8", "0.0",
 * "false", "IDLE", "T#500ms") so both numeric and enum/string literals are
 * lossless. The tree is recursive so list/repeat/sparse/struct can nest.
 */
struct InitValue {
    InitKind kind = InitKind::None;

    // Scalar
    std::string scalar;

    // List
    std::vector<InitValue> list;

    // Repeat
    int count = 0;
    std::shared_ptr<InitValue> repeatValue;

    // Sparse
    struct SparseEntry {
        int lower = 0;
        int upper = 0;
        std::shared_ptr<InitValue> value;
    };
    std::vector<SparseEntry> entries;
    std::shared_ptr<InitValue> defaultValue; // may be null

    // Struct
    struct StructEntry {
        std::string member;
        std::shared_ptr<InitValue> value;
    };
    std::vector<StructEntry> members;
};

// ---------------------------------------------------------------------------
// C++ bindings
// ---------------------------------------------------------------------------

/**
 * @brief C++ binding at library level (include + namespace).
 */
struct LibraryCppBinding {
    std::string include; // e.g. "examplelib/examplelib.hpp"
    std::string ns;      // e.g. "examplelib"
};

/**
 * @brief C++ binding for entities mapped to a plain C++ symbol
 * (constants, enums, struct types, global variables).
 */
struct SymbolCppBinding {
    std::string symbol;
};

/**
 * @brief Discriminant of a function C++ binding.
 */
enum class FunctionBindingKind {
    FreeFunction,
    StaticMethod
};

/**
 * @brief C++ binding for a FUNCTION.
 * @details freeFunction carries only a symbol; staticMethod also requires the
 * owner ("examplelib::MathUtils") of the static method.
 */
struct FunctionCppBinding {
    FunctionBindingKind kind = FunctionBindingKind::FreeFunction;
    std::string symbol;
    std::string owner; // staticMethod only
};

/**
 * @brief C++ binding for a FUNCTION_BLOCK.
 * @details Maps the ST function block onto a C++ instance type and the method
 * invoked to step it ("examplelib::TonInstance" / "process").
 */
struct FbCppBinding {
    std::string instanceType;
    std::string call;
};

/**
 * @brief Direction discriminant shared by functions and function blocks.
 */
enum class ParamDirection {
    In,     // "IN"
    Out,    // "OUT"
    InOut   // "IN_OUT"
};

// ---------------------------------------------------------------------------
// Entity declarations
// ---------------------------------------------------------------------------

/**
 * @brief A constant (compile-time) value exported by the library.
 */
struct Constant {
    std::string name;
    TypeRef type;
    InitValue value; // scalar default/explicit "value" member
    SymbolCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    std::string documentation;
};

/**
 * @brief A single enumerator of an ENUM.
 */
struct EnumMember {
    std::string name;
    int value = 0;
};

/**
 * @brief An ENUM type defined by the library.
 */
struct EnumTypeDef {
    std::string name;
    SymbolCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    TypeRef baseType;            // primitive integer/bitstring base
    std::vector<EnumMember> members;
    InitValue initValue;         // default initializer (scalar enumerator)
    std::string documentation;
};

/**
 * @brief A field of a struct type.
 */
struct StructField {
    std::string name;
    TypeRef type;
    InitValue initValue; // optional
    std::string documentation;
};

/**
 * @brief A user-defined STRUCT type.
 */
struct StructTypeDef {
    std::string name;
    SymbolCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    std::vector<StructField> fields;
    std::string documentation;
};

/**
 * @brief Global variable or constant declared by the library.
 */
struct GlobalVariable {
    std::string name;
    TypeRef type;
    std::string scope; // "global" in the v1.0 schema
    bool constant = false;
    SymbolCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    InitValue initValue;         // optional
    std::string documentation;
};

/**
 * @brief A parameter of a function or function block.
 */
struct FunParam {
    std::string name;
    TypeRef type;
    ParamDirection direction = ParamDirection::In;
    InitValue initValue; // optional default value
    std::string documentation;
    /// True when the parameter is inherited from a base function block rather
    /// than declared here. Always false for function parameters.
    bool inherited = false;
    std::string declaredIn; // base block name, set when inherited
};

/**
 * @brief A FUNCTION exported by the library.
 * @details `cppBinding` is optional: a semantic-only descriptor (e.g. one
 * produced from a Structured Text source by the LibraryDescriptorBuilder)
 * carries no C++ binding until a later enrichment/configuration step adds it.
 * `hasCppBinding` tells whether the binding members are meaningful.
 */
struct FunctionDef {
    std::string name;
    FunctionCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    TypeRef returnType;
    std::vector<FunParam> parameters;
    std::string documentation;
};

/**
 * @brief The storage class a function block member lives in.
 * @details Mirrors the ST variable sections an FB member can be declared in.
 * Parameters (VAR_INPUT/VAR_OUTPUT/VAR_IN_OUT) are not members: they are
 * described by FunctionBlockDef::parameters instead.
 */
enum class FbMemberStorage {
    Var,      ///< VAR: state that persists across calls
    Temp,     ///< VAR_TEMP: scratch valid for the duration of one call
    Retain,   ///< VAR RETAIN: state that survives a warm restart
    Constant  ///< VAR CONSTANT
};

/**
 * @brief A state member of a function block (its internal data).
 * @details Unlike a parameter, a member is not part of the block's call
 * interface: it is declared in VAR/VAR_TEMP and is what makes two instances of
 * the same FB hold different data.
 */
struct FbMember {
    std::string name;
    TypeRef type;
    FbMemberStorage storage = FbMemberStorage::Var;
    InitValue initValue; // optional default value
    std::string documentation;
    bool inherited = false;  ///< declared by a base FB, not by this one
    std::string declaredIn;  ///< base FB name, set when inherited
};

/**
 * @brief Visibility of a function block method.
 */
enum class FbMethodVisibility {
    Private,
    Protected,
    Public
};

/**
 * @brief A method of a function block.
 */
struct FbMethodDef {
    std::string name;
    TypeRef returnType;
    std::vector<FunParam> parameters;
    FbMethodVisibility visibility = FbMethodVisibility::Public;
    bool isAbstract = false;
    bool isFinal = false;
    bool isOverride = false;
    bool inherited = false;  ///< declared by a base FB, not by this one
    std::string declaredIn;  ///< base FB name, set when inherited
    std::string documentation;
};

/**
 * @brief A FUNCTION_BLOCK exported by the library.
 * @details `cppBinding` is optional: a semantic-only descriptor (e.g. one
 * produced from a Structured Text source by the LibraryDescriptorBuilder)
 * carries no C++ binding until a later enrichment/configuration step adds it.
 * `hasCppBinding` tells whether the binding members are meaningful.
 *
 * A descriptor describes the *whole* block, not only its call interface:
 * `members` carries the internal state (VAR/VAR_TEMP, inherited ones included
 * so that a consumer can lay out an instance without walking the base chain
 * itself) and `methods` its behaviour, while `parameters` stays the IN/OUT
 * surface.
 */
struct FunctionBlockDef {
    std::string name;
    FbCppBinding cppBinding; // optional, present when hasCppBinding
    bool hasCppBinding = false;
    std::vector<FunParam> parameters;
    std::string baseType;                ///< EXTENDS target, empty when none
    std::vector<std::string> interfaces; ///< IMPLEMENTS targets
    bool isAbstract = false;
    bool isFinal = false;
    std::vector<FbMember> members;    ///< internal state, inherited included
    std::vector<FbMethodDef> methods; ///< behaviour, inherited included
    std::string documentation;
};

/**
 * @brief A dependency on another external library.
 */
struct Dependency {
    std::string id;
    VersionConstraint version;
};

// ---------------------------------------------------------------------------
// LibraryDescriptor
// ---------------------------------------------------------------------------

/**
 * @brief Root model of a Library Descriptor.
 * @details Holds identity, dependencies, C++ binding and all entity lists.
 * Provides case-insensitive (IEC 61131-3 style) lookup helpers so a loaded
 * library can be queried directly by the model consumers.
 */
class LibraryDescriptor {
public:
    std::string schemaVersion; // supported value: "1.0"
    std::string id;
    std::string name;
    std::string version;       // semantic version string
    std::string description;

    std::vector<Dependency> dependencies;
    LibraryCppBinding cppBinding;

    std::vector<Constant> constants;
    std::vector<EnumTypeDef> enums;
    std::vector<StructTypeDef> types;
    std::vector<GlobalVariable> globalVariables;
    std::vector<FunctionDef> functions;
    std::vector<FunctionBlockDef> functionBlocks;

    /**
     * @brief Case-insensitive identifier normalization (IEC 61131-3).
     */
    static std::string makeKey(const std::string& name);

    // ---- Lookup helpers (case-insensitive) ----
    const Constant* findConstant(const std::string& name) const;
    const EnumTypeDef* findEnum(const std::string& name) const;
    const StructTypeDef* findStruct(const std::string& name) const;
    const GlobalVariable* findGlobalVariable(const std::string& name) const;
    const FunctionDef* findFunction(const std::string& name) const;
    const FunctionBlockDef* findFunctionBlock(const std::string& name) const;
    const Dependency* findDependency(const std::string& id) const;

    /**
     * @brief True when a user-defined type (enum or struct) exists.
     */
    bool hasType(const std::string& name) const {
        return findEnum(name) != nullptr || findStruct(name) != nullptr;
    }
};

} // namespace st2cpp::library