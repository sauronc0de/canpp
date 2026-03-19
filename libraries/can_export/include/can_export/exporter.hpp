#pragma once

#include <fstream>
#include <string>

#include "can_core/core_types.hpp"
#include "can_decode/decoder.hpp"

namespace can_export
{
enum class ExportMode
{
  RawCsv,
  DecodedCsv,
};

struct ExportRequest
{
  std::string outputPath;
  ExportMode exportMode = ExportMode::RawCsv;
  bool shouldWriteHeader = true;
};

struct ExportSummary
{
  std::uint64_t writtenRows = 0;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class Exporter
{
public:
  Exporter() = default;
  ~Exporter();

  bool open(const ExportRequest &exportRequest);
  bool writeRaw(const can_core::CanEvent &canEvent);
  bool writeDecoded(const can_decode::DecodedMessage &decodedMessage, const can_core::CanEvent &canEvent);
  ExportSummary close();

private:
  bool writeHeader();
  bool writeEscaped(const std::string &value);
  bool writeSignalValue(const can_decode::DecodedSignalValue &decodedSignalValue);

  std::ofstream outputFile_;
  ExportRequest exportRequest_;
  ExportSummary exportSummary_;
  bool isOpen_ = false;
};
} // namespace can_export
