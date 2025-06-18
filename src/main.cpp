#include "WaylandClipboard.h"
#include <iostream>

int main()
{
  WaylandClipboard clipboard;

  if (!clipboard.initialize())
  {
    std::cerr << "Failed to initialize Wayland clipboard" << std::endl;
    return 1;
  }

  return clipboard.run();
}
