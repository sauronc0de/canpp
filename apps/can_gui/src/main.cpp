#include "log.hpp"
#include "can_gui/gui_application.hpp"

#include <string>
#include <vector>

int main(int argc, char **argv)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Can GUI started");
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for(int index = 0; index < argc; ++index)
  {
    arguments.emplace_back(argv[index]);
  }
  can_gui::GuiApplication guiApplication(std::move(arguments));
  return guiApplication.run();
}
