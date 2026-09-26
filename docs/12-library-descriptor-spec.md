# Library Descriptor — JSON v1.0 / v1.1 Specification

> Part of the [st2cpp documentation](README.md) (docs/12). Normative schema
> for the Library Descriptor consumed by `LibraryLoader` and `--ext-libs`.

## 1. Scope

A **Library Descriptor** is the serializable, JSON-based representation of an
external ST/C++ library used by the st2cpp toolchain. A descriptor declares the
library's identity, its C++ binding, the resources it exports (constants,
enums, named types, global variables, functions, function blocks) and the
initializers attached to those resources.

The descriptor is the *source of truth* for the features that will:

- resolve external symbols in the SemanticAnalyzer,
- generate C++ code for library calls,
- generate descriptors from ST sources or C++ headers.

This document specifies format versions **1.0** and **1.1** and the validation
rules applied by `LibraryLoader`. Both are accepted: 1.1 adds the function block
member and method description, and a 1.0 descriptor simply has no such sections.

## 2. Formal structure

```
LibraryDescriptor
  $schemaVersion   string      "1.0" | "1.1"
  id               string      unique, non-empty (IEC-style, case-insensitive)
  name             string      non-empty
  version          string      "MAJOR.MINOR.PATCH"[-prerelease][+build]
  description      string      optional
  dependencies     [Dependency]  optional
  cppBinding       LibraryCppBinding   optional
  constants        [Constant]          optional
  enums            [EnumTypeDef]       optional
  types            [StructTypeDef]     optional
  globalVariables  [GlobalVariable]    optional
  functions        [FunctionDef]       optional
  functionBlocks   [FunctionBlockDef]  optional

Dependency
  id        string   dependency library id
  version   string   version constraint (see 2.8)

LibraryCppBinding
  include      string   e.g. "examplelib/examplelib.hpp"
  namespace    string   e.g. "examplelib"
```

Every section and every member not marked `optional` is required; a missing
required member is a validation error.

## 2.1 Constants

```
Constant
  name           string
  type           TypeRef
  value          scalar          required
  cppBinding     { symbol }      optional
  documentation  string          optional
```

`value` must be a scalar (number, boolean, string literal such as `"T#500ms"`,
or an enumerator name). Constants are members of the *variable namespace*
shared with `globalVariables`.

## 2.2 Enums

```
EnumTypeDef
  name          string
  baseType      TypeRef         primitive integer/bitstring
  members       [ { name, value } ]   value is an integer, required
  initValue     InitValue       optional; scalar (enumerator) or default
  cppBinding    { symbol }      optional
  documentation string          optional
```

`baseType` must resolve to a primitive integer or bitstring elementary type
(`BOOL...ULINT`, `BYTE...LWORD`). The enum's `initValue`, when present, is
validated against the declared members.

Footnote: the JSON section/entities below use `"kind": "struct"` for type
definitions of structs (the only supported kind in v1.0).

## 2.3 Named types

```
StructTypeDef
  name          string
  kind          "struct"        (required in v1.0)
  fields        [ StructField ] required
  cppBinding    { symbol }      optional
  documentation string          optional

StructField
  name          string
  type          TypeRef
  initValue     InitValue   optional
  documentation string       optional
```

## 2.4 Global variables

```
GlobalVariable
  name          string
  type          TypeRef
  scope         "global"     (required in v1.0)
  constant      bool         optional (default false)
  initValue     InitValue    optional
  cppBinding    { symbol }   optional
  documentation string       optional
```

## 2.5 Functions

```
FunctionDef
  name          string
  returnType    TypeRef
  parameters    [ FunParam ]  required
  cppBinding    FunctionCppBinding   optional
  documentation string               optional

FunctionCppBinding
  kind      "freeFunction" | "staticMethod"
  symbol    string                 required
  owner     string                 required when kind == "staticMethod"
```

## 2.6 Function blocks

