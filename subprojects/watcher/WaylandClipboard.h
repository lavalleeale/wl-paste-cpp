#pragma once

#include <wayland-client.h>
#include <wlr-data-control-unstable-v1-client-protocol.h>
#include "WaylandConnection.h"
#include <string>
#include <queue>
#include <map>
#include <memory>
#include "ClipboardHistory.h"

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
    int read_fd = -1;
    std::shared_ptr<Offer> offer = nullptr;
    clipboard::ClipboardHistory clipboard_history;
    std::string current_mime;
    std::string current_content;
    bool copied = false;

    // Callback implementations
    void handle_selection(std::shared_ptr<Offer> offer);

    void cleanup();

    // Helper methods for run() function
    void setup_polling(struct pollfd fds[2]);
    bool handle_wayland_events(struct pollfd fds[2]);
    void process_clipboard_data(bool has_pipe_data);
    void handle_offer_completion();
    bool start_next_mime_read();
    void finish_current_mime_read();

    void save_clipboard_data();
    void load_clipboard_data();
};
