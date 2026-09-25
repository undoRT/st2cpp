# Changelog

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
