# Changelog

## [0.4.5] - 2026-09-29

### Changed
- The code generator is no longer one 7200-line translation unit. It has been
  split into focused components, each a pure move verified against a golden
  corpus of generated output: the C++ emitted for every sample is byte-identical
  before and after, in every mode. The three emitters sit on top of a shared
  `EmissionContext` and call only into one another, never into each other's guts:
  - `EmissionContext` owns the state a single run shares: the two output
    streams, the indent, the scope stack, the maps collected while walking the
    translation unit, and the collaborators built from the semantic analysis.
    It emits no line; it only holds what the emitters read and write.
  - `ProjectEmitter` assembles the files: the flat translation unit and the
    modular project, one file per function block, function, program and global
    list, plus the master headers.
  - `DeclEmitter` emits declarations: function blocks, functions, programs,
    structs, interfaces, enums, globals and methods, and collects the
    signatures the bodies need.
  - `BodyEmitter` emits the statements and expressions that fill a body, and
    the address read/write and ordered struct-initializer helpers they share.
  - `SemanticBridge` owns every query the generator makes over the semantic
    analysis (symbol table, C++ spelling of a type or an enumerator, the
    callable a call targets, the interface of an external function block, the
    diagnostic for an incomplete descriptor binding). It only reads: it never
    touches the output streams, the indent or the scope stack.
  - `LibraryIncludeTracker` decides which external library headers a
    translation unit actually needs, and emits them deduplicated in library
    load order.
  - `DependencyOrdering` returns the order function blocks and structs must be
    emitted in, preferring the order the semantic analysis computed and falling
    back to a topological sort of a name-level dependency graph.
  - `TypeMapper` turns a Structured Text type into its C++ spelling: the
    elementary and the named types, how an array or a pointer decorates them,
    alias resolution, the layout a value takes and the assignment cast a numeric
    store needs. It deliberately keeps the mixed-case spelling of the runtime
    aliases (`Int16`, `Float`, ...) and lets only user-defined names follow the
    identifier policy.
  - `ProcessImage` holds the AT address allocator (fixed addresses marked
    occupied, placeholders handed the first free aligned slot, regions grown on
    demand), the analyzer that sizes each memory area from the addresses a
    translation unit actually uses, and the IEC address parser both share.
  - `ScopeManager` owns the symbol scope stack: the C++ type and AT address of
    each variable, the output-parameter temporary counters, and the base class a
    `SUPER^` resolves through. Lookup walks from the innermost scope outward.

  `CodeGenerator` is now only the facade: it owns the context and the three
  emitters and forwards its public entry points to them, so no call site outside
  the generator moved. `CodegenTypes.h` holds the call-interface value types and
  the generated-file descriptors, and `IdentifierPolicy.h` holds the identifier
  case policy, so every component can spell an identifier the same way without
  sharing state.

### Added
- `scripts/golden.sh`, the gate that guards the generator split. It compiles
  every sample under `tests/st_samples` and `examples` in all four modes
  (default, `--strict`, `--caseSensitive`, modular) and prints one SHA-256 per
  generated file, header and source alike, plus a hash of the diagnostics.
  A refactor that changes one byte of output, or moves one diagnostic, breaks
  the diff. The header used to go unverified: the CLI writes it to the current
  directory unless `-H` is given, so the harness now pins it explicitly.
- `examples/complex_example/stComplex.st` doubles as the case-policy sample: one
  function refers to its `value` parameter as `vAlue`, so `--caseSensitive`
  reports a `CaseMismatch` warning on a file that compiles cleanly by default.
- `--caseSensitive` now governs identifier resolution in the semantic phase and
  no longer only the spelling of the generated code. In that mode identifiers
  are keyed verbatim, so `name` and `Name` are two distinct symbols and a
  mis-cased reference is rejected the way a C++ compiler rejects it. The
  policy covers variables, function and function block names, parameters,
  struct and interface members, enumerators, user-defined types, the elementary
  types and the IEC base conversion functions.
- The elementary types needed a dedicated check: the parser folds every spelling
  of a type keyword to the same `BaseType` enumerator, so `dint` and `DINT` were
  indistinguishable and `--caseSensitive` was blind to them. `TypeRef::name`
  keeps the spelling as written, which is now verified against the IEC name.
- The three modes are kept apart: the default still follows IEC 61131-3 and folds
  identifiers to uppercase; `--caseSensitive` alone resolves a mis-cased
  reference and reports a `CaseMismatch` warning; `--caseSensitive --strict`
  reports it as an error and blocks generation.
