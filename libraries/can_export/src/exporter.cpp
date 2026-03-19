#include "can_export/exporter.hpp"

#include <iomanip>
#include <variant>

namespace can_export
{
namespace
{
bool writePayload(std::ofstream &outputFile, const can_core::CanEvent &canEvent)
{
  for(std::uint8_t index = 0; index < canEvent.dlc; ++index)
  {
    outputFile << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(canEvent.payload[index]);
    if(index + 1U < canEvent.dlc)
    {
      outputFile << ' ';
    }
  }

  outputFile << std::dec << std::setfill(' ');
  return static_cast<bool>(outputFile);
}
} // namespace

Exporter::~Exporter()
{
  if(isOpen_)
  {
    close();
  }
}

bool Exporter::open(const ExportRequest &exportRequest)
{
  exportRequest_ = exportRequest;
  exportSummary_ = {};
  outputFile_.open(exportRequest.outputPath);
  if(!outputFile_.is_open())
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Unable to open export file: " + exportRequest.outputPath;
    return false;
  }

  isOpen_ = true;
  if(exportRequest.shouldWriteHeader)
  {
    return writeHeader();
  }

  return true;
}

bool Exporter::writeRaw(const can_core::CanEvent &canEvent)
{
  if(!isOpen_)
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Exporter is not open";
    return false;
  }

  outputFile_ << canEvent.timestampNs << ','
              << static_cast<unsigned int>(canEvent.channel) << ','
              << std::hex << std::uppercase << canEvent.canId << std::dec << ','
              << static_cast<unsigned int>(canEvent.dlc) << ',';
  if(!writePayload(outputFile_, canEvent))
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Failed to write raw export payload";
    return false;
  }

  outputFile_ << ','
              << (canEvent.frameType == can_core::FrameType::CanFd ? "FD" : "CAN")
              << '\n';
  if(!outputFile_)
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Failed to write raw export row";
    return false;
  }

  ++exportSummary_.writtenRows;
  return true;
}

bool Exporter::writeDecoded(const can_decode::DecodedMessage &decodedMessage, const can_core::CanEvent &canEvent)
{
  if(!isOpen_)
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Exporter is not open";
    return false;
  }

  for(const can_decode::DecodedSignal &decodedSignal : decodedMessage.signals)
  {
    outputFile_ << canEvent.timestampNs << ','
                << std::hex << std::uppercase << canEvent.canId << std::dec << ','
                << decodedMessage.messageName << ','
                << decodedSignal.name << ',';
    if(!writeSignalValue(decodedSignal.value))
    {
      exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
      exportSummary_.errorInfo.message = "Failed to write decoded signal value";
      return false;
    }

    outputFile_ << ',' << decodedSignal.unit << '\n';
    if(!outputFile_)
    {
      exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
      exportSummary_.errorInfo.message = "Failed to write decoded export row";
      return false;
    }

    ++exportSummary_.writtenRows;
  }

  return true;
}

ExportSummary Exporter::close()
{
  if(outputFile_.is_open())
  {
    outputFile_.flush();
    outputFile_.close();
  }

  isOpen_ = false;
  return exportSummary_;
}

bool Exporter::writeHeader()
{
  if(exportRequest_.exportMode == ExportMode::RawCsv)
  {
    outputFile_ << "timestamp_ns,channel,can_id,dlc,payload,frame_type\n";
  }
  else
  {
    outputFile_ << "timestamp_ns,can_id,message_name,signal_name,signal_value,unit\n";
  }

  if(!outputFile_)
  {
    exportSummary_.errorInfo.code = can_core::ErrorCode::IoFailure;
    exportSummary_.errorInfo.message = "Failed to write export header";
    return false;
  }

  return true;
}

bool Exporter::writeEscaped(const std::string &value)
{
  outputFile_ << '"' << value << '"';
  return static_cast<bool>(outputFile_);
}

bool Exporter::writeSignalValue(const can_decode::DecodedSignalValue &decodedSignalValue)
{
  std::visit(
    [this](const auto &value)
    {
      outputFile_ << value;
    },
    decodedSignalValue);
  return static_cast<bool>(outputFile_);
}
} // namespace can_export
