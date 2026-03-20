#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "can_reader_api/trace_reader.hpp"

namespace can_readers_binary
{
class BinaryCursor
{
public:
  BinaryCursor() = default;
  explicit BinaryCursor(std::vector<std::byte> data);

  [[nodiscard]] bool isValid() const;
  [[nodiscard]] std::size_t remaining() const;
  bool advance(std::size_t byteCount);

private:
  std::vector<std::byte> data_;
  std::size_t position_ = 0;
};

class UnsupportedBinaryReader : public can_reader_api::ITraceReader
{
public:
  bool open(const can_reader_api::SourceDescriptor &sourceDescriptor, const can_reader_api::ReaderOptions &readerOptions) override;
  can_reader_api::ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) override;
  [[nodiscard]] can_core::TraceMetadata metadata() const override;
  [[nodiscard]] can_reader_api::ReaderCapabilities capabilities() const override;
  void close() override;

protected:
  virtual bool canParseExtension(std::string_view extension) const = 0;
  [[nodiscard]] virtual std::string formatName() const = 0;

private:
  can_core::TraceMetadata traceMetadata_;
  bool isOpen_ = false;
};

class BlfReader : public UnsupportedBinaryReader
{
public:
  bool canParseExtension(std::string_view extension) const override;
  [[nodiscard]] std::string formatName() const override;
};

class Mf4Reader : public UnsupportedBinaryReader
{
public:
  bool canParseExtension(std::string_view extension) const override;
  [[nodiscard]] std::string formatName() const override;
};

class TrcReader : public UnsupportedBinaryReader
{
public:
  bool canParseExtension(std::string_view extension) const override;
  [[nodiscard]] std::string formatName() const override;
};

template <typename ReaderType>
class BinaryReaderFactory : public can_reader_api::ITraceReaderFactory
{
public:
  [[nodiscard]] bool canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const override
  {
    ReaderType reader;
    return reader.canParseExtension(sourceDescriptor.extension);
  }

  [[nodiscard]] std::unique_ptr<can_reader_api::ITraceReader> create() const override
  {
    return std::make_unique<ReaderType>();
  }

  [[nodiscard]] std::string formatName() const override
  {
    ReaderType reader;
    return reader.formatName();
  }
};

using BlfReaderFactory = BinaryReaderFactory<BlfReader>;
using Mf4ReaderFactory = BinaryReaderFactory<Mf4Reader>;
using TrcReaderFactory = BinaryReaderFactory<TrcReader>;
} // namespace can_readers_binary

