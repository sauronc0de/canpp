#pragma once

#include <cstddef>
#include <string>

namespace can_core
{
enum class ErrorCode
{
  None,
  InvalidArgument,
  IoFailure,
  ParseFailure,
  DecodeFailure,
  UnsupportedFormat,
};

struct ErrorInfo
{
  ErrorCode code = ErrorCode::None;
  std::string message;
  std::size_t line = 0;
  std::size_t column = 0;

  [[nodiscard]] bool hasError() const
  {
    return code != ErrorCode::None;
  }
};
} // namespace can_core

