# `can_cli` Detailed Design

## Purpose

Provide the first concrete frontend and validation harness.

## Responsibilities

- parse command-line input
- map commands to application use cases
- format text output
- expose benchmark and export commands

## Main Public Types

- `class CliApplication`
- `class CommandRouter`
- `class OutputFormatter`

## Data Types

### `struct CliCommand`

Fields:

- `std::string name`
- `std::vector<std::string> arguments`

### `struct CliOptions`

Fields:

- `bool shouldUseColor`
- `bool shouldPrintHeaders`
- `bool shouldPrintTiming`

## Public API

### `class CliApplication`

- `int run(int argc, char** argv)`

### `class CommandRouter`

- `int dispatch(const CliCommand& cliCommand)`

### `class OutputFormatter`

- `void printEvent(const can_core::CanEvent& canEvent)`
- `void printDecoded(const can_decode::DecodedMessageView& decodedMessageView)`
- `void printError(const ErrorInfo& errorInfo)`

## Main Internal Types

- `class InspectCommand`
- `class QueryCommand`
- `class ExportCommand`
- `class BenchmarkCommand`
- `class CliArgumentParser`

## Internal Design

The CLI should be organized by command handlers. Each command:

1. parses arguments
2. builds a request object
3. invokes `can_app`
4. formats results

## Verification

- command parsing tests
- snapshot-style output tests
- CLI integration tests over sample traces
