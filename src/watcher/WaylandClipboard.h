#pragma once

#include <wayland-client.h>
#include <wlr-data-control-unstable-v1-client-protocol.h>
#include "WaylandConnection.h"
#include <string>
#include <queue>
#include <map>
#include <list>
#include <memory>

class WaylandClipboard
{
public:
    WaylandClipboard() = default;
    ~WaylandClipboard();

    // Delete copy constructor and assignment operator
    WaylandClipboard(const WaylandClipboard &) = delete;
    WaylandClipboard &operator=(const WaylandClipboard &) = delete;

    bool initialize();
    int run();

private:
    WaylandConnection connection;
    int pipe_fds[2] = {-1, -1};
    std::shared_ptr<Offer> offer = nullptr;
    std::list<std::map<std::string, std::string>> clipboard_history;
    bool copied = false;

    // Callback implementations
    void handle_selection(std::shared_ptr<Offer> offer);

    bool setup_pipe();
    void cleanup();

    // Helper methods for run() function
    void setup_polling(struct pollfd fds[2]);
    bool handle_wayland_events(struct pollfd fds[2]);
    void process_clipboard_data(bool has_pipe_data);
    void handle_offer_completion();

    void save_clipboard_data();
    void load_clipboard_data();
};