# Runtime (`undoCore`)

`undoCore` is a **header-only C++ runtime** shipped with the repository at
`st2cpp_includes/undoCore/include/undoCore/`. st2cpp maps IEC 61131-3
elementary types onto these C++ aliases so generated code is plain, portable
C++17.

## Header layout

```
st2cpp_includes/undoCore/include/undoCore/
  undoCore.hpp   # umbrella header (include this in generated code)
  types.hpp      # elementary-type aliases, VAR_IN_OUT wrapper, named types
  memory.cpp     # (conditionally compiled) buffer/IEC-call primitives
  …
```

The CLI's default runtime header is `undoCore/undoCore.hpp`; override per
invocation with `--runtime`.

## Elementary type mapping (`types.hpp`)

```cpp
using Bool    = bool;              // BOOL
using Int8    = int8_t;            // SINT
using Int16   = int16_t;           // INT
using Int32   = int32_t;           // DINT
using Int64   = int64_t;           // LINT
using UInt8   = uint8_t;           // USINT
using UInt16  = uint16_t;          // UINT
using UInt32  = uint32_t;          // UDINT
using UInt64  = uint64_t;          // ULINT
using Float   = float;             // REAL
using Double  = double;            // LREAL
using Byte    = uint8_t;           // BYTE
using Word    = uint16_t;          // WORD
using Dword   = uint32_t;          // DWORD
using Lword   = uint64_t;          // LWORD
using String  = std::string;       // STRING
using Wstring = std::wstring;      // WSTRING
using Time    = UInt32;            // TIME (milliseconds)
using Date    = UInt32;            // DATE (seconds since 1970-01-01)
using Tod     = UInt64;            // TIME_OF_DAY (milliseconds since midnight)
using Dt      = UInt64;            // DATE_AND_TIME (seconds since 1970-01-01)
using Void    = void;              // FUNCTION returning nothing
```

Plus `struct TcType`-style named types for user-derived wrappers and a
`VAR_IN_OUT` template wrapper for inout/ref parameters:

```cpp
template <typename T> struct VAR_IN_OUT { T* p; /* … deref/assign … */ };
```

## Constants / literals

`TIME_LITERAL(const char*)` and friends parse IEC time/date literals into the
integer representations above (`u`/`UInt32` seconds-or-millis as documented),
so `T#5s`-style literals lower to plain runtime calls or `constexpr` values.

## Integration

- **As a submodule**: CMake `add_subdirectory(undoCore)` (header-only).
- **Externally**: set `ST2CPP_USE_EXTERNAL_UNDOCORE=ON` — requires
  `find_package(undoCore)`; install/bundle via `ST2CPP_BUNDLE_UNDOCORE`.
- **Header-only fallback**: when no undoCore CMake project is present, st2cpp
  toolchains into header-only mode (warns), consuming
  `st2cpp_includes/undoCore/include` directly.

The generated C++ includes `undoCore/undoCore.hpp` first, then any external
library headers the descriptors declared (`cppHeader`). The end-to-end examples
validate the result by compiling the generated translation units with
`g++ -std=c++17 -fsyntax-only` against the mock library headers and the
runtime.