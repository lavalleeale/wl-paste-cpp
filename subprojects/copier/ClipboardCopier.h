#pragma once

#include <string>
#include <wayland-client.h>
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include <vector>
#include <map>

class ClipboardCopier
{
public:
    ClipboardCopier(const std::string &command);
    int run();

private:
    bool init();
    void cleanup();

    // Listener callbacks (static)
    static void registry_global_s(void *data, struct wl_registry *reg, uint32_t name, const char *interface, uint32_t version);
    static void registry_global_remove_s(void *data, struct wl_registry *reg, uint32_t name);
    static void data_source_send_s(void *data, struct zwlr_data_control_source_v1 *source, const char *mime, int32_t fd);
    static void data_source_cancelled_s(void *data, struct zwlr_data_control_source_v1 *source);

    void load_clipboard_data();

    // Wayland objects
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_seat *seat = nullptr;
    zwlr_data_control_manager_v1 *data_control_manager = nullptr;
    zwlr_data_control_source_v1 *data_source = nullptr;
    zwlr_data_control_device_v1 *data_control_device = nullptr;

    // State
    bool running = true;
    std::map<std::string, std::string> clipboard_data;
    std::vector<std::map<std::string, std::string>> clipboard_history;

    // Listener structs
    static const struct wl_registry_listener registry_listener;
    static const struct zwlr_data_control_source_v1_listener data_source_listener;
};
