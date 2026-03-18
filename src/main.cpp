#include "log.hpp"
#include "canapp.hpp"

int main(int, char **)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("🗒️  CanApp started 🗒️");
  CanApp app;
  return app.run();
}