# `can_export` Detailed Design

## Purpose

Export query results to external formats.

## Responsibilities

- export raw events
- export decoded signals
- support CSV initially
- support columnar export later

## Main Public Types

- `class Exporter`
- `struct ExportRequest`
- `enum class ExportMode`

## Data Types

### `struct ExportRequest`

Fields:

- `std::string outputPath`
- `ExportMode exportMode`
- `bool shouldWriteHeader`
- `std::vector<std::string> selectedColumns`

### `enum class ExportMode`

Values:

- `RawCsv`
- `DecodedCsv`
- `Columnar`

## Public API

### `class Exporter`

- `bool open(const ExportRequest& exportRequest)`
- `void writeRaw(const can_core::CanEvent& canEvent)`
- `void writeDecoded(const can_decode::DecodedMessageView& decodedMessageView)`
- `void close()`

## Main Internal Types

- `class CsvRawExporter`
- `class CsvDecodedExporter`
- `class ColumnarExporter`
- `class OutputStreamAdapter`

## Internal Design

Export behavior is strategy-based:

- raw export path writes `CanEvent` projections
- decoded export path writes decoded signal projections
- exporter instances receive rows incrementally from query execution

## Ownership Rules

- export request owns target path and output options
- exporter owns stream state and header emission state

## Verification

- CSV formatting tests
- output schema tests
- streaming export tests on large result sets
