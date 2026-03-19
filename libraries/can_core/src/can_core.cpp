#include "can_core/can_event.hpp"

namespace can_core
{
namespace
{
constexpr CanEvent kExampleEvent{};
static_assert(kExampleEvent.payload.size() == 64, "CanEvent payload must support CAN FD");
} // namespace
} // namespace can_core