- A mis-cased reference that permissive mode still resolves is emitted with the
  declaration's own spelling, so the generated C++ compiles instead of carrying
  forward a name no declaration introduced.
- `CaseMismatch` (9007) diagnostic, reported even when the run is otherwise
  quiet, since it is a direct consequence of an explicitly requested
  `--caseSensitive`.
- Workspace merging follows the same policy: under `--caseSensitive` two
  declarations that differ only in case are no longer merged into one.

### Fixed
- The signature of an external function block marked every parameter as an
  input, because the direction recorded on the symbol was discarded. A
  `VAR_OUTPUT` therefore received a `set_` call instead of being read back, a
  `VAR_IN_OUT` was treated as a by-value argument, and the "called VAR_OUTPUT
  without naming it" diagnostic was unreachable. Parameters now bind by their
  declared direction, and an `OUTPUT` takes a reference type, matching the
  mapping the code generator builds from the AST for a project-local block.
- `LibrarySymbolImporter` held a `Symbol*` across the `declare()` calls that
  populate a function or function block. Symbols are stored in a `std::vector`,
  so the pointer could dangle once the vector reallocated. The symbol is now
  re-resolved by id at every write point.

### Known issues
- In single-file mode `--output-dir` is ignored: both the header and the source
  are written to the current directory, whatever the flag says. It is honoured
  only in modular project mode. Use `-H` and `-o` to place the two files, as
  `scripts/golden.sh` does. Unchanged in this release.

## [0.4.4] - 2026-09-26

### Fixed
- `LibrarySymbolImporter` restored only the call interface of an imported
  function block, so a consumer could see the parameters of a block declared in
  another file but none of its state or methods. It now imports `members` (with
  their constant and retain flags) and `methods` (each in its own nested scope
  holding its parameters, with its return type and modifiers), and resolves
  `baseType` so an inherited interface resolves through the same chain as in
  source. An override replaces the inherited declaration instead of being
  imported next to it.
- The importer never recorded the block's scope on its symbol, so the symbols it
  declared were unreachable by scope. DeclVisitor does record it, and so does
  the importer now.

## [0.4.3] - 2026-09-26

### Added
- Function block descriptors now describe the whole block instead of only its
  call interface: `members` (internal `VAR`/`VAR_TEMP`/`VAR RETAIN`/`VAR CONSTANT`
  state) and `methods`, plus `baseType`, `interfaces`, `isAbstract` and `isFinal`
- Inherited state and methods are folded into the block with `inherited` /
  `declaredIn` markers, so a consumer can lay out an instance without walking the
  `EXTENDS` chain itself; an overriding method replaces the inherited declaration
  instead of being duplicated
- Diagnostics carry the `.st` file each declaration came from: top-level AST
  entities (`POU`, `StructType`, `EnumType`, `TypeAlias`, `Interface`,
  `VarSection`) record a `fileName`, so a cross-file error in a merged workspace
  names the file it actually points into
- `Diagnostics::addSourceFile()` registers additional sources, letting a
  diagnostic print the snippet of the file its location names
- `schemaVersion` 1.1; the loader accepts both 1.0 and 1.1
- Function block methods carry a `documentation` field, so a method has the
  same shape as a function

### Fixed
- Inherited function block parameters were invisible, which made `EXTENDS`
  unusable at a call site and incomplete in a descriptor.
  `SymbolTable::effectiveParams()` now returns the whole call interface: the
  parameters of the base chain (root ancestor first, so the base interface stays
  the prefix positional calls bind against) followed by the block's own. A
  parameter redeclared by a derived block is emitted once, with the derived
  type. The arity check in `BodyVisitor` and the descriptor export both use it,
  and exported parameters carry `inherited` / `declaredIn` like members and
  methods do. Code generation was already correct
- Workspace merge dropped `typeAliases`, silently making a type alias declared
  in one `.st` file invisible to the others in `--project-style`. The merge now
  lives in a single helper so every declaration kind is forwarded, and type
  aliases are deduplicated across files with a warning when they disagree
- Parse errors pointed at the offending token instead of the insertion point of
  the missing one, blaming unrelated code (often the next line). `expect()` now
  reports just past the last consumed token, so a missing `;` is reported at the
  end of the declaration that lacks it
