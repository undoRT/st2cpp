# Library Descriptor — Import and Export (JSON v1.0)

> Part of the [st2cpp documentation](README.md) (docs/15). Describes the *new
> features* of the Library Descriptor JSON v1.0, on both sides: **import**
> (loader/serializer, optional C++ bindings) and **export**
> (`LibraryDescriptorBuilder`, ST → semantic-only descriptor → JSON). The
> normative schema lives in [`12-library-descriptor-spec.md`](12-library-descriptor-spec.md).

---

## 1. Overview

The toolchain now handles the descriptor in **both directions**:

```
IMPORT  (existing, extended)
  JSON → LibraryLoader → LibraryDescriptor → LibraryRegistry
  LibraryDescriptor → LibrarySerializer → canonical JSON (round trip)

EXPORT  (new)
  ST → Lexer/Parser → SemanticAnalyzer → TranslationUnit + SemanticInfo
     → LibraryDescriptorBuilder → LibraryDescriptor (no cppBinding)
     → LibrarySerializer::toJson → JSON
```

Both novelties concern the same `$schemaVersion: "1.0"` format:

1. **Import side:** `cppBinding` is now **optional** for `functions` and
   `functionBlocks`. A "semantic-only" descriptor — without C++ bindings — is
   valid and is accepted by the loader with `ok() == true`.
2. **Export side:** `semantic::LibraryDescriptorBuilder` builds a descriptor
   directly from analyzed ST source, without touching JSON, without re-resolving
   symbols and without inventing bindings; the result is re-importable and the
   round trip is guaranteed.

---

## 2. IMPORT side: optional C++ bindings

### 2.1 Before

In the original v1.0 model `FunctionDef::cppBinding` and
`FunctionBlockDef::cppBinding` were **required**: a function/FB JSON without a
`cppBinding` block produced a validation error, because the loader could not
distinguish "binding absent" from "binding empty".

That prevented the toolchain from representing **purely ST libraries**: the C++
default does not exist yet (it has not been generated), so a descriptor imported
from ST source could never be valid.

### 2.2 Model change

```cpp
struct FunctionDef {
    std::string name;
    TypeRef returnType;
    std::vector<FunParam> parameters;
    // ...
    bool hasCppBinding = false;             // false ⇒ no binding
    FunctionCppBinding cppBinding;
};

struct FunctionBlockDef {
    std::string name;
    std::vector<FunParam> parameters;
    // ...
    bool hasCppBinding = false;             // false ⇒ no binding
    FbCppBinding cppBinding;
};
```

### 2.3 Loader behaviour

Updated rules in `LibraryLoader`:

- `cppBinding` on a function/FB is **optional** in JSON.
- If the member is **present** but is not a JSON object ⇒ `"expected an object"`
  error (same error as for any other binding section).
- The `validateBindings` pass skips functions/FBs with `hasCppBinding == false`:
  `symbol`/`instanceType`/`call` are not checked.
- All other constraints remain unchanged (non-empty symbol, `kind`
  `freeFunction|staticMethod`, mandatory owner for `staticMethod`, etc.).

Example of a function **without** a binding (now valid):

```json
{
  "name": "Clamp",
  "returnType": { "kind": "primitive", "name": "INT" },
  "parameters": [
    { "name": "value", "type": { "kind": "primitive", "name": "INT" }, "direction": "IN" }
  ]
}
```

Example of a function **with** a binding (as before):

```json
{
  "name": "Clamp",
  "returnType": { "kind": "primitive", "name": "INT" },
  "parameters": [],
  "cppBinding": { "kind": "freeFunction", "symbol": "clamp" }
}
```

### 2.4 Serializer behaviour

`LibrarySerializer::toJson` emits the `cppBinding` block **only** when
`hasCppBinding` is `true`. A serialized semantic-only descriptor never contains
bindings — and the resulting JSON is ready to be imported again: the round trip
is tested (`SerializeThenReloadIsStable`).