A function block is described in full, not only by its call interface: the
internal state (`members`) and the behaviour (`methods`) are part of the
descriptor, so a consumer can lay out and drive an instance from the descriptor
alone.

```
FunctionBlockDef
  name          string
  parameters    [ FunParam ]              required   the IN/OUT call interface
  members       [ FbMember ]              optional   internal state (1.1)
  methods       [ FbMethodDef ]           optional   behaviour      (1.1)
  baseType      string                    optional   EXTENDS target, by name
  interfaces    [ string ]                optional   IMPLEMENTS targets, by name
  isAbstract    bool                      optional   default false
  isFinal       bool                      optional   default false
  cppBinding    FbCppBinding              optional
  documentation string                    optional

FbMember
  name          string
  type          TypeRef
  storage       "VAR" | "VAR_TEMP" | "VAR RETAIN" | "VAR CONSTANT"
                                            default "VAR"
  initValue     InitValue                 optional
  documentation string                    optional
  inherited     bool                      optional   default false
  declaredIn    string                    optional   base block, when inherited

FbMethodDef
  name          string
  returnType    TypeRef                   required (VOID for a procedure)
  parameters    [ FunParam ]              optional
  visibility    "private" | "protected" | "public"
                                            default "public"
  isAbstract    bool                      optional   default false
  isFinal       bool                      optional   default false
   isOverride    bool                      optional   default false
   inherited     bool                      optional   default false
   declaredIn    string                    optional   base block, when inherited
   documentation string                    optional

FbCppBinding
  instanceType  string   required
  call          string   required

FunParam
  name          string
  type          TypeRef
  direction     "IN" | "OUT" | "IN_OUT"   default "IN"
  initValue     InitValue                 optional
  documentation string                    optional
  inherited     bool                      optional   default false
  declaredIn    string                    optional   base block, when inherited
```

Rules:

- `parameters` is the call interface. `VAR_INPUT`/`VAR_OUTPUT`/`VAR_IN_OUT`
  declarations appear there, never in `members`; `members` holds only
  `VAR`, `VAR_TEMP`, `VAR RETAIN` and `VAR CONSTANT`.
- `parameters` of a function block is **flattened over the `EXTENDS` chain** in
  the same way as `members` and `methods`: the parameters inherited from the
  base blocks come first, so the base interface stays the prefix that positional
  arguments bind against, followed by the block's own. An inherited parameter
  carries `inherited: true` and `declaredIn: "<base block>"`. A parameter
  redeclared by the block appears **once**, with the derived type, and is not
  marked as inherited.
  These flags are always absent on a function's parameters, which never inherit.
- `baseType` and each entry of `interfaces` are references **by name**: they are
  described by their own entry in the same descriptor. A base block that comes
  from another library makes that library a dependency (see 2.8).
- `members` and `methods` are **flattened over the `EXTENDS` chain**, ordered
  from the root ancestor down to the block itself, so laying out an instance
  needs no base walk. An entry contributed by a base block carries
  `inherited: true` and `declaredIn: "<base block>"`.
- A member or method that the block redeclares is emitted **once**, as the
  derived declaration. Overriding a method therefore does not produce both the
  inherited and the overriding entry.
- A member of a type provided by another library makes that library a
  dependency.

## 2.7 Type references

Type references are **structured** (not strings). Kinds:

```
Primitive   { "kind":"primitive", "name":"INT" }        elementary type name
Named       { "kind":"named",     "name":"State" }       local type
Named ext   { "kind":"named", "library":"corelib",
              "name":"Range" }                            external type
Array       { "kind":"array", "lowerBound":0, "upperBound":7,
              "elementType": TypeRef }
```

Rules:

- `Primitive` names are the IEC 61131-3 elementary type names; a name that
  maps to `VOID` is rejected.
- A `Named` reference without `library` must resolve to a locally declared enum
  or struct.
- A `Named` reference **with** `library` must (a) not reference the descriptor's
  own id and (b) reference a declared dependency (see 2.8).
