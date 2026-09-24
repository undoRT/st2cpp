# Semantic Analysis

`src/semantic/` is a two-pass analyzer that decorates the parsed AST with typed,
resolved symbols and produces diagnostics. It is exposed both as a CMake target
(`st2cpp_semantic`) and through the CLI (`--strict`, `--ext-libs`).

## Entry points

```cpp
SemanticInfo analyze(const TranslationUnit& tu,
                     Strictness strictness = Strictness::Permissive);

SemanticInfo analyze(const TranslationUnit& tu,
                     const st2cpp::library::LibraryRegistry& registry,
                     Strictness strictness = Strictness::Permissive);
```

`Strictness` (`include/semantic/SemanticAnalyzer.h`):

- `Permissive` (default) — collect diagnostics, never block generation;
  unresolved references degrade to legacy syntactic inference in the codegen.
- `Strict` — print all diagnostics and abort generation on the first error.

## Passes

1. **Declaration pass** (`DeclVisitor`) — walks every POU, struct, enum,
   interface and global; registers all *names* into the symbol table and
   produces the binding data structures the later passes read. No body
   expressions are traversed here.
2. **Body pass** (`BodyVisitor`) — type-checks statement and expression nodes
   against the symbol table, resolving overloads, members, array dims and
   `AT` addresses.

Both passes run over the TranslationUnit built by the parser and can consume an
external `LibraryRegistry` (see below).

## Symbol table (`include/semantic/SymbolTable.h`)

- `st2cpp` resolves ST identifiers **case-insensitively** (IEC 61131-3): all
  keys pass through `makeKey()` normalization.
- Hierarchy-aware: global scope → POU scope → block scope; guarded by
  `ScopeGuard` in the visitors.
- Tracks POU symbols (function/FB/program signatures), variable symbols with
  types, enum constants, struct members, interface method contracts, external
  symbols imported from libraries.

## Type system (`include/semantic/TypeSystem.h`)

- Resolves `TypeRef` to an actual type (elementary, array, derived struct,
  enum, pointer/ref) and links array dims to constants when resolvable.
- Implicit conversion model (`CastResult`): widens integers
  (`INT`→`DINT`→`LINT`, signed↔unsigned with rank rules), floats
  (`REAL`→`LREAL`), and rejects narrowing/ambiguous casts with diagnostics.
- Writes into the AST nodes the resolved type decisions the codegen consumes.

## Diagnostics (`include/semantic/Diagnostics.h`)

Diagnostics carry severity (`Error`/`Warning`), a message, and a source
location (`file`, `line`, `column`). The analyzer fills
`SemanticInfo::diagnostics`; the CLI prints them (always under `--strict`,
additionally with `-v` otherwise).

## External symbols and libraries

`LibrarySymbolImporter` injects into the symbol table everything an external
library contributes: namespace bindings, types/constants for `TYPE` reuse,
free functions, FB/method signatures, global definitions. It receives the
qualified names the codegen needs (namespace + `cppBinding.symbol` for methods
and functions) and produces the `ExternalSymbol` entries used by `BodyVisitor`
to type-check call sites (e.g. `ScaleAnalog(chn.raw, 1.0)` resolves to the
`namespace.symbol` pair emitted by the codegen).

When libraries are present:

```
LibraryRegistry ──▶ analyze(tu, registry, strictness)
                        │
                        ├─ while resolving a name not found locally,
                        │   look it up in registered libraries
                        ▼
   SemanticInfo { symbolTable, diagnostics, libraryRegistry (back-ref), … }
```

`SemanticInfo::libraryRegistry` is a **non-owning** back-reference; the codegen
uses it to emit `#include` directives for each library namespace plus the exact
binding calls. Callers that create a `SemanticInfo` for the C API can attach a
registry later; when `libraryRegistry == nullptr` the codegen falls back to
legacy inference.

## Process-image resolution

`ProcessImageAnalyzer::analyze` scans `AT %I/%Q/%M` placeholders, derives the
required buffer sizes (overridable in `ProcessImageConfig`) and bakes the
byte offsets into `SemanticInfo` so the codegen can emit the byte-array-backed
pinning headers (`UDINT`, `DINT`, … getter/setter accessors). See
`08-process-image.md`.

## Output object — `SemanticInfo`

- `symbolTable` — decorated symbol model;
- `diagnostics` — errors/warnings with locations;
- `fbTopoOrder`, `structTopoOrder` — topological orders for codegen;
- base-class map and interface map (for FB inheritance) ;
- resolved `AT` addresses + `ProcessImageConfig`;
- `preservedSemantics` — run strictness + total reference counts;
  `preserved()` is `true` only when the analyzer ran and every reference
  resolved;
- `libraryRegistry` — optional back-reference for external bindings.