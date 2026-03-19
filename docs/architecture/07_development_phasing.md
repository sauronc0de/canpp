# Development Phasing

## 1. Purpose

This document proposes how the architecture should be delivered in phases
without redesigning the core at each step.

## 2. Phase 1: Core Libraries

### Scope

Build the reusable architecture foundation:

- `can_core`
- `can_reader_api`
- `can_readers_text`
- `can_dbc`
- `can_decode`
- `can_query`
- `can_export` with CSV
- optional first version of `can_cache`

### Deliverables

- Canonical `CanEvent`
- Reader framework and initial text readers
- DBC parsing and decode
- Streaming raw and decoded queries
- CSV export
- Unit tests and initial performance benchmarks

### Goal

Validate the architecture on real traces without introducing frontend
complexity.

## 3. Phase 2: CLI Validation

### Scope

Build:

- `can_app`
- `can_cli`
- benchmark command set
- cache and index tooling as needed for workload validation

### Deliverables

- CLI commands for inspect, filter, decode, export, benchmark
- Integration tests over realistic traces
- Performance evidence against phase-1 assumptions

### Goal

Use the CLI as the first complete consumer of the architecture and as the main
regression harness.

## 4. Phase 3: GUI Integration

### Scope

Build:

- `can_gui`
- GUI-specific view models
- cache or index assisted navigation
- incremental result presentation

### Deliverables

- Trace loading
- Interactive filters
- Raw and decoded views
- Time-axis navigation
- Distinct-value filtering support

### Goal

Add interactive analysis without changing lower-layer processing contracts.

## 5. Phase 4: Extensibility Growth

Potential additions:

- Extended binary readers
- Columnar export
- Lua scripting
- Additional database formats
- Live CAN adapters

## 6. Phase Transition Rules

- Phase 2 must consume the same core contracts created in phase 1
- Phase 3 must consume the same application services created in phase 2
- Optional features must remain additive and must not destabilize the core path
- New adapters should be introduced without changing `can_core`

