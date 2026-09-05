# un(to)do - A TODO for undo

## Automatic Recognition of External Libraries

The transpiler currently has no knowledge of external libraries. The goal is to define a mechanism that allows st2cpp to recognize external C++ libraries (functions and function blocks) via a configuration descriptor.

### Plan

Define a descriptor format (JSON or YAML) that specifies:

- available functions and function blocks
- parameter and return types
- optional documentation

Develop a utility that:

- parses C++ header files (and also ST library directly)
- automatically generates the descriptor file
- optionally produces Markdown/HTML documentation

### Benefits

- Reusability — libraries become plug-and-play
- Better diagnostics — type errors can be caught earlier
- Documentation — automatic generation of reference docs

## Any Other TODO?

This is an open list. Contributions and suggestions are welcome.
