# st2cpp - Structured Text to C++ Compiler

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Documentation](https://img.shields.io/badge/docs-doxygen-blue.svg)](docs/html/index.html)

It is a part of the [**undoRT**](https://github.com/undoRT) project.

**st2cpp** is a transpiler that translates IEC 61131-3 Structured Text (ST) into readable and efficient C++17 code. It is designed for industrial automation, PLC programming, and embedded systems where C++ performance and portability are required.

## Download

Download the latest release from [GitHub Releases](https://github.com/Salvatore1999/st2cpp/releases).

- **Linux (x64)**: `st2cpp-linux-x64.tar.gz`
- **Linux (ARM64)**: `st2cpp-linux-arm64.tar.gz`
- **macOS (ARM64)**: `st2cpp-macos-arm64.tar.gz`
- **Windows (x64)**: `st2cpp-win-x64.zip`

Extract and run:

~~~bash
# Linux/macOS
tar -xzf st2cpp-*.tar.gz
./st2cpp --help

# Windows
unzip st2cpp-win-x64.zip
st2cpp.exe --help
~~~

## Features

### Language Support

- **POUs** (Program Organization Units): `FUNCTION`, `FUNCTION_BLOCK`, `PROGRAM`
- **Variable sections**: `VAR`, `VAR_INPUT`, `VAR_OUTPUT`, `VAR_IN_OUT`, `VAR_TEMP`, `VAR_EXTERNAL`, `VAR_GLOBAL`
- **Data types**: All IEC 61131-3 elementary types (`BOOL`, `INT`, `REAL`, `STRING`, etc.)
- **User-defined types**: `STRUCT` and `ENUM` with `TYPE ... END_TYPE`
- **Arrays**: Single and multi-dimensional with automatic size calculation
- **Pointers**: `POINTER TO` and `REF_TO`
- **Control flow**: `IF/THEN/ELSIF/ELSE`, `FOR/TO/BY/DO`, `WHILE/DO`, `REPEAT/UNTIL`, `CASE/OF`
- **Function calls**: Positional arguments (`foo(1,2,3)`) and named arguments (`foo(a:=1, b:=2)`)
- **Output bindings**: Using `=>` syntax (`foo(total => myVar)`)
- **Initialization**: Variable initializers (`value : INT := 10`) and array initializers (`data : ARRAY[0..2] OF INT := [1,2,3]`)

### Code Generation

- **Readable output**: Generates clean, well-formatted C++17 code
- **Function Blocks**: Compiled to C++ structs with `set_*()` methods for inputs, `get_*()` for outputs, and `operator()()` for execution
- **Functions**:
  - `VAR_INPUT` parameters become C++ function parameters with default values
  - `VAR_OUTPUT` parameters become optional pointers (`T* __name`) for flexible calling
  - `VAR_IN_OUT` parameters become required references (`T& name`)
- **VAR_IN_OUT**: Wrapped in `VAR_INOUT<T>` template for reference semantics
- **Type mapping**: IEC types mapped to native C++ types (`Bool` -> `bool`, `INT` -> `int16_t`, etc.)
- **st2cpp_types.hpp**: Header-only runtime library with zero dependencies, customizable at compile time

### Project-Style Generation

For large-scale PLC projects with multiple Function Blocks, st2cpp offers a modular project generation mode that produces a well-organized, compilation-ready C++ project.

#### Enabling Project Mode

~~~bash
st2cpp --workspace ./my_plc_sources --project-style --output-dir build --namespace mycompany
~~~

#### Generated Structure

~~~bash
build/
├── SimpleGVLs.hpp       # Foundation types (ENUMs, simple STRUCTs, simple globals)
├── FunctionBlocks.hpp   # Master aggregator
├── FunctionBlocks/      # One file per Function Block
│   ├── Timer.hpp
│   ├── Timer.cpp
│   ├── Counter.hpp
│   └── Counter.cpp
├── GVLs.hpp             # Complex types (STRUCTs containing FBs or complex globals)
├── GVLs.cpp             # Global variable storage
├── Functions.hpp        # Global function declarations
├── Functions.cpp        # Global function implementations
├── Programs.hpp         # Master aggregator for programs
└── Programs/            # One file per PROGRAM
    ├── Main.hpp
    └── Main.cpp
~~~

#### Inclusion Hierarchy

The architecture is carefully designed to avoid circular dependencies:

~~~bash
SimpleGVLs.hpp
├── ENUM types
├── STRUCT types that do NOT contain Function Blocks
└── Global variables of simple types (non-FB)

FunctionBlocks.hpp
├── #include "SimpleGVLs.hpp"
└── #include "FunctionBlocks/*.hpp"

GVLs.hpp
├── #include "FunctionBlocks.hpp"  (already includes SimpleGVLs.hpp)
├── STRUCT types that contain Function Blocks
└── extern declarations for complex global variables (FB types)

GVLs.cpp
├── #include "GVLs.hpp"
└── Definitions of complex global variables

Functions.hpp
├── #include "GVLs.hpp"
└── Function declarations

Functions.cpp
├── #include "Functions.hpp"
└── Function implementations

FB.hpp (e.g., Timer.hpp)
├── #include "SimpleGVLs.hpp"
├── #include "FunctionBlocks/dependencies.hpp"  (other FBs used as members)
└── Function Block struct definition

FB.cpp (e.g., Timer.cpp)
├── #include "FB.hpp"
├── #include "Functions.hpp"  (only if needed)
└── Function Block implementation
~~~

#### Important Limitations

A STRUCT defined in GVLs.hpp that contains a Function Block CANNOT be instantiated inside another Function Block's header (FB.hpp).

**Why?** This would create a circular dependency:

- FB.hpp would need to include GVLs.hpp (to see the STRUCT)
- GVLs.hpp already includes FunctionBlocks.hpp (which includes FB.hpp)
- Result: FB.hpp -> GVLs.hpp -> FunctionBlocks.hpp -> FB.hpp (circular)

#### Dependency Management

The generator automatically:

- Detects dependencies between Function Blocks using topological sorting
- Places dependent FB definitions in the correct order
- Ensures each FB header only includes what it strictly needs

#### Benefits

- No circular dependencies - Headers remain lightweight and independent
- Incremental compilation - Changes to one FB only recompile its own file
- IDE friendly - Each FB appears as a separate file in the project tree
- Scalable - Works with projects of any size
- Linker ready - Global variables use extern/definition separation

#### How It Works

- Dependency Analysis: The compiler scans all .st files and builds a dependency graph between Function Blocks
- Topological Sorting: FBs are ordered so that dependencies are defined before they are used
- **Strategic Includes**:
  - SimpleGVLs.hpp contains only independent types (ENUMs, simple STRUCTs, simple globals) and ensures the correct inclusion of structures
  - FunctionBlocks/*.hpp include SimpleGVLs.hpp but never include GVLs.hpp
  - GVLs.hpp includes FunctionBlocks.hpp and defines complex types
  - This eliminates circular dependencies

#### Benefits Over Flat Generation

| Aspect | Flat Mode | Project Style |
| -------- | -------- | -------- |
| File | count 2 per .st file | 2 per POU + aggregators |
| Compilation | Whole project rebuild | Incremental rebuild |
| Dependencies | Manual management | Automatic detection |
| IDE | support Single file per ST source | One file per logical unit |
| Header bloat | All types in one header | Minimal, targeted includes |

#### Example: Building the Generated Project

~~~bash
# Generate
st2cpp --workspace ./source --project-style --output-dir proj

# Build with CMake (recommended)
cd proj
cat > CMakeLists.txt << EOF
cmake_minimum_required(VERSION 3.16)
project(plc_program)

set(CMAKE_CXX_STANDARD 17)
add_executable(plc_main
    FunctionBlocks/Timer.cpp
    FunctionBlocks/Counter.cpp
    Programs/Main.cpp
)
target_include_directories(plc_main PRIVATE /path/to/st2cpp_includes)
EOF

cmake -B build && cmake --build build
~~~

#### When to Use Project Style

- Large projects with 10+ Function Blocks
- Multiple programmers working on different FBs
- Version control where per-file history matters
- CI/CD pipelines that benefit from incremental compilation
- Frequent changes to individual FBs

#### When to Use Flat Mode

- Small projects (1-3 FBs)
- Rapid prototyping
- Single-file distribution

### Developer Experience

- **Single binary**: No external dependencies, just `st2cpp` executable
- **CMake integration**: Easy to include in existing projects
- **Documentation**: Doxygen-ready with comprehensive API docs
- **Testing**: Example programs included in `tests/` directory

## Quick Start

### Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.16+ (for building)

### Building from Source

~~~bash
# Clone the repository
git clone https://github.com/yourusername/st2cpp.git
cd st2cpp

# Build using the provided script
./build.sh

# Or build manually
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
~~~

The executable st2cpp will be in the build/ directory.

## usage

~~~bash
# Basic usage (default namespace: st2cpp, default runtime: st2cpp_types.hpp)
./st2cpp input.st

# Specify custom namespace and runtime
./st2cpp program.st -o output.cpp -H output.hpp --namespace mycompany --runtime myruntime.hpp

# Dump tokens (debugging)
./st2cpp program.st --tokens

# Project style generation
st2cpp --workspace ./my_plc_project --project-style --output-dir build --namespace myplc

# Flat workspace mode (one .hpp/.cpp per .st file)
st2cpp --workspace ./my_plc_project --output-dir generated

# Get help
./st2cpp --help
~~~

## Example

### Input ST (example.st)

~~~st
FUNCTION AddWithOutput : INT
    VAR_INPUT
        a : INT := 5;
        b : INT := 10;
    END_VAR
    VAR_OUTPUT
        result : INT;
    END_VAR
    VAR
        temp : INT;
    END_VAR
    temp := a + b;
    result := temp;
END_FUNCTION

PROGRAM Main
    VAR
        myResult : INT;
    END_VAR
    AddWithOutput(result => myResult);
END_PROGRAM
~~~

### Compile

~~~bash
./st2cpp example.st -o example.cpp --namespace myplc
~~~

### Generated C++ (counter.cpp)

~~~cpp
#include "example.hpp"
#include "st2cpp_types.hpp"

namespace myplc {

// FUNCTION AddWithOutput
Int16 AddWithOutput(Int16 a = 5, Int16 b = 10, Int16* __result) {
    Int16 AddWithOutput_ret{};
    Int16 temp{};
    
    // Initialize output from pointer if provided
    Int16 result = __result ? *__result : Int16{};
    
    temp = (a + b);
    result = temp;
    
    // Write back output if pointer is valid
    if (__result) { *__result = result; }
    return AddWithOutput_ret;
}

// PROGRAM Main
void Main::run() {
    AddWithOutput(5, 10, &(myResult));
}

} // namespace myplc
~~~

## Command Line Options

| Option | Description |
| ----------- | ----------------- |
| `-o <file>` | Output C++ source file (default: `<input>.cpp`) |
| `-H <file>` | Output C++ header file (default: `<input>.hpp`) |
| `--namespace <name>` | Set C++ namespace for generated code (default: `st2cpp`) |
| `--runtime <file>` | Custom runtime header file (default: `st2cpp_types.hpp`) |
| `--workspace <path>` | Process all .st files in workspace (recursive) |
| `--project-style` | Generate modular project structure (requires --workspace) |
| `--output-dir <dir>` | Output directory for workspace/project mode (default: generated) |
| `--tokens` | Dump token list and exit (debugging) |
| `-h, --help` | Show help message |

## Project Structure

~~~bash
st2cpp/
├── include/           # Public headers
│   ├── ast/           # Abstract Syntax Tree nodes
│   ├── codegen/       # C++ code generator
│   ├── lexer/         # Token definitions and lexer
│   ├── version.hpp    # Version file (semver)
│   └── parser/        # Recursive descent parser
├── src/               # Implementation files
│   ├── cli/           # Command-line interface
│   ├── codegen/       # Code generator implementation
│   ├── lexer/         # Lexer implementation
│   └── parser/        # Parser implementation
├── st2cpp_includes/   # Runtime libraries
│   └── st2cpp_types/  # Header-only types runtime
├── tests/             # Example ST programs
├── scripts/           # Build and utility scripts
├── CMakeLists.txt     # CMake build configuration
├── build.sh           # Quick build script
├── LICENSE            # License file
├── COPYING            # Copying file
└── README.md          # This file
~~~

## Architecture

The compiler follows a traditional frontend/middleend/backend architecture:

~~~text
ST Source -> Lexer -> Parser -> AST -> Code Generator -> C++17 Code
~~~

- Lexer: Converts source code into a stream of tokens
- Parser: Builds an Abstract Syntax Tree (AST) using recursive descent
- Code Generator: Traverses the AST and emits C++17 code

### Function Call Translation

| ST Syntax | C++ Translation |
| ----------- | ----------------- |
| foo(10, false) | Positional arguments mapped to parameters |
| foo(a:=1, b:=2) | Named arguments reordered according to declaration |
| foo(out => myVar) | Output binding passes address (&myVar) |
| foo(inout := myVar) | IN_OUT binding passes reference (myVar) |
| foo(result => myResult) (missing inputs) | Uses default values from function declaration |

## Documentation

Generate full API documentation with Doxygen:

~~~bash
./scripts/generate_docs.sh
# Or
mkdir build && cd build
cmake .. && make docs
~~~

Then open docs/html/index.html in your browser.

## Testing

Test files are located in the tests/ directory:

~~~bash
# Run all tests
cd tests
../build/st2cpp counter.st
g++ -std=c++17 -I../st2cpp_includes/ counter.cpp -o counter
./counter
~~~

## Contributing

Contributions are welcome. Please follow these steps:

- Fork the repository
- Create a feature branch (git checkout -b feature amazing-feature)
- Commit your changes (git commit -m 'Add amazing feature')
- Push to the branch (git push origin feature/amazing-feature)
- Open a Pull Request

## Coding Standards

Few coding standards:

- Use C++17 features
- Follow RAII principles
- Document all public APIs with Doxygen comments
- Write tests for new functionality

## Known Limitations (Beta)

- Limited error recovery in parser (first error stops compilation)
- No support for `ARRAY[*]` (variable-length arrays)
- No complete semantic analysis (type checking in progress)
- No support for `CONSTANT` arrays with non-literal bounds
- VAR_CONFIG not yet implemented
- Asynchronous function blocks not supported

## Tested With

- Simple FBs with VAR_INPUT/VAR_OUTPUT
- Nested structs containing FBs
- Functions with IN_OUT parameters
- ENUM types with explicit values
- Array with different dimensions and array of function blocks
- Function blocks inheritated, with method access via SUPER^

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

st2cpp - Structured Text to C++ Compiler
Copyright (c) 2026 undoRT

### st2cpp_types Library Exception

The C++ st2cpp_types library (`st2cpp_types.hpp`) is subject to the same GPL-3.0 license.

See [COPYING](COPYING.ST2CPP_TYPES) for the full exception text.

## Acknowledgments

This project is a **complete, independent implementation** written in C++17.

The architecture and design were inspired by the excellent [**STruCpp** project](https://github.com/Autonomy-Logic/STruCpp), which is written in TypeScript.
No code from STruCpp was copied or ported.

## Roadmap

### v1.0 (Current)

Details:

- Core language features
- Function blocks with getters/setters
- Arrays and user-defined types
- Complete control flow

### Planned - Future

Some better to have things:

- Semantic analysis (type checking)
- Better error messages with location info
- Optimizations (constant folding, dead code elimination)
- Support for ACTION and TRANSITION (SFC)
