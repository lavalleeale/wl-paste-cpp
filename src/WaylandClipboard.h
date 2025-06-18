#pragma once

#include <wayland-client.h>
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include <string>
#include <queue>
#include <map>
#include <vector>

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
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_event_queue *event_queue = nullptr;
    zwlr_data_control_manager_v1 *dc_manager = nullptr;
    wl_seat *seat = nullptr;
    zwlr_data_control_device_v1 *dc_device = nullptr;
    int pipe_fds[2] = {-1, -1};
    std::queue<std::string> mime_types;
    zwlr_data_control_offer_v1 *offer = nullptr;
    std::vector<std::map<std::string, std::string>> clipboard_history;

    // Static callback wrappers
    static void offer_handle_wrapper(void *data, zwlr_data_control_offer_v1 *offer, const char *mime_type);
    static void selection_handle_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer);
    static void data_offer_wrapper(void *data, zwlr_data_control_device_v1 *dev, zwlr_data_control_offer_v1 *offer);
    static void primary_selection_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer);
    static void registry_global_wrapper(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver);
    static void registry_global_remove_wrapper(void *, wl_registry *, uint32_t);
    static void data_finished_wrapper(void *data, zwlr_data_control_device_v1 *);

    // Member function implementations
    void handle_offer(zwlr_data_control_offer_v1 *, const char *mime_type);
    void handle_selection(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer);
    void handle_data_offer(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer);
    void handle_primary_selection(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer);
    void handle_registry_global(wl_registry *reg, uint32_t name, const char *iface, uint32_t);

    // Listener structures
    const zwlr_data_control_offer_v1_listener offer_listener = {
        .offer = offer_handle_wrapper};

    const zwlr_data_control_device_v1_listener dc_device_listener = {
        .data_offer = data_offer_wrapper,
        .selection = selection_handle_wrapper,
        .finished = data_finished_wrapper,
        .primary_selection = primary_selection_wrapper,
    };

    const wl_registry_listener registry_listener = {
        .global = registry_global_wrapper,
        .global_remove = registry_global_remove_wrapper};

    bool setup_pipe();
    void cleanup();
};