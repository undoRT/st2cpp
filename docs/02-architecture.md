# Architecture

This document describes how the compiler is split into modules, how the CMake
targets map to them, and the structure of the AST that flows through the
pipeline.

## Module map

| CMake target | Namespace | Sources | Role |
|--------------|-----------|---------|------|
| `st2cpp_lib` | `st2cpp` | `src/lexer`, `src/parser`, `src/codegen` | Lexing, parsing, code generation |
| `st2cpp_json` | `st2cpp::json` | `src/json/JsonValue.cpp` | Minimal self-contained JSON DOM/parser/serializer |
| `st2cpp_library` | `st2cpp::library` | `src/library/*` | Library Descriptor model, loader, serializer, registry |
| `st2cpp_project` | `st2cpp::project` | `src/project/*` | Project JSON config loader + library loading |
| `st2cpp_semantic` | `st2cpp::semantic` | `src/semantic/*` | Semantic analysis, symbol table, diagnostics, external-symbol importer |
| `st2cpp` | — | `src/cli/main.cpp` | The command-line executable |
| `st2cpp_tests` | — | `tests/*` | GoogleTest suite (only with `BUILD_TESTS=ON`) |

Target interdependencies (CMake, `PUBLIC` links):

```
st2cpp_json  ◀── st2cpp_library
st2cpp_library ◀── st2cpp_project
st2cpp_lib + st2cpp_library ◀── st2cpp_semantic
st2cpp_semantic + st2cpp_lib + st2cpp_project ◀── st2cpp (CLI)
```

`st2cpp_semantic` depends on the *external-library model* (`st2cpp_library`)
because `SemanticAnalyzer::analyze(tu, registry, strictness)` imports symbols
from `LibraryRegistry`. The CodeGenerator consumes library binding data
through `SemanticInfo::libraryRegistry` (non-owning pointer).

## Header layout

```
include/
  ast/AST.h                    # AST nodes, BaseType, TypeRef, TranslationUnit
  lexer/Token.h  Lexer.h
  parser/Parser.h
  codegen/CodeGenerator.h      # CodegenResult, GeneratedFile, ProcessImageConfig
  semantic/ SemanticAnalyzer.h SemanticInfo.h SymbolTable.h TypeSystem.h
            DeclVisitor.h BodyVisitor.h Diagnostics.h LibrarySymbolImporter.h
  json/ JsonValue.h
  library/ LibraryDescriptor.h LibraryLoader.h LibraryRegistry.h LibrarySerializer.h
  project/ ProjectConfig.h ProjectConfigLoader.h ProjectLoader.h
```

## The AST (`include/ast/AST.h`)

The AST is a single translation unit (`TranslationUnit`) aggregating
homogeneous vectors:

```cpp
struct TranslationUnit {
  std::vector<StructDef>   structs;
  std::vector<EnumDef>     enums;
  std::vector<InterfaceDef> interfaces;
  std::vector<POU>         pous;      // functions, function blocks, programs
  std::vector<GlobalVarDef> globals;
};
```

`POU` (guideline shape):

- `pouType` — `FUNCTION | FUNCTION_BLOCK | PROGRAM` (+ methods, properties);
- name, return type, base class (`EXTENDS`), implemented interfaces
  (`IMPLEMENTS`), `VAR_*` sections, body statements.

Elementary types are modelled by the global `enum class BaseType`
(`BOOL…ULINT`, `REAL/LREAL`, `BYTE…LWORD`, `STRING/WSTRING`, `TIME/DATE/DT/TOD`,
`VOID`, `NAMED`). `TypeRef` is a structured type reference:

```cpp
struct TypeRef {
  BaseType base = BaseType::NAMED;
  std::string name;          // NAMED or user-defined type
  bool isPointer = false;    // POINTER TO
  bool isRefTo = false;      // REF_TO
  std::vector<ArrayDim> dims; // array bounds
};
```

Statement and expression payloads are `std::shared_ptr<Expr>`/statement nodes
with an `ExpressionKind`/`StatementKind` discriminator; semantic analysis
decoration is stored on the nodes and in `SymbolTable`.

## Data flow

```
source text ─▶ Lexer ─▶ vector<Token> ─▶ Parser ─▶ TranslationUnit
                                                       │
                                   ┌───────────────────┼───────────────────┐
                                   ▼                   ▼                   ▼
                        (optionally)           SemanticAnalyzer    ProcessImageAnalyzer
                        LibraryRegistry ──▶ analyze(tu, reg)
                                   │                   │
                                   │            SemanticInfo
                                   ▼                   ▼
                              CodeGenerator ──▶ GeneratedFile(s) ──▶ disk
```

`SemanticInfo` (owned by the analysis result) carries:

- `symbolTable` (`std::unique_ptr<SymbolTable>`) — decorated symbol model;
- `diagnostics` — errors/warnings with source locations;
- topological orders (`fbTopoOrder`, `structTopoOrder`), base-class and
  interface maps for FB inheritance;
- resolved addresses for `AT %…` placeholders;
- process-image configuration;
- `preservedSemantics` (run strictness + resolution counts) — `preserved()`
  is `true` only when the analyzer ran and all references resolved;
- `libraryRegistry` (nullable, non-owning) — back-reference to the bindings.

## Naming and case sensitivity

- ST identifiers are case-insensitive (IEC). The semantic layer and library
  model use normalized (uppercase) keys.
- `--caseSensitive` keeps the original casing of generated namespace members;
  by default output identifiers are converted to uppercase.
- CLI output identifiers that come from `cppBinding.symbol` are emitted
  verbatim, preserving exact C++ symbol names.