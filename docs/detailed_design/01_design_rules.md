# Detailed Design Rules

## 1. Purpose

This document defines the detailed-design rules that shall be applied to all
software components. These rules are derived from:

- [`../guide_lines.md`](../guide_lines.md)
- [`../architecture/01_architecture_overview.md`](../architecture/01_architecture_overview.md)
- [`../architecture/02_module_decomposition.md`](../architecture/02_module_decomposition.md)
- [`../architecture/03_interface_contracts.md`](../architecture/03_interface_contracts.md)

## 2. Naming Rules

The following rules shall be applied in all detailed design decisions:

- File names use `snake_case`
- Class names use `PascalCase`
- Struct names use `PascalCase`
- Enum names and enum values use `PascalCase`
- Function names use `camelCase`
- Variable names use `camelCase`
- Member variables use `camelCase_`
- Constants use `kPrefixPascalCase`
- Namespaces use `snake_case`

These rules apply to:

- headers and source files
- class and struct names
- internal helper types
- function and method names
- configuration types
- result and error types

## 3. Source Layout Rules

Each component should follow this structure when implementation begins:

```text
libraries/<component>/
  include/<component>/
  src/
  tests/
  benchmarks/    # only if performance-sensitive
```

Frontend applications follow:

```text
apps/<frontend>/
  include/<frontend>/   # only if needed
  src/
  tests/
```

## 4. Design-Level Coding Rules

- Public API types should be minimal and stable
- Internal helper classes should remain inside `src/` unless they are part of
  the public contract
- Hot-path data structures should prefer value semantics and contiguous storage
- Per-event heap allocation shall be avoided in performance-critical code
- Frontend-specific types shall not appear in lower architectural layers
- Optional features shall be isolated behind explicit interfaces

## 5. Error Handling Rules

- Error categories should be explicit and typed
- Parsing and decode failures should capture location or message context where
  available
- Recoverable data issues should not require process termination
- Core modules should not depend on frontend formatting for error reporting

## 6. Documentation Rules

Each component detailed design shall specify:

- Purpose
- Main responsibilities
- Internal types and classes
- Public API surface
- Internal collaborators
- Data ownership rules
- Threading or streaming assumptions
- Performance-sensitive areas
- Tests and verification expectations

## 7. Current Consistency Assessment

No blocking inconsistency requiring an immediate architecture change was found
between the current style guide and the architecture proposal.

Observations:

- Component names such as `can_core` and `can_query` already follow
  `snake_case`
- Proposed class names such as `CanEvent`, `QuerySpec`, and `TraceSession`
  follow `PascalCase`
- Proposed namespaces such as `can_core`, `can_query`, and `can_app` follow
  `snake_case`
- The use of `I` prefixes for abstract interfaces is not defined by the style
  guide but does not conflict with it

