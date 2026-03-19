#include "log.hpp"
#include "can_app/can_app.hpp"

int main(int, char **)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Can CLI started");
  can_app::CanApp app;
  return app.run();
}