- A diagnostic whose location named no file printed a header starting with a bare
  `:line:col:`; it now falls back to the primary source name
- An unknown type was reported with no position at all, so a struct member whose
  type is missing produced a bare `file:` header with no line, column or snippet.
  `TypeRef` and `MethodParameter` now record where the type name starts, and
  `resolveTypeRef` prefers that position over the enclosing declaration, so the
  caret lands on the type name (`v : Missing`) rather than on the variable
  (`missing : Missing`)
- `unknown type: X` now quotes the name, as the identifier diagnostics already did
- A circular by-value dependency was reported with no position and a bare type
  list. `Symbol` now records the line, column and source file of its
  declaration, and the cycle report names the member that closes the cycle:
  `'ST_Motor5' contains itself through member 'Motor_': a value of this type has
  no finite size` instead of `circular by-value dependency between types:
  ST_Motor5`. A mutual cycle names both ends. `Symbol` positions are also set for
  method parameters
- Flat workspace mode no longer re-reads each `.st` file to render diagnostics

### Changed
- Build archives (`*.a`) are ignored by a single rule instead of an explicit
  list, so the per-target archives CMake emits next to their sources
  (`include/json/`, `include/library/`, `include/project/`, `include/semantic/`)
  no longer show up as untracked files

## [0.4.2] - 2026-09-25

### Fixed
- String literal generation: `'...'` converted to `"..."` for `std::string` (`String`)
- Wide string literal generation: `"..."` converted to `L"..."` for `std::wstring` (`WString`)
- Added `BaseType` parameter to `genExpr` for type-aware string literal quote conversion
- Updated all variable declaration sites to pass the type hint to `genExpr`

## [0.4.1] - 2026-09-25

### Added
- `--export-descriptor` CLI command for JSON Library Descriptor export
- Integration of CLI tests

### Fixed
- Doxygen output paths in workflow

## [0.4.0] - 2026-09-24

### Added
- Semantic tests for type aliases and array bounds
- IEC TIME literal support
- Forward type reference support

## [0.3.0] - 2026-09-21

### Added
- Semantic analyzer module (SemanticInfo, Diagnostics)
- `--strict` mode: block generation on semantic errors
- Workspace deduplication
- SymbolTable::enterScope/exitScope and lookupGlobal
- Two-pass declaration for forward type references
- CircularDependency detection for by-value type cycles
- CONTINUE, RETURN with expression, EXIT targets
- Location info in ParseError

### Changed
- Resolve named types in the global type namespace with line info
- Semantic-aware type mapping, signatures, enum/FB resolution
- Populate VarDecl::line for local/GVL/method sections

### Fixed
- False strict errors in demo and process image examples
- False strict errors in arrays example

## [0.2.1] - 2026-09-05

### Added
- Typed literals (INT#, REAL#, etc.) support
- Visibility modifiers
- Unit tests for Process Image functionality and ScopeManager
- ScopeManager for variable and AT address management in CodeGenerator
- Parser enhancements for AT address handling and address expression parsing

### Fixed
- Debian 12 compilation support
- CMake minimum version and project version
- Subproject commit reference in undoCore submodule
- Build workflow improvements (Linux, Windows)

## [0.2.0] - 2026-07-12

### Added
- compileSources method for modular project compilation
- MSVC environment setup and compiler detection
- Windows compatibility (popen/pclose)
- Process Image functionality
- AT address expression parsing

### Changed
- Refactored Windows build steps to use vcpkg
- CI improvements (GitHub Actions)

### Fixed
- Removed obsolete test file
- Documentation link updates

## [0.1.2] - 2026-06-13

### Added
- AST enhancements
- Parser improvements
- version.hpp for version information

### Fixed
- CodeGenerator refinements
- Lexer enhancements

## [0.1.1] - 2026-06-12

### Fixed
- Removed unnecessary CMake files (CMakeDoxyfile.in, CPack configs)
- CodeGenerator and Parser refinements
- README updates

## [0.1.0] - 2026-06-09

### Added
- First stable release
- Support for FUNCTION, FUNCTION_BLOCK, PROGRAM
- Struct and Enum support
- Arrays (1D only)
- Project-style generation with dependency management
- Flat mode generation
- Cross-platform support (Linux, Windows, macOS)

### Fixed
- Void return type handling in methods
- Circular dependency detection in structs
- Function Block dependency ordering

### Known Issues
- Multi-dimensional arrays not yet supported
- Limited error recovery in parser
