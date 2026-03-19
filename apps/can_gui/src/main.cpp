#include "log.hpp"
#include "can_gui/gui_application.hpp"

int main()
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Can GUI started");
  can_gui::GuiApplication guiApplication;
  return guiApplication.run();
}

