#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <wayland-client.h>
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include <iostream>
#include <string>
#include <queue>
#include <map>
#include <vector>
#include "utils.h"

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
