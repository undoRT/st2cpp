# Overview

**st2cpp** translates IEC 61131-3 **Structured Text (ST)** into clean, native
**C++17**. There is no VM and no interpreter: the result is standard C++ that
compiles with any C++17 compiler and can be debugged, unit-tested and
integrated with native libraries directly.

## Design goals

- **No runtime overhead** — generated code is plain C++ data members and
  methods; the only runtime is a small collection of headers
  (`undoCore`) for the process-image and type mappings.
- **Self-contained** — no third-party dependency at build time: the JSON parser
  used by the external-library subsystem is implemented inside the project
  (`st2cpp::json`).
- **Offline analysis** — a semantic analyzer builds a symbol table, type-checks
  the program and feeds the code generator, improving output quality over a
  purely syntactic pass.
- **Gradual adoption** — the analyzer runs by default in *Permissive* mode:
  diagnostics are surfaced but never block generation; `--strict` turns it into
  a fail-fast mode.

## The pipeline

```text
 .st source                       Library Descriptors (JSON)
      │                                     │
      ▼                                     ▼
 ┌──────────┐   tokens   ┌─────────┐   Project Configuration (JSON)
 │  Lexer   │───────────▶│  Parser │◀──────────────┐
 └──────────┘            └─────────┘               │
                             │ AST                 │
                             ▼                     ▼
                     ┌─────────────────┐   ProjectConfigLoader
                     │ SemanticAnalyzer│◀────────ProjectLoader
                     └─────────────────┘        (LibraryRegistry)
                             │ SemanticInfo
                             ▼
                     ┌─────────────────┐
                     │  CodeGenerator  │
                     └─────────────────┘
                             │
                             ▼
                     generated C++ (.hpp/.cpp)
```

Stages:

1. **Lexing** (`src/lexer/Lexer.cpp`) — tokenizes ST into a
   `std::vector<Token>`.
2. **Parsing** (`src/parser/Parser.cpp`, `ParserStmt.cpp`) — produces the AST
   (`include/ast/AST.h`): POUs, structs, enums, interfaces, globals.
3. **Semantic analysis** (`src/semantic/`) — two passes (declarations then
   bodies), symbol table with case-insensitive IEC lookup, type checking,
   diagnostics with `file:line:col`. When `--ext-libs` is given, external
   symbols are imported from a `LibraryRegistry` into a dedicated *external
   scope*.
4. **Code generation** (`src/codegen/CodeGenerator.cpp`) — emits C++17: either
   one header/source pair or a modular project (files per FB).
5. **Optional native compilation** — the generated code is ordinary C++ you
   compile into your application.

## Pipelines in detail

- **Single file**: `st2cpp program.st` → `program.hpp` / `program.cpp`.
- **Flat workspace**: `st2cpp --workspace ./plc` → one `.hpp`/`.cpp` pair per
  `.st` file in `--output-dir`.
- **Project style**: `st2cpp --workspace ./plc --project-style` → modular
  layout (`GVLs.hpp`, `Functions.*`, `FunctionBlocks/*.*`, `Programs.*`) with
  topological FB ordering.
- **External libraries**: `--ext-libs project.json` can be combined with any of
  the above; the declared libraries are registered and their symbols resolved
  at analysis time. See `11-external-libraries.md`.

## Repository layout

```text
├── CMakeLists.txt            # build orchestration (targets, tests, undoCore)
├── include/                  # public headers, one folder per module
│   ├── ast/  AST.h           # AST + elementary type enum (BaseType)
│   ├── lexer/  parser/       # frontend
│   ├── codegen/              # CodeGenerator, CodegenResult, GeneratedFile
│   ├── semantic/             # analyzer, visitors, symbol table, diagnostics
│   ├── json/                 # st2cpp::json (DOM, parse, dump)
│   ├── library/              # LibraryDescriptor, Loader, Serializer, Registry
│   └── project/              # ProjectConfig, ProjectConfigLoader, ProjectLoader
├── src/                      # implementations + CLI (src/cli/main.cpp)
├── tests/                    # GoogleTest suite + helpers + fixtures
├── examples/                 # runnable examples
├── st2cpp_includes/undoCore/ # header-only runtime
└── docs/                     # this documentation
```

## Stylistic facts

- Primary namespace: `st2cpp::…`. The CLI binary is called `st2cpp`.
- Elementary IEC types live in a global `enum class BaseType` in
  `include/ast/AST.h` and are reused across all modules.
- Identifiers are case-insensitive in `st2cpp` (IEC 61131-3): a
  `makeKey()`-style normalization is used by the semantic layer and the
  library registry.