The pre-existing negative tests (which used structurally invalid bindings) still
pass: the relaxation concerns only the **absent** member, not invalid members.

---

## 3. EXPORT side: `LibraryDescriptorBuilder`

### 3.1 API

In `include/semantic/LibraryDescriptorBuilder.h`, namespace `st2cpp::semantic`:

```cpp
struct LibraryExportOptions {
    std::string id;                    // library to be exported
    std::string name;
    std::string version;               // semver MAJOR.MINOR.PATCH[-pre][+build]
    std::string description;
    std::map<std::string, std::string> dependencyVersions; // library -> constraint
};

struct LibraryExportError {
    std::string entity;                // "enum Color", "function Clamp", ...
    std::string message;
    std::string toString() const;
};

struct LibraryExportResult {
    std::optional<LibraryDescriptor> descriptor; // best effort
    std::vector<LibraryExportError> errors;
    bool ok() const;                   // errors.empty()
};

// Entry point
static LibraryExportResult build(
    const TranslationUnit& tu,
    const SemanticInfo& info,
    const LibraryExportOptions& options);
```

### 3.2 Contract

- **Pure**: reads `TranslationUnit` (declaration order, names, initializers) and
  `SymbolTable`/`TypeInfo` (resolved types, external provenance). It never
  writes to the AST, never uses JSON, never re-resolves symbols.
- **No bindings**: the descriptor deliberately carries no `cppBinding`. It can
  later serve as the *source* for generating the bindings.
- **Explicit errors, never silent**: unrepresentable constructs produce a
  `LibraryExportError` with `entity` and `message`. The descriptor is still
  built (best effort) for inspection; `ok()` tells whether it is trustworthy.
- **Symbols by name**: the analysis does not fill `pou.symbolId`/
  `decl.symbolId`, so the builder locates a POU/global/member/parameter through
  `lookupGlobal` and name-alignment with the `params`/`members` lists.

### 3.3 Internal pipeline (simplified)

```
run()
 ├─ desc.schemaVersion = "1.0"; identity from options; metadata validation
 ├─ exportEnum(e)      for each enum in the TU
 ├─ exportStruct(s)    for each struct in the TU
 ├─ exportGlobals()    VAR_GLOBAL sections (constant vs plain)
 ├─ exportPou(pou)     FUNCTIONS and FUNCTION_BLOCKs (in order)
 ├─ exportInterface/exportTypeAlias → explicit errors
 └─ dependencies sorted by normalized key → desc.dependencies
```

### 3.4 Emitted sections and provenance

| Descriptor section | ST origin | Notes |
| --- | --- | --- |
| `enums` | `TYPE C : (a, b := 5, c); END_TYPE` | `baseType` = `INT` (the IEC default for an enum without a base); implicit consecutive values; explicit values are folded (decimal, unary `-`, `INT#` prefixes) |
| `types` | `TYPE S : STRUCT ... END_STRUCT END_TYPE` | fields with TypeRef and optional init |
| `constants` | `VAR_GLOBAL CONSTANT k : T := lit; END_VAR` | **mandatory scalar** value |
| `globalVariables` | `VAR_GLOBAL ... END_VAR` | `"scope": "global"`, `constant: false` |
| `functions` | `FUNCTION F : T ... END_FUNCTION` | return type + parameters from `VAR_INPUT`/`VAR_OUTPUT`/`VAR_IN_OUT`, declaration order, defaults on initializers |
| `functionBlocks` | `FUNCTION_BLOCK FB ... END_FUNCTION_BLOCK` | **interface only** (parameters); state/implementation are internal |
| `dependencies` | **external** types and enumerators resolved via the registry | mandatory policy, see §3.7 |

### 3.5 Initializers

The ST initializer expression is converted into an `InitValue` with these rules:

