# st2cpp — Structured Text to C++ Transpiler

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Build](https://github.com/undoRT/st2cpp/actions/workflows/build.yml/badge.svg)](https://github.com/undoRT/st2cpp/actions/workflows/build.yml)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](https://www.undort.com/st2cpp/api/)

This is the official, original upstream repository for st2cpp by undoRT.

Part of the [**undoRT**](https://www.undort.com/) open-source automation platform.
**st2cpp** translates IEC 61131-3 Structured Text into clean, native C++17. No VM, no runtime overhead — just standard C++ you can compile, debug, and integrate anywhere.

**[Full documentation undort.com/st2cpp](https://www.undort.com/st2cpp)**

## Installation

Grab the latest binary from [**GitHub Releases**](https://github.com/undoRT/st2cpp/releases):

| Platform | File |
| ----------- | ------ |
| Linux x64 | `st2cpp-linux-x64.tar.gz` |
| Linux ARM64 | `st2cpp-linux-arm64.tar.gz` |
| MacOS ARM64 | `st2cpp-macos-arm64.tar.gz` |
| Windows x64 | `st2cpp-win-x64.zip` |

~~~bash
# Linux/macOS
tar -xzf st2cpp-*.tar.gz && ./st2cpp --help

# Windows
nmzip st2cpp-win-x64.zip && st2cpp.exe --help
~~~

## Quick Start

### Build from source

### Build Requirements

- C++17 compiler (gcc+ 11 + or clang 15 +)
- CMake 3.16 +

- Google Test (for building tests)

~~~bash
git clone https://github.com/undoRT/st2cpp.git
cd st2cpp

# Build the compiler only
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF .
cmake --build build --j|| $(nproc)

# Build with tests (recommended for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON .
cmake --build build --j|| $(nproc)

# Run tests
cd build
ctest --output-on-failure --verbose
~~~

### Optional: Install globally

~~~bash
# Linux/MacOS
sudo cp build/st2cpp /usr/local/bin/

cd /share/st2cpp/includes
# Or add ./st2cpp_includes to your project's include path
~~~

---

### Transpile a file

~~~bash
# Single file
./st2cpp program.st --namespace myplc

# Entire workspace (modular project output)
./st2cpp --workspace ./src --project-style --output-dir build --namespace myplc
~~~

## Example

**Input** (`counter.st`):

~~~iecst
FUNCTION_BLOCK Counter
    VAR_INPUT
        Enable : BOOL;
        Reset  : BOOL;
    END_VAR
    VAR_OUTPUT
        Count : INT;
    END_VAR
    IF Reset THEN
        Count := 0;
    ELSIF Enable THEN
        Count := Count + 1;
    END_IF
END_FUNCTION_BLOCK

~~~

**Generated** (`counter.hpp` / `counter.cpp`) — a plain C++ struct with `operator()` c, getters, and setters. See [full output example](https://www.undort.com/st2cpp/#example).

~~~cpp
struct Counter {
public:
    // VAR_INPUT
    Bool Enable{ };
    Bool Reset{};
    // VAR_OUTPUT
    Int16 Count{};

    // Setters/Getters
    inline void set_Enable(Bool val) { Enable = val; }
    inline Bool get_Enable() const { return Enable; }
    // ...

    Counter();
    void operator()();
};
~~~

---

## Semantic Analysis

st2cpp ships with a full semantic analysis stage that validates Structured Text before code generation. It builds a symbol table with case-insensitive identifier resolution (IEC 61131-3 identifiers are case-insensitive), resolves type references across scopes, and checks assignments, calls, array indices, and member access for type compatibility.

Key capabilities:

- **Scope and symbol management**: POU scopes (functions, function blocks, programs), struct scopes, and method scopes; nested scope-chain lookup (`LOCAL → POU → GLOBAL → BUILTIN`); duplicate declarations detected.
- **Type checking**: assignments, literals, operator operands, function/FB parameters, struct member access, array indices (bounds and rank), pointer dereferences.
- **Inheritance and interfaces**: `EXTENDS` / `SUPER^` method resolution; `IMPLEMENTS` interface contract verification (abstract method presence, override checks).
- **Enum support**: enumerators are typed with the enum type rather than plain `INT`, and can be used in assignments and comparisons.
- **Struct field resolution**: member access on struct and function block instances (including through interfaces), with nested access like `A.B.C`.
- **Process image globals**: `AT %I…`, `AT %Q…`, `AT %M…` variables are resolved and validated against the configured process image layout.
- **Error reporting**: diagnostics carry error/warning codes, human-readable messages, and precise source locations (`file:line:col`).

Analysis runs in two passes — first every enum, struct, interface and POU header is registered (name and type entry, with a stable type id), then bodies are resolved — so forward reference between types (a STRUCT or FUNCTION_BLOCK that uses another struct, FB, enum or interface declared later, `ARRAY OF` / `POINTER TO` / `REF TO` of later types, later FUNCTION return types) and mutual recursion resolve correctly, and the result is independent of the ST declaration order inside a file or after workspace merge. By-value containment cycles (`A` contains `B` contains `A`, or a function block containing itself) are reported as `CircularDependency`; pointer/`REF_TO`-based cycles stay legal.

### Strict vs. Permissive mode

By default the analyzer runs in **Permissive** mode: diagnostics are collected and surfaced (see `--verbose` / `--strict`), but generation proceeds even when errors exist, gracefully falling back to legacy syntactic inference for unresolvable constructs. This eases incremental adoption on existing codebases.

With `--strict`, the analyzer runs in **Strict** mode: all diagnostics are printed and generation is **blocked** as soon as any error is found — a fail-fast workflow suited to CI pipelines and production builds.

---

## External Libraries

`st2cpp` can call into native C++ from ST through **external libraries** declared in JSON. A `project.json` lists one Library Descriptor per library (`libraries/io.json`, …); descriptors define the C++ namespace, the headers to include, and the ST-visible symbols (types, constants, globals, functions, static methods, FB-like objects).

~~~bash
st2cpp --workspace . --ext-libs project.json --output-dir generated
~~~

The libraries are loaded into a `LibraryRegistry`, injected into semantic analysis, and the generated C++ emits namespace-qualified calls, FB instance methods, and the required `#include`s automatically. Full JSON schemas: [`docs/12-library-descriptor-spec.md`](docs/12-library-descriptor-spec.md) and [`docs/13-project-configuration-spec.md`](docs/13-project-configuration-spec.md). Runnable example: [`examples/external_library`](examples/external_library).

### Exporting descriptors from ST

The pipeline also runs in the **export** direction: `LibraryDescriptorBuilder`
turns analyzed ST directly into a valid v1.0 descriptor (semantic-only, no C++
bindings), which can be serialized to JSON and re-imported. Functions and
function blocks no longer require a `cppBinding` section, so ST-originated
libraries load cleanly.

~~~bash
# Single file → reusable library descriptor
./st2cpp lib.st --export-descriptor lib.json \
    --lib-id timerlib --lib-name TimerLib --lib-version 1.0.0

# Whole workspace, merged as one library, resolved against external libraries
./st2cpp --workspace ./plc --export-descriptor mylib.json \
    --lib-id mylib --lib-name MyLib --lib-version 2.0.0 \
    --ext-libs project.json --lib-dependency timerlib=^1.0.0
~~~

See [`docs/15-library-descriptor-export.md`](docs/15-library-descriptor-export.md).

---

## Documentation

In-repository technical documentation lives in [`docs/`](docs/README.md):

| Document | Contents |
|----------|----------|
| [`01-overview.md`](docs/01-overview.md) | Overview of the pipeline and repository layout |
| [`02-architecture.md`](docs/02-architecture.md) | Module/target map and AST model |
| [`03-build-and-test.md`](docs/03-build-and-test.md) | Build options and how to run the tests |
| [`04-command-line.md`](docs/04-command-line.md) | Complete CLI reference and exit codes |
| [`05-language-support.md`](docs/05-language-support.md) | IEC 61131-3 coverage and limitations |
| [`06-semantic-analysis.md`](docs/06-semantic-analysis.md) | Analyzer passes, symbol table, strictness |
| [`07-code-generation.md`](docs/07-code-generation.md) | Generated C++ shape, modular project mode |
| [`08-process-image.md`](docs/08-process-image.md) | `AT %I/%Q/%M` mapping and pi options |
| [`09-runtime.md`](docs/09-runtime.md) | The `undoCore` header-only runtime |
| [`10-json-module.md`](docs/10-json-module.md) | `st2cpp::json` DOM/parser/serializer |
| [`11-external-libraries.md`](docs/11-external-libraries.md) | External-library subsystem overview |
| [`12-library-descriptor-spec.md`](docs/12-library-descriptor-spec.md) | Library Descriptor JSON v1.0 spec |
| [`13-project-configuration-spec.md`](docs/13-project-configuration-spec.md) | Project Configuration JSON v1.0 spec |
| [`14-examples.md`](docs/14-examples.md) | Example walkthroughs |
| [`15-library-descriptor-export.md`](docs/15-library-descriptor-export.md) | Descriptor import (optional bindings) and export (`LibraryDescriptorBuilder`, CLI `--export-descriptor`, ST → JSON) |

---

## CLI Reference

| Option | Description |
| ---- | -------- |
| `-o <output.cpp>` | Output C++ source file (default: `<input>.cpp`) |
| `-H <output.hpp>` | Output C++ header file (default: `<input>.hpp`) |
| `--namespace <name>` | Set C++ namespace for generated code (default: undoCore) |
| `--runtime <file>` | Custom runtime header file (default: undoCore/undoCore.hpp) |
| `--tokens` | Dump token list and exit |
| `--caseSensitive` | Preserve original case (default: convert to uppercase) |
| `--workspace <path>` | Process all .st files in workspace (recursive) |
| `--ext-libs <file.json>` | Load external libraries listed in the given Project Configuration JSON (`project.json`) |
| `--project-style` | Generate modular project structure (separate files for each FB) |
| `--output-dir <dir>` | Output directory (default: generated) |
| `--pi-auto` | Auto-detect Process Image sizes (default) |
| `--pi-no-auto` | Disable auto-detection, use manual sizes |
| `--pi-input <bytes>` | Process Image Input size in bytes (default: 1024) |
| `--pi-output <bytes>` | Process Image Output size in bytes (default: 1024) |
| `--pi-marker <bytes>` | Process Image Marker size in bytes (default: 1024) |
| `--export-descriptor <file.json>` | Export a semantic-only JSON Library Descriptor from the analyzed ST |
| `--lib-id <id>` | Library id for `--export-descriptor` (required) |
| `--lib-name <name>` | Library name for `--export-descriptor` (required) |
| `--lib-version <ver>` | Library semver version for `--export-descriptor` (required) |
| `--lib-description <text>` | Optional library description |
| `--lib-dependency <id>=<constraint>` | Version policy for an external dependency (repeatable) |
| `-v, --verbose` | Print detailed processing information |
| `--strict` | Strict IEC 61131-3 mode: block generation on semantic errors |
| `-h, --help` | Show this help |

---

## What's Supported

- **POUs**: `FUNCTION`, `FUNCTION_BLOCK`, `PROGRAM`
- **Variable sections**: `VAR`, `VAR_INPUT`, `VAR_OUTPUT`, `VAR_IN_OUT`, `VAR_TEMP`, `VAR_EXTERNAL`, `VAR_GLOBAL`
- **Types**: All IEC 61131-3 elementary types, `STRUCT`, `ENUM`, arrays, `POINTER_TO`, `REF_TO`
- **Control flow**: `IF/ELSIF/ELSE`, `FOR/TO/BY`, `WHILE`, `REPEAT/UNTIL`, `CASE/OF`
- **Calls**: Positional, named, output binding (=>), IN_OUT references
- **FB Namespaces**
- **Fbinheritance**: `EXTENDS` with `SUPER^` method access
- **Calls**: Positional, named (:=`�, output binding (=>`), IN_OUT references
- **FB Methods**: with `UAR_INPUT`, `VAR_OUTPUT`VAR_TEMP locals
- **Project mode**: Topological dependency sort, circular-dependency-safe header layout
- **Supported Language**: Full coverage of IEC 61131-3 details at [docs](https://www.undort.com/st2cpp/#supported)
- **Circular-dependency**: Detected and reported with clear error messages

---

## Testing

The project includes a comprehensive test suite using Google Test (**663 tests passing, 2 disabled, 24 suites**), covering:

- **Lexing and parsing** of all IEC 61131-3 constructs
- **Semantic analysis**: symbol resolution, type checking, scope chains, enums, structs, arrays, interfaces, inheritance, method overriding, forward type references and by-value dependency cycles, process image globals, and **external-library symbol import**
- **Code generation** for single-file and modular project output
- **JSON, Library Descriptor and Project Configuration modules**
- **Library descriptor export**: ST → descriptor → canonical JSON → re-import round trip, dependency policies, and representability errors
- **CLI export integration**: `--export-descriptor` end-to-end (single file, workspace, validation and error codes)
- **End-to-end compilation** of generated C++ with a native compiler

To build and run tests:

~~~bash
cmake -B build -DBUILD_TESTS=ON .
cmake --build build --j|| $(nproc)
cd build
ctest --output-on-failure --verbose
~~~

All tests are automatically executed in the CI/CD pipeline on every pull request and commit to main. Test results are available in the [Actions] tab(<https://github.com/undoRT/st2cpp/actions>) of the GitHub repository.

## Known Limitations (Beta)

- `ARRAY[*]` (variable-length arrays) not supported
- `VAR_CONFIG` and SFC (`ACTION`, `TRANSITION`) not yet implemented

---

## Contributing

1. Fork the repo and create a feature branch
2. Follow the coding standards in [CONTRIBUTING.md](CONTRIBUTING.md)
3. Open a Pull Request

Core standards: C++17, RAII, Doxygen on all public APIs.

---

## Testing Checklist

Before submitting a PR, ensure that:

- [ ] All tests pass locally (`ctest`)
- [ ] No regression test failures
- [ ] Code is documented with Doxygen comments

## License

GPL-3.0 -- see [LICENSE](LICENSE).

## Acknowledgements

Architecture inspired by [STruCpp](https://github.com/Autonomy-Logic/STruCpp) (TypeScript). No code was copied or ported.
