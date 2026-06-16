# st2cpp — Structured Text to C++ Transpiler

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Build](https://github.com/undoRT/st2cpp/actions/workflows/build.yml/badge.svg)](https://github.com/undoRT/st2cpp/actions/workflows/build.yml)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](docs/html/index.html)

Part of the [**undoRT**](www.undort.com) open-source automation platform.

**st2cpp** translates IEC 61131-3 Structured Text into clean, native C++17. No VM, no runtime overhead — just standard C++ you can compile, debug, and integrate anywhere.

**[Full documentation undort.com/st2cpp](www.undort.com/st2cpp)**

---

## Download

Grab the latest binary from [**GitHub Releases**](https://github.com/undoRT/st2cpp/releases):

| Platform | File |
|---|---|
| Linux x64 | `st2cpp-linux-x64.tar.gz` |
| Linux ARM64 | `st2cpp-linux-arm64.tar.gz` |
| macOS ARM64 | `st2cpp-macos-arm64.tar.gz` |
| Windows x64 | `st2cpp-win-x64.zip` |

```bash
# Linux/macOS
tar -xzf st2cpp-*.tar.gz && ./st2cpp --help

# Windows
unzip st2cpp-win-x64.zip && st2cpp.exe --help
```

---

## Quick Start

### Build from source

```bash
git clone https://github.com/undoRT/st2cpp.git
cd st2cpp
./build.sh          # or: mkdir build && cd build && cmake .. && make -j$(nproc)
```

### Transpile a file

```bash
# Single file
./st2cpp program.st --namespace myplc

# Entire workspace (modular project output)
./st2cpp --workspace ./src --project-style --output-dir build --namespace myplc
```

### Example

**Input** (`counter.st`):

```iecst
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
```

**Generated** (`counter.hpp` / `counter.cpp`) — a plain C++ struct with `operator()()`, getters, and setters. See [full output example](www.undort.com/st2cpp/#example)

---

## CLI Reference

| Option | Description |
|---|---|
| `-o <file>` | Output `.cpp` file (default: `<input>.cpp`) |
| `-H <file>` | Output `.hpp` file (default: `<input>.hpp`) |
| `--namespace <name>` | C++ namespace for generated code (default: `st2cpp`) |
| `--runtime <file>` | Custom runtime header (default: `st2cpp_types.hpp`) |
| `--workspace <path>` | Process all `.st` files recursively |
| `--project-style` | Modular output with dependency management (requires `--workspace`) |
| `--output-dir <dir>` | Output directory for workspace/project mode |
| `--tokens` | Dump token stream and exit (debugging) |
| `-h, --help` | Show help |

---

## What's Supported

- **POUs**: `FUNCTION`, `FUNCTION_BLOCK`, `PROGRAM`
- **Variable sections**: `VAR`, `VAR_INPUT`, `VAR_OUTPUT`, `VAR_IN_OUT`, `VAR_TEMP`, `VAR_EXTERNAL`, `VAR_GLOBAL`
- **Types**: All IEC 61131-3 elementary types, `STRUCT`, `ENUM`, arrays, `POINTER TO`, `REF_TO`
- **Control flow**: `IF/ELSIF/ELSE`, `FOR/TO/BY`, `WHILE`, `REPEAT/UNTIL`, `CASE/OF`
- **Calls**: Positional, named (`:=`), output binding (`=>`), IN_OUT references
- **FB inheritance**: `EXTENDS` with `SUPER^` method access
- **Project mode**: Topological dependency sort, circular-dependency-safe header layout

Full language coverage details: [st2cpp language](www.undort.com/st2cpp/#supported)

---

## Known Limitations (Beta)

- No semantic analysis / type checking yet
- First parse error stops compilation (no error recovery)
- `ARRAY[*]` (variable-length arrays) not supported
- `VAR_CONFIG` and SFC (`ACTION`, `TRANSITION`) not yet implemented

---

## Contributing

1. Fork the repo and create a feature branch
2. Follow the coding standards in [CONTRIBUTING.md](CONTRIBUTING.md)
3. Open a Pull Request

Core standards: C++17, RAII, Doxygen on all public APIs.

---

## License

GPL-3.0 — see [LICENSE](LICENSE).  
The runtime library `st2cpp_types.hpp` carries a linking exception — see [COPYING.ST2CPP_TYPES](COPYING.ST2CPP_TYPES).

---

## Acknowledgments

Architecture inspired by [STruCpp](https://github.com/Autonomy-Logic/STruCpp) (TypeScript). No code was copied or ported.