| ST expression | Emitted InitValue | JSON example |
| --- | --- | --- |
| numeric or boolean literal | `scalar` (number/bool) | `-5`, `8` |
| string `'...'` / `WSTR#"..."` | `scalar` (string, without quotes) | `"Hello World"` |
| `TRUE` / `FALSE` | `scalar` | `true` / `false` |
| `-<literal>` | `scalar` with sign | `-3` |
| identifier resolving to an enumerator | `scalar` = member name (external ⇒ dependency) | `"Green"` |
| `[a, b, c]` | `list` | `{ "kind": "list", "values": [...] }` |
| `(m := v, ...)` | `struct` | `{ "kind": "struct", "values": [...] }` |

In serialization numeric values stay numbers (`numberRaw`), booleans stay
`true|false`, strings keep their quotes: the JSON→loader→JSON round trip is
lexically stable.

### 3.6 Enumerations

- `baseType.primitive = INT` (the ST dialect only supports `TYPE x : (...)`).
- Members without an explicit value continue the sequence from the previous
  value.
- A non-decimal explicit value (hex, identifier, ...) ⇒ error
  `"enum X.Y: explicit value is not a decimal integer literal"`.
- `initValue` = first member (scalar, the enumerator name).

### 3.7 Dependencies and policy

- **When**: a type reference resolves to an external symbol
  (`Symbol::isExternal`, `externalLibraryId`) or an initializer uses an external
  enumerator. The external library enters `dependencies`.
- **Mandatory policy**: every dependency must have an entry in
  `options.dependencyVersions`; the match is **case-insensitive** (keys
  normalized to uppercase, like `LibraryDescriptor::makeKey`). Missing ⇒ error
  `"missing version policy for dependency 'x'"`.
- **Constraint**: the value is validated with `VersionConstraint::parse`;
  invalid ⇒ error `"invalid version constraint '...'"`.
- **Dedup and order**: an external library counts once; dependencies are emitted
  sorted by normalized key (determinism).
- **Self-reference**: a library name equal to the exported `id` ⇒ error.

Example: `options.dependencyVersions` with `"timerlib"` and a reference to
`Mode` from `timerlib` produce:

```json
"dependencies": [
  { "id": "timerlib", "version": "^1.0.0" }
]
```

and the associated TypeRef becomes:

```json
{ "kind": "named", "name": "Mode", "library": "timerlib" }
```

### 3.8 Rejected constructs (never silent)

| Construct | Reported entity (e.g.) | Error |
| --- | --- | --- |
| `PROGRAM` | `program Main` | `PROGRAM POUs are not representable` |
| `INTERFACE` | `interface IFx` | `INTERFACE declarations are not representable` |
| alias | `type alias MyInt` | `named type aliases are not representable` |
| `VAR_GLOBAL RETAIN` | `global keep` | `RETAIN variables are not representable` |
| `AT %MW10` on a global | `global mapped` | `AT (externally mapped) variables are not representable` |
| section attributes | `globals` | `variable section attributes are not representable` |
| `STRING[n]`/`WSTRING[n]` | `struct Bag.name` | `STRING[n]/WSTRING[n] lengths are not representable` |
| `POINTER TO` / `REF_TO` / `VOID` | affected TypeRef | `... are not representable` |
| multi-dimensional array | global/parameter | `multi-dimensional arrays are not representable` |
| non-constant array bounds | TypeRef | `array bounds are not compile-time constants` |
| `FUNCTION` without return type | `function Bad` | `a function requires a return type` |
| `VAR_EXTERNAL`/`VAR_GLOBAL` inside a POU | `function F` | non-representable sections |
| FB with `EXTENDS`/`IMPLEMENTS`/`ABSTRACT`/`FINAL`/methods | `function block Dev` | specific error + **the FB is not exported** |
| enum with non-decimal value | `enum C.B` | `explicit value is not a decimal integer literal` |
| non-compile-time initializer | `global g` / `constant k` / parameter | `... is not a compile-time ...` |

### 3.9 Determinism and round trip

- Emission order = TU declaration order (types → globals → POUs); metadata and
  sorted dependencies.
