# CAN Trace Explorer — Software Requirements Specification (ASPICE-aligned)

## 1. Scope

This document defines the software requirements for the CAN Trace Explorer system, an open-source, vendor-independent C++ platform for processing CAN traces.

## 2. Definitions

| Term | Definition |
|------|------------|
| CAN | Controller Area Network |
| DBC | CAN database defining messages and signals |
| Trace | Sequence of CAN frames recorded over time |
| Event | Canonical internal representation of a CAN frame |
| CLI | Command Line Interface |
| GUI | Graphical User Interface |

## 3. System Overview

The system shall consist of:
- Core processing libraries
- File format readers (plugins)
- Database decoding modules (DBC initially)
- Query and filtering engine
- CLI tools for validation
- GUI frontend (Dear ImGui, later phase)
- Lua sandbox allowing scripting layer for user-defined filtering, transformation, and automation.

---

# 4. Functional Requirements

## 4.1 General System Requirements

- SYS-REQ-001: The system shall be implemented in C++.
- SYS-REQ-002: The system shall be vendor-independent and shall not require proprietary SDKs to operate.
- SYS-REQ-003: The system shall support processing of CAN traces containing at least 10 million messages.
- SYS-REQ-004: The system shall support both CAN 2.0 and CAN FD frames.
- SYS-REQ-005: The system shall operate on Linux and Windows platforms.
- SYS-REQ-006: The system shall provide a modular architecture enabling independent development of components.
- SYS-REQ-007: The system shall expose a public API for integration with external tools.

## 4.2 Canonical Data Model

- DATA-REQ-001: The system shall represent all input traces using a unified internal CAN event structure.
- DATA-REQ-002: Each CAN event shall include timestamp, CAN ID, DLC, data payload, bus/channel, and frame type.
- DATA-REQ-003: The timestamp shall be normalized to nanoseconds.
- DATA-REQ-004: The internal CAN event structure shall not require dynamic memory allocation per event.
- DATA-REQ-005: The system shall support payload sizes up to 64 bytes (CAN FD).

## 4.3 File Input / Output

### Reader Framework

- IO-REQ-001: The system shall support a plugin-based architecture for trace file readers.
- IO-REQ-002: Each reader shall implement a common interface for opening, reading, and closing traces.
- IO-REQ-003: The system shall automatically select the appropriate reader based on file format.

### Supported Formats (Initial)

- IO-REQ-010: The system shall support reading candump/log format files.
- IO-REQ-011: The system shall support reading CSV-based CAN trace files.
- IO-REQ-012: The system shall support reading ASC format files.

### Extended Formats

- IO-REQ-020: The system shall support reading BLF files.
- IO-REQ-021: The system shall support reading MDF4/MF4 files.
- IO-REQ-022: The system shall support reading TRC files.

### Output

- IO-REQ-030: The system shall support exporting filtered traces to CSV format.
- IO-REQ-031: The system shall support exporting decoded signals to CSV format.
- IO-REQ-032: The system shall support exporting data to a columnar format.

## 4.4 Database (DBC) Handling

- DBC-REQ-001: The system shall support loading DBC files.
- DBC-REQ-002: The system shall map CAN IDs to message definitions defined in the DBC.
- DBC-REQ-003: The system shall decode CAN frames into signals using DBC definitions.
- DBC-REQ-004: The system shall support signal scaling and offset computation.
- DBC-REQ-005: The system shall support little-endian and big-endian signal encoding.
- DBC-REQ-006: The system shall support signed integer, unsigned integer, IEEE 754 single-precision floating-point, and IEEE 754 double-precision floating-point signal types.
- DBC-REQ-007: The system shall support signal lengths from 1 bit to 64 bits.
- DBC-REQ-008: The system shall support multiplexed signals.
- DBC-REQ-009: The system shall allow operation without a loaded DBC file.

## 4.5 Filtering and Query Engine

- QRY-REQ-001: The system shall support filtering by timestamp range.
- QRY-REQ-002: The system shall support filtering by CAN ID.
- QRY-REQ-003: The system shall support filtering by bus/channel.
- QRY-REQ-004: The system shall support filtering by frame type.
- QRY-REQ-005: The system shall support filtering by message name (DBC-based).
- QRY-REQ-006: The system shall support filtering by signal value (DBC-based).
- QRY-REQ-007: The system shall apply raw filters before decoding operations.
- QRY-REQ-008: The system shall support streaming query execution without loading the full trace into memory.
- QRY-REQ-009: The system shall allow combining multiple filter criteria within a single query.
- QRY-REQ-010: The system shall support logical AND between filter criteria.
- QRY-REQ-011: The system shall support logical OR between filter criteria.
- QRY-REQ-012: The system shall support logical NOT for individual filter criteria.
- QRY-REQ-013: The system shall support retrieval of messages preceding and following a matched message.
- QRY-REQ-014: The system shall allow the number of preceding and following messages to be configured independently.
- QRY-REQ-015: The system shall support retrieval of message context using a specific message occurrence selected from a trace.
- QRY-REQ-016: The system shall support addressing a specific message occurrence by its ordinal position within the trace.
- QRY-REQ-017: The system shall support retrieval of message context from the original trace independently of the active filter criteria.

