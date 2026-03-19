#pragma once

#include "can_core/can_event.hpp"

namespace can_reader_api
{
class ITraceReader
{
public:
  virtual ~ITraceReader() = default;
};
} // namespace can_reader_api