- Identical ST + identical `options` ⇒ **identical canonical JSON**
  (test `SameInputYieldsSameJson`).
- Every produced descriptor goes through `LibrarySerializer::toJson` →
  `LibraryLoader::fromString` and the reloaded JSON must be byte-identical
  (test `SerializeThenReloadIsStable`).

### 3.10 CLI: `--export-descriptor`

The export pipeline is exposed on the command line:

```
st2cpp lib.st --export-descriptor lib.json \
    --lib-id timerlib --lib-name TimerLib --lib-version 1.0.0
```

| Option | Meaning |
| --- | --- |
| `--export-descriptor <file.json>` | Destination JSON file |
| `--lib-id <id>` | Library id (required) |
| `--lib-name <name>` | Library name (required) |
| `--lib-version <semver>` | Library version, semver (required) |
| `--lib-description <text>` | Optional library description |
| `--lib-dependency <id>=<constraint>` | Version policy for an external dependency (repeatable, e.g. `timerlib=^1.0.0`) |

Semantics:

- works on a **single `.st` file** or on a whole **`--workspace`** (all files are
  merged and de-duplicated as one library, mirroring project-style conflation);
- may be combined with `--ext-libs project.json`: external references then
  resolve against the registry and enter `dependencies` — each one requires a
  matching `--lib-dependency` policy;
- export errors print as `Export error:` lines and the process exits `1`; the
  descriptor is still written best-effort for inspection (mirrors §3.2);
- missing `--lib-id`/`--lib-name`/`--lib-version` or a malformed
  `--lib-dependency <id>=<constraint>` is a usage error (exit `1`).

The flags above populate exactly the `LibraryExportOptions` of §3.1.

---

## 4. Complete example

ST source:

```st
TYPE Color : (Red, Green := 5, Blue); END_TYPE

TYPE Point : STRUCT
   x : INT;
   tag : STRING;
END_STRUCT END_TYPE

VAR_GLOBAL CONSTANT MAX_CHANNELS : INT := 8; END_VAR

VAR_GLOBAL
   counter : INT := 1;
   row : ARRAY[0..2] OF INT := [1, 2, 3];
END_VAR

FUNCTION Clamp : INT
VAR_INPUT value, lo, hi : INT; END_VAR
VAR_OUTPUT result : INT; END_VAR
END_FUNCTION

FUNCTION_BLOCK Ton
VAR_INPUT in : BOOL; END_VAR
VAR acc : TIME; END_VAR
END_FUNCTION_BLOCK
```

Call from the application (or from tests, via `TestHelper`), or from the CLI —
`st2cpp lib.st --export-descriptor lib.json --lib-id mylib --lib-name MyLib
--lib-version 1.2.3 --lib-description "Exported ST library"`:

```cpp
st2cpp::semantic::LibraryExportOptions opts;
opts.id = "mylib";
opts.name = "MyLib";
opts.version = "1.2.3";
opts.description = "Exported ST library";

auto res = TestHelper::buildDescriptorFromST(st, opts);
assert(res.ok());
std::string json = st2cpp::library::LibrarySerializer::toJson(*res.descriptor, 2);
```

Produced JSON (canonical, already accepted by the loader):