## 4.6 Indexing and Performance

- PERF-REQ-001: The system shall support sequential streaming processing of trace files.
- PERF-REQ-002: The system shall support optional indexing of large trace files.
- PERF-REQ-003: The system shall allow skipping of non-relevant data using indexes.
- PERF-REQ-004: The system shall process at least 1 million messages per second on a modern desktop CPU.
- PERF-REQ-005: The system shall minimize heap allocations during processing.
- PERF-REQ-006: The system shall support chunk-based processing of large files.

## 4.7 Internal Cache Format

- CACHE-REQ-001: The system shall support an internal binary cache format.
- CACHE-REQ-002: The cache format shall store CAN events in chunked blocks.
- CACHE-REQ-003: The cache format shall support fast random access via indexes.
- CACHE-REQ-004: The system shall allow conversion from external formats to the internal format.
- CACHE-REQ-005: The cache format shall be documented and open.

## 4.8 CLI Tools

- CLI-REQ-001: The system shall provide a CLI tool for reading and printing trace data.
- CLI-REQ-002: The CLI shall support filtering operations.
- CLI-REQ-003: The CLI shall support DBC-based decoding.
- CLI-REQ-004: The CLI shall support exporting filtered results.
- CLI-REQ-005: The CLI shall allow benchmarking of processing performance.

## 4.9 GUI (Future Phase)

- GUI-REQ-001: The system shall provide a GUI based on Dear ImGui.
- GUI-REQ-002: The GUI shall allow loading trace files.
- GUI-REQ-003: The GUI shall allow applying filters interactively.
- GUI-REQ-004: The GUI shall display raw CAN frames.
- GUI-REQ-005: The GUI shall display decoded signals.
- GUI-REQ-006: The GUI shall support searching by CAN ID.
- GUI-REQ-007: The GUI shall support searching by message name.
- GUI-REQ-008: The GUI shall support searching by timestamp.
- GUI-REQ-009: The GUI shall support searching by bus or channel.
- GUI-REQ-010: The GUI shall support searching by frame type.
- GUI-REQ-011: The GUI shall support searching by decoded signal name and decoded signal value when a database is loaded.
- GUI-REQ-012: The GUI shall allow applying multiple search criteria simultaneously.
- GUI-REQ-013: The GUI shall allow combining search criteria using logical AND.
- GUI-REQ-014: The GUI shall support combining search criteria using logical OR.
- GUI-REQ-015: The GUI shall allow filtering data based on distinct values of a selected column.
- GUI-REQ-016: The GUI shall allow selecting which values of a column are visible or hidden.
- GUI-REQ-017: The GUI shall update displayed results dynamically when search or filter criteria are modified.
- GUI-REQ-018: The GUI shall display available distinct values for a selected column to support user filtering.
- GUI-REQ-019: The GUI shall support navigation of the trace data along a time axis.
- GUI-REQ-020: The GUI shall allow the user to move forward and backward in time within the trace.
- GUI-REQ-021: The GUI shall allow zooming in and out of the time axis.

## 4.10 Extensibility and Scripting
- EXT-REQ-001: The system shall provide a scripting interface for user-defined data processing.
- EXT-REQ-002: The scripting interface shall support execution of scripts written in Lua.
- EXT-REQ-003: The system shall allow future support of additional scripting languages without breaking existing functionality.
- EXT-REQ-004: The scripting interface shall allow filtering of CAN events.
- EXT-REQ-005: The scripting interface shall allow transformation of decoded messages.
- EXT-REQ-006: The scripting interface shall provide access to CAN event fields including timestamp, CAN ID, DLC, payload, and bus.
- EXT-REQ-007: The scripting interface shall provide access to decoded signal values when a database is loaded.
- EXT-REQ-008: The scripting interface shall execute user scripts in a sandboxed environment.
- EXT-REQ-009: The scripting interface shall allow enabling and disabling scripting at runtime.
- EXT-REQ-010: The scripting interface shall not be required for core trace reading, indexing, or decoding operations.

---

# 5. Non-Functional Requirements

## Performance

- NFR-PERF-001: The system shall support streaming processing of trace files without requiring the full trace to be loaded into memory.
- NFR-PERF-002: The system shall process a trace containing at least 10 million CAN messages.

## Portability

- NFR-PORT-001: The system shall compile with GCC, Clang, and MSVC.
- NFR-PORT-002: The system shall not depend on platform-specific APIs in core modules.

## Maintainability

- NFR-MAINT-001: The system shall follow modular architecture principles.
- NFR-MAINT-002: Each module shall be independently testable.
- NFR-MAINT-003: Public APIs shall be documented with doxygen.

## Testability

- NFR-TEST-001: The system shall include performance benchmarks.

---

# 6. Constraints

- CON-REQ-001: The system shall not rely on proprietary file format SDKs.
- CON-REQ-002: The system shall prioritize open specifications where available.

---

# 7. Future Extensions

- FUT-REQ-001: The system shall allow integration of additional database formats.
- FUT-REQ-002: The system shall allow integration with live CAN interfaces (Peak).
- FUT-REQ-003: The system shall support distributed processing of large datasets.
