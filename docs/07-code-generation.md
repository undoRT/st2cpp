# Code Generation

`src/codegen/CodeGenerator.cpp` turns the analyzed `TranslationUnit` +
`SemanticInfo` into C++17. The public surface is in
`include/codegen/CodeGenerator.h`.

## API

```cpp
struct GeneratedFile {
  std::string relativePath;  // e.g. "Programs/MyProgram.hpp"
  std::string content;
};

struct CodegenResult {
  bool ok = false;
  std::vector<GeneratedFile> files;
  std::string error;         // populated on failure
};

CodegenResult generate(const TranslationUnit& tu,
                       const SemanticInfo& sem,
                       const std::string& nsName = "undoCore",
                       bool upperCaseIdentifiers = true,
                       const std::string& runtimeHeader = "undoCore/undoCore.hpp");

CodegenResult generateModularProject(const std::vector<TranslationUnit>& units,
                                     const SemanticInfo& sem,
                                     const std::string& nsName = "undoCore",
                                     bool upperCaseIdentifiers = true,
                                     const std::string& runtimeHeader = "undoCore/undoCore.hpp");
```

The CLI wraps these two entry points. `generate` is single-file;
`generateModularProject` is project-style (see below).

## Output shape

### Single file (`generate`)

One header + one source:

- The header declares the namespace (`undoCore` by default), all POUs (structs
  first, then FBs/Functions, then Programs), enums/typedefs, and externs.
- The source implements function bodies, FB methods/`process()` chains, and
  programs.
- `#include` directives: the chosen runtime header first, then — when external
  libraries are registered (`SemanticInfo::libraryRegistry`) — a dedicated
  include per library namespace (e.g. `#include "core/core.hpp"`,
  `#include "io/io.hpp"` resolved from the descriptor's `cppHeader`).

Identifier emission is controlled by `upperCaseIdentifiers`: on (default) ST
names become uppercase (`MyVar` → `MYVAR`); with `--caseSensitive` casing is
preserved. **`cppBinding.symbol` values are emitted verbatim**, so
cross-library C++ symbols always keep their exact name.

FBs become classes with:

- members for `VAR`, `VAR_INPUT`, `VAR_OUTPUT`, `VAR_IN_OUT` (typed with the
  runtime aliases),
- `ENABLE`/`EN` + `process()`/`RUN()` rendering of the body,
- getter accessors for interface/IO members (`get_X()`),
- inheritance from declared base FBs/interfaces (`: public Base`).

Functions become free functions (or static members when declared as a library
`staticMethod`); default End-of-Body implicit `RETURN` handling converts ST
`RETURN;` into an early exit.

### Process-image

`AT %I/%Q/%M` declarations are emitted as bytes into `ProcessImageConfig`-sized
buffers (see `08-process-image.md`), with typed getter/setter accessors.

### Project style (`generateModularProject`)

```
gen/
  GVLs.hpp                       # global variable declarations
  Functions.hpp                  # master FB/function header
  Functions.cpp
  FunctionBlocks.hpp             # includes each FB header
  FunctionBlocks/
    Averager.hpp                 # one FB per file
    Averager.cpp
  Programs.hpp
  Programs.cpp
```

The project-style path deep-merges the per-file units, drops duplicate
declarations, topologically orders FBs (`sem.fbTopoOrder` / base-class and
interface maps) so that derived classes are declared after their bases, and
distributes each POU over its own files with a master include per area.

## What the codegen does NOT do

- It does **not** run a C++ compiler: the generated C++ is validated by the
  tests (`test_compilation`) and the examples via `g++ -fsyntax-only`.
- It does **not** reconcile `VAR_EXTERNAL` across translation units by itself:
  in single-file mode externals are emitted as externs; cross-file linkage is
  the project-style merge's job.
- It does **not** invent bindings for unresolvable symbols: in Permissive mode
  those constructs degrade to legacy syntactic inference (still emitted, no
  semantic decoration); in Strict mode they block generation.