```json
{
  "$schemaVersion": "1.0",
  "id": "mylib",
  "name": "MyLib",
  "version": "1.2.3",
  "description": "Exported ST library",
  "constants": [
    {
      "name": "MAX_CHANNELS",
      "type": { "kind": "primitive", "name": "INT" },
      "value": 8
    }
  ],
  "enums": [
    {
      "name": "Color",
      "baseType": { "kind": "primitive", "name": "INT" },
      "members": [
        { "name": "Red", "value": 0 },
        { "name": "Green", "value": 5 },
        { "name": "Blue", "value": 6 }
      ],
      "initValue": { "kind": "scalar", "value": "Red" }
    }
  ],
  "types": [
    {
      "name": "Point",
      "kind": "struct",
      "fields": [
        { "name": "x", "type": { "kind": "primitive", "name": "INT" } },
        { "name": "tag", "type": { "kind": "primitive", "name": "STRING" } }
      ]
    }
  ],
  "globalVariables": [
    {
      "name": "counter",
      "type": { "kind": "primitive", "name": "INT" },
      "scope": "global",
      "constant": false,
      "initValue": { "kind": "scalar", "value": 1 }
    },
    {
      "name": "row",
      "type": {
        "kind": "array",
        "lowerBound": 0,
        "upperBound": 2,
        "elementType": { "kind": "primitive", "name": "INT" }
      },
      "scope": "global",
      "constant": false,
      "initValue": {
        "kind": "list",
        "values": [
          { "kind": "scalar", "value": 1 },
          { "kind": "scalar", "value": 2 },
          { "kind": "scalar", "value": 3 }
        ]
      }
    }
  ],
  "functions": [
    {
      "name": "Clamp",
      "returnType": { "kind": "primitive", "name": "INT" },
      "parameters": [
        { "name": "value", "type": { "kind": "primitive", "name": "INT" }, "direction": "IN" },
        { "name": "lo", "type": { "kind": "primitive", "name": "INT" }, "direction": "IN" },
        { "name": "hi", "type": { "kind": "primitive", "name": "INT" }, "direction": "IN" },
        { "name": "result", "type": { "kind": "primitive", "name": "INT" }, "direction": "OUT" }
      ]
    }
  ],
  "functionBlocks": [
    {
      "name": "Ton",
      "parameters": [
        { "name": "in", "type": { "kind": "primitive", "name": "BOOL" }, "direction": "IN" }
      ]
    }
  ]
}
```

Note: no `cppBinding` sections — that is the signature of a semantic-only
descriptor (§2.3). `VAR acc : TIME` (internal FB state) does not appear:
`VAR`/`VAR_TEMP` declarations of a POU are not exported.

---

## 5. Importing the export: combined use

An exported descriptor can be registered and reused like any normal library:

```cpp
st2cpp::library::LibraryRegistry registry;
auto load = st2cpp::library::LibraryLoader::fromString(json);   // same JSON
assert(load.ok());

std::string err;
assert(registry.registerLibrary(*load.descriptor, err));        // registered

// ... and resolved again by semantic analysis as an external library
auto res2 = TestHelper::buildDescriptorFromST(otherSt, opts, registry);
```

This closes the loop: **ST export → JSON → re-import → resolution**.

---

## 6. Notes related to the format

- `baseTypeName` for primitive types uses the IEC names (`INT`, `REAL`,
  `STRING`, `TIME`, ...).
- JSON conversion of `scalar` values is automatic: numbers → numbers
  (`numberRaw`), `true|false` → booleans, everything else → string (e.g.
  `"T#500ms"`).
- The parser `CONSTANT`/`RETAIN` flags are now propagated to the `VarDecl`: this
  is what lets the builder distinguish `constants` from `globalVariables` and
  reject `RETAIN`. No pre-existing consumer read those flags, so the behaviour
  is backward-compatible.

## 7. Known limits and debts

- Enum base type is fixed to `INT` (no parser change for explicit bases).
- `STRING[n]`, pointers/refs and multi-dimensional arrays are **rejected**, not
  truncated.
- No `documentation`/`cppBinding` is emitted (they are not invented by the
  semantic-only builder).
- B2 (`Symbol*` pointers saved before `TypeInfo` registration during imports
  with collisions) remains a documented debt.

## 8. References

- [`12-library-descriptor-spec.md`](12-library-descriptor-spec.md) — normative
  JSON v1.0 schema (includes §6 "Semantic-only descriptors").
- [`11-external-libraries.md`](11-external-libraries.md) — external libraries,
  registry and `--ext-libs`.
- [`06-semantic-analysis.md`](06-semantic-analysis.md) — `SemanticAnalyzer`,
  `SemanticInfo`, `SymbolTable`.
