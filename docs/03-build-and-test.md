# Building and Testing

## Requirements

- Compiler with C++17: GCC ≥ 11, Clang ≥ 15 (or equivalent).
- CMake ≥ 3.16.
- GoogleTest, only when building the test suite (`BUILD_TESTS=ON`).

There are **no other dependencies**: the JSON parser used by the
external-library subsystem is vendored inside the project (`st2cpp::json`), and
the generated-code runtime (`undoCore`) is header-only and shipped under
`st2cpp_includes/`.

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `BUILD_TESTS` | `OFF` | Build the GoogleTest suite (`st2cpp_tests`) |
| `ST2CPP_USE_EXTERNAL_UNDOCORE` | `OFF` | Use a system-installed `undoCore` via `find_package` instead of the submodule |
| `ST2CPP_BUNDLE_UNDOCORE` | `ON` | Bundle the undoCore headers at install time |

When `ON`, `ST2CPP_USE_EXTERNAL_UNDOCORE` requires `find_package(undoCore)` to
succeed. Otherwise the build uses the headers under
`st2cpp_includes/undoCore/include`; if the undoCore CMake project is present it
is added as a subproject, otherwise the toolchain falls back to header-only
mode with a warning.

## Configure and build

Out-of-source build (recommended):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build build -j "$(nproc)"
```

With tests (development):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build -j "$(nproc)"
```

The compiler binary is `build/st2cpp`; the test binary is `build/st2cpp_tests`.

### In-source builds

The repository historically supports `cmake .` at the source root (generates a
`Makefile` + build artifacts in-tree, all git-ignored). Two caveats:

- The `st_samples` custom target that copies `tests/st_samples/*.st` into the
  build tree is **disabled when the build tree equals the source tree**
  (`CMAKE_CURRENT_BINARY_DIR == CMAKE_CURRENT_SOURCE_DIR`): in-source the files
  are already where the tests expect them, and copying them over themselves
  would fail.
- **Never run `make clean` in an in-source tree**: it deletes build *outputs*,
  which in-source coincide with source files (this already destroyed
  `tests/st_samples/*.st` once; guard above prevents the *copy* target from
  deleting them again, but other outputs can still be affected).

Prefer out-of-source builds.

## Targets

| Target | Description |
|--------|-------------|
| `st2cpp_lib` | core compiler library (lexer/parser/codegen) |
| `st2cpp_json` | JSON module |
| `st2cpp_library` | Library Descriptor module |
| `st2cpp_project` | Project Configuration module |
| `st2cpp_semantic` | Semantic analysis module |
| `st2cpp` | CLI executable |
| `st2cpp_tests` | GoogleTest executable (with `BUILD_TESTS=ON`) |

## Running the tests

```bash
cd build
ctest --output-on-failure --verbose
```

or directly:

```bash
./build/st2cpp_tests
```

Baseline (tests tagged per module): **599 tests passing, 2 disabled, 21 test
suites**. The suite covers lexing, parsing, scope/symbol management, semantic
analysis (visitors, type system, diagnostics, external-library import), code
generation (single-file, modular, golden files), the JSON/library/project
modules, and end-to-end native compilation of generated code (the compilation
tests invoke a real C++ compiler and require it to be available on `PATH`).

Many semantics/codegen tests need the sample ST files under
`tests/st_samples/`; the CMake `st_samples` target copies them into the build
tree (out-of-source builds). Tests that compile generated code additionally
resolve the `core/core.hpp`, `io/io.hpp`, … mock headers declared in
`tests/test_compilation.cpp`.

## Adding a test

1. Put the source in `tests/` (module-specific: `tests/semantic/`,
   `tests/library/`, `tests/project/`, `tests/codegen/`).
2. Register the `*.cpp` in the `st2cpp_tests` target list inside
   `CMakeLists.txt` (`BUILD_TESTS` block).
3. For fixture data, add files under `tests/<module>/data/` and reference them
   through the compile definitions `ST_SAMPLES_DIR`,
   `LIBRARY_SAMPLES_DIR`, `PROJECT_SAMPLES_DIR` (absolute paths at configure
   time), or relative to the working directory when running from the repo root.