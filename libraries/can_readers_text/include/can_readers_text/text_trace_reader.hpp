#pragma once

#include <array>
#include <fstream>
#include <memory>
#include <string>

#include "can_reader_api/trace_reader.hpp"

namespace can_readers_text
{
struct ParsedTextRecord
{
  std::uint64_t timestampNs = 0;
  std::uint32_t canId = 0;
  std::uint8_t dlc = 0;
  std::uint8_t channel = 0;
  can_core::FrameType frameType = can_core::FrameType::Can20;
  std::array<std::uint8_t, 64> payload{};
};

class TextLineReader
{
public:
  bool open(const std::string &path);
  bool readLine(std::string &line);
  void close();
  [[nodiscard]] bool isOpen() const;
  [[nodiscard]] std::uint64_t fileSizeBytes() const;
  [[nodiscard]] std::uint64_t consumedSizeBytes() const;

private:
  std::ifstream inputFile_;
  std::uint64_t fileSizeBytes_ = 0;
  std::uint64_t consumedSizeBytes_ = 0;
};

class TextTraceReaderBase : public can_reader_api::ITraceReader
{
public:
  bool open(
    const can_reader_api::SourceDescriptor &sourceDescriptor,
    const can_reader_api::ReaderOptions &readerOptions) override;
  can_reader_api::ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) override;
  [[nodiscard]] can_core::TraceMetadata metadata() const override;
  [[nodiscard]] can_reader_api::ReaderCapabilities capabilities() const override;
  void close() override;
  [[nodiscard]] virtual bool canParseExtension(std::string_view extension) const = 0;

protected:
  virtual bool parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const = 0;
  [[nodiscard]] virtual std::string formatName() const = 0;
  [[nodiscard]] virtual bool shouldSkipLine(const std::string &line) const;

private:
  can_reader_api::ReadResult makeParseError(std::size_t lineNumber, const std::string &message) const;
  static can_core::CanEvent makeCanEvent(const ParsedTextRecord &parsedTextRecord);

  TextLineReader textLineReader_;
  can_reader_api::ReaderOptions readerOptions_;
  can_core::TraceMetadata traceMetadata_;
  std::size_t currentLineNumber_ = 0;
  bool isOpen_ = false;
};

class CandumpReader : public TextTraceReaderBase
{
public:
  bool canParseExtension(std::string_view extension) const override;

protected:
  bool parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const override;
  [[nodiscard]] std::string formatName() const override;
};

class CsvTraceReader : public TextTraceReaderBase
{
public:
  bool canParseExtension(std::string_view extension) const override;

protected:
  bool parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const override;
  [[nodiscard]] std::string formatName() const override;
  [[nodiscard]] bool shouldSkipLine(const std::string &line) const override;
};

class AscTraceReader : public TextTraceReaderBase
{
public:
  bool canParseExtension(std::string_view extension) const override;

protected:
  bool parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const override;
  [[nodiscard]] std::string formatName() const override;
  [[nodiscard]] bool shouldSkipLine(const std::string &line) const override;
};

class CandumpReaderFactory : public can_reader_api::ITraceReaderFactory
{
public:
  [[nodiscard]] bool canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const override;
  [[nodiscard]] std::unique_ptr<can_reader_api::ITraceReader> create() const override;
  [[nodiscard]] std::string formatName() const override;
};

class CsvTraceReaderFactory : public can_reader_api::ITraceReaderFactory
{
public:
  [[nodiscard]] bool canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const override;
  [[nodiscard]] std::unique_ptr<can_reader_api::ITraceReader> create() const override;
  [[nodiscard]] std::string formatName() const override;
};

class AscTraceReaderFactory : public can_reader_api::ITraceReaderFactory
{
public:
  [[nodiscard]] bool canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const override;
  [[nodiscard]] std::unique_ptr<can_reader_api::ITraceReader> create() const override;
  [[nodiscard]] std::string formatName() const override;
};
} // namespace can_readers_text
