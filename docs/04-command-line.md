# Command Line Interface

```
st2cpp <input.st> [options]
```

## Options

| Option | Description |
|--------|-------------|
| `-o <output.cpp>` | Output C++ source file (default: `<input>.cpp`) |
| `-H <output.hpp>` | Output C++ header file (default: `<input>.hpp`) |
| `--namespace <name>` | C++ namespace for generated code (default: `undoCore`) |
| `--runtime <file>` | Custom runtime header (default: `undoCore/undoCore.hpp`) |
| `--tokens` | Dump the token list and exit |
| `--strict` | Strict IEC 61131-3 mode: block generation on semantic errors |
| `--caseSensitive` | Preserve original case (default: convert to uppercase) |
| `--workspace <path>` | Process all `.st` files in a workspace (recursive) |
| `--ext-libs <file.json>` | Project JSON listing the external libraries to load |
| `--project-style` | Generate a modular project structure (separate files per FB) |
| `--output-dir <dir>` | Output directory (default: `generated`) |
| `--pi-auto` | Auto-detect Process Image sizes (default) |
| `--pi-no-auto` | Disable auto-detection, use manual sizes |
| `--pi-input <bytes>` | Process Image Input size in bytes (default: `1024`) |
| `--pi-output <bytes>` | Process Image Output size in bytes (default: `1024`) |
| `--pi-marker <bytes>` | Process Image Marker size in bytes (default: `1024`) |
| `-v, --verbose` | Print detailed processing information |
| `-h, --help` | Show help and exit |
| `--version` | Show version and exit |

A positional argument (`<input.st>`) is the file to translate in single-file
mode. Any value starting with `-` that is not a known option is rejected, and
options that require a value report an error when the value is missing.

## Modes

### 1. Single file

```bash
st2cpp counter.st                 # counter.hpp / counter.cpp in CWD
st2cpp counter.st -o out.cpp -H out.hpp --namespace myplc
```

Processes one translation unit and writes one header/source pair. `--tokens`
prints the lexer output and stops. Output paths default to
`<stem>.cpp`/`<stem>.hpp` next to the input.

### 2. Flat workspace

```bash
st2cpp --workspace ./plc --output-dir generated
```

Finds every `.st` file under the workspace recursively and independently
translates each one into `<stem>.hpp`/`<stem>.cpp` inside `--output-dir`. Each
file is analyzed and generated in isolation (per-file semantic info).

### 3. Project style (modular)

```bash
st2cpp --workspace ./plc --project-style --output-dir build
```

Parses all files, **merges** the translation units, drops duplicate
declarations, topologically orders FBs, and writes:

```
build/GVLs.hpp
build/Functions.hpp            build/Functions.cpp
build/FunctionBlocks.hpp       (master include)
build/FunctionBlocks/*.hpp     build/FunctionBlocks/*.cpp
build/Programs.hpp             build/Programs.cpp
```

`--project-style` without `--workspace` prints a warning and falls back to flat
generation.

### 4. External libraries

```bash
st2cpp --workspace . --ext-libs project.json --output-dir generated
```

`--ext-libs` loads the Project Configuration JSON, resolves every declared
Library Descriptor through the registry and feeds it to semantic analysis, so
symbols used in the ST can come from external libraries. It composes with any of
the modes above. Errors in the project JSON or descriptor loading are printed
with the full diagnostic list and exit code `1`. See
`11-external-libraries.md`.

## Semantic strictness and exit codes

- Default: **Permissive** — diagnostics are collected and (with `-v`) printed,
  but generation never blocks on errors; the codegen degrades to legacy
  syntactic inference for unresolvable constructs.
- `--strict` — **Strict** — all diagnostics are printed and generation is
  **blocked** as soon as any semantic error is found.

Exit codes:

| Code | Meaning |
|------|---------|
| `0` | success (also `--help`/`--version`) |
| `1` | usage error, parse error, generation error, strict-mode block, invalid `--ext-libs` JSON |

Workspace and project-style runs return `1` if any file failed, `0` otherwise.

## Process Image sizing

`--pi-*` options control the generated process-image buffers (Input / Output /
Marker). By default `--pi-auto` runs `ProcessImageAnalyzer::analyze` on the
translation unit and uses the recommended sizes derived from the highest
`AT %I/%Q/%M` addresses; manual values win with `--pi-no-auto` or when a
recommended config cannot be derived. See `08-process-image.md`.

## Examples

```bash
# Single file, custom namespace and runtime header
st2cpp motor.st --namespace plc --runtime plc/runtime.hpp

# Flat workspace
st2cpp --workspace ./src --output-dir gen

# Modular project with tests
st2cpp --workspace ./plc --project-style --output-dir build

# External libraries (project JSON drives everything)
st2cpp --workspace . --ext-libs project.json --output-dir generated

# Strict, CI-friendly
st2cpp --workspace ./plc --strict --output-dir build
```