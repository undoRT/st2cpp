# Changelog

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