- Arrays require `lowerBound <= upperBound`.

## 2.8 Dependencies and version constraints

```
Dependency
  id     string   e.g. "corelib"
  version string  e.g. "^1.0.0"
```

Dependency ids must be unique within one descriptor. Version constraints follow
a semver-style grammar:

| Example      | Meaning                                   |
|--------------|-------------------------------------------|
| `1.0.0`      | exact                                     |
| `=1.0.0`     | exact                                     |
| `^1.0.0`     | compatible (>=1.0.0, <2.0.0 for MAJOR>0) |
| `~1.2.0`     | >=1.2.0, <1.3.0                          |
| `>=1.0.0`    | greater or equal                          |
| `>1.0.0`     | greater                                   |
| `<=1.0.0`    | less or equal                             |
| `<1.0.0`     | less                                      |
| `1.x`, `*`   | wildcard (MAJOR.MINOR.PATCH with `x`)     |

The raw text is preserved in `VersionConstraint::raw`; the parsed clause list
(`VersionConstraint::clauses`) is kept so future dependency resolution can work
without re-parsing.

## 2.9 Initializers

```
Scalar    { "kind":"scalar",  "value": <scalar> }
Default   { "kind":"default" }
List      { "kind":"list",    "values":[ InitValue, ... ] }
Repeat    { "kind":"repeat",  "count":N, "value": InitValue }
Sparse    { "kind":"sparse",  "entries":[ { "range":{ "lower":L, "upper":U },
                              "value": InitValue }, ... ] , "default": InitValue }
Struct    { "kind":"struct", "values":[ { "member":"Field", "value": InitValue } ] }
```

A bare scalar (number, boolean, string) is accepted wherever a Scalar is
expected (e.g. sparse entries `"value": 100` and list values).

Rules enforced at validation time:

- scalar-init: BOOL literals must be `true|false|0|1|TRUE|FALSE`;
- enum init: scalar value must be a declared enumerator (or `default`);
- struct init: every `member` must exist, no duplicates, value validated against
  the field type;
- array init: list size ≤ capacity, repeat count ≤ capacity, sparse ranges
  within `lowerBound..upperBound` with `lower <= upper`.

## 2.10 C++ bindings

- Library level: `include` path + `namespace`.
- Constants/enums/structs/globals: optional `{ symbol }`. A binding object, when
  present, must carry a non-empty symbol.
- Functions: `freeFunction` requires a symbol; `staticMethod` requires a symbol
  and an owner.
- Function blocks: `instanceType` (the C++ class of the instance) and `call`
  (the method invoked to process one scan).

A `cppBinding` member is **optional** for functions and function blocks: a
*v1.0 semantic-only* descriptor (see §6) carries no binding. When the member is
absent the loader neither validates nor generates a binding; it fails only if a
binding member is present but structurally invalid.

## 3. Validation rules (summary)

The loader runs a structural parse followed by an order-independent validation
pass. Errors are collected (not fail-fast) as `{ path, message }` pairs, so one
load attempt can report several independent problems.

1. `$schemaVersion` must equal `1.0`.
2. `id`, `name` non-empty; `version` a valid semantic version.
3. Duplicate names:
   - within each category (constants, enums, types, globals, functions,
     function blocks) — case-insensitive;
   - across a shared *type/entity namespace* (enums, structs, function blocks,
     functions);
   - across the *variable namespace* (constants, globals);
   - nested: enum members, struct fields, function/FB parameters.
4. Duplicate dependency ids rejected.
5. Type references resolve (local named types exist; external libraries are
   declared dependencies and differ from the descriptor's own id).
6. Enum base type is an integer/bitstring primitive.
7. Array `lowerBound <= upperBound`.
8. Function `returnType` present/valid; parameter directions valid.
9. Bindings coherent with entity kind.
10. Initializers structurally compatible with the target type.
11. `scope` value is `"global"`.

A descriptor is returned by the loader with `ok() == true` only when no error
was found.

## 4. Example

See `tests/library/data/examplelib.json` (full v1.0 sample covering all the
sections: identity, dependency on `corelib`, constants, an `INT`-based enum with
init value, two structs with array/repeat initializers, four globals including a
constant sparse array, two functions — freeFunction and staticMethod — and two
function blocks with IN/OUT/IN_OUT parameters).

## 5. Round trip

`LibrarySerializer::toJson(descriptor, indent)` produces a canonical,
deterministic JSON document that the loader accepts again; the serialized form
is guaranteed to satisfy all validation rules of this spec.

## 6. Semantic-only descriptors (`LibraryDescriptorBuilder`)

`semantic::LibraryDescriptorBuilder::build(tu, semanticInfo, options)` converts
an analyzed ST translation unit into a **v1.0 semantic-only** descriptor: the
same JSON schema as above, but with **no `cppBinding`** sections. It is the
export direction of the toolchain: ST sources, not C++ headers, are the origin.
The loader consumes the result exactly as any other descriptor (round-trip
tested), so a semantic-only library can be re-imported by the analyzer.

### 6.1 Emitted sections

- **Identity**: `id`, `name`, `version`, `description` come from
  `LibraryExportOptions`. An empty `id`/`name`/`version` or an invalid
  `version` is a validation error.
- **Enums**: `baseType` is `INT` (the IEC 61131-3 default for a plain
  `TYPE Color : (...)`); members get implicit sequence values or the folded
  decimal of an explicit literal (unary `-` and `INT#`-style literals
  accepted). `initValue` is the first member.
- **Structs**: fields with their TypeRefs and scalar/list/struct initializers.
- **`VAR_GLOBAL CONSTANT`**: `constants` (scalar value required).
- **Plain `VAR_GLOBAL`**: `globalVariables` with `scope = "global"`.
- **Functions**: `returnType` + parameters from the `INPUT`/`OUTPUT`/`IN_OUT`
  sections with `direction`, plus `initValue` defaults. `VAR`/`VAR_TEMP` are
  implementation state and are intentionally not exported.
- **Function blocks**: the interface (parameters) only; state/implementation is
  internal.
- **Dependencies**: emitted whenever a referenced type (or enumerator) belongs
  to an external library. Each dependency needs a matching (case-insensitive)
  entry in `options.dependencyVersions` or the export errors out
  (`VersionConstraint::parse` on the policy).

### 6.2 Rejected / unrepresentable constructs

The builder reports an explicit export error (never silent) for:

- `PROGRAM` POUs, `INTERFACE`s, and named type aliases;
- `VAR_GLOBAL RETAIN`, `AT`-mapped declarations and section attributes;
- `FUNCTION` without a return type; `VAR_EXTERNAL`/`VAR_GLOBAL` sections inside
  a POU; `STRING[n]`/`WSTRING[n]`, `POINTER TO`, `REF_TO`, `VOID` and
  multi-dimensional array types; array bounds that are not compile-time
  constants;
- function blocks with `EXTENDS`, `IMPLEMENTS`, `ABSTRACT`, `FINAL` or methods;
- non-decimal/literal enum explicit values and non-compile-time initializers.

### 6.3 Parser note

Analysis revealed that the ST parser previously *consumed* the `CONSTANT` and
`RETAIN` section modifiers but dropped them: `VarDecl` never received
`isConstant`/`isRetain`. `parseVarSection` and `parseGlobalVarSection` now store
both flags on every declaration. This is what lets the builder distinguish a
`VAR_GLOBAL CONSTANT` block (→ `constants`) from a plain global (→
`globalVariables`) and reject `RETAIN`. Existing semantics users (body
validation, code generation) are unaffected: the flags were not read anywhere
before.

### 6.4 Determinism

The build order follows TU declaration order (enums, structs, globals, POUs)
and dependency ids are emitted sorted (normalized case-insensitive key), so
equal ST + equal options ⇒ byte-identical canonical JSON (`SameInputYieldsSameJson`).