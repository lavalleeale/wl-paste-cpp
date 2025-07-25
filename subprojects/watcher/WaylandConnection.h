#pragma once

#include <wayland-client.h>
#include <wlr-data-control-unstable-v1-client-protocol.h>
#include <string>
#include <functional>
#include <list>
#include <memory>
#include "Offer.h"

class WaylandConnection
{
public:
    WaylandConnection() = default;
    ~WaylandConnection();

    // Delete copy constructor and assignment operator
    WaylandConnection(const WaylandConnection &) = delete;
    WaylandConnection &operator=(const WaylandConnection &) = delete;

    bool initialize();
    void cleanup();

    // Getters
    wl_display *get_display() const { return display; }
    wl_event_queue *get_event_queue() const { return event_queue; }
    wl_seat *get_seat() const { return seat; }
    zwlr_data_control_manager_v1 *get_data_control_manager() const { return dc_manager; }
    zwlr_data_control_device_v1 *get_data_control_device() const { return dc_device; }

    // Callback registration
    void set_offer_ready_callback(std::function<void(std::shared_ptr<Offer>)> callback) { offer_ready_callback = callback; }

    // Create data control device
    bool create_data_control_device();

private:
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_event_queue *event_queue = nullptr;
    wl_seat *seat = nullptr;
    zwlr_data_control_manager_v1 *dc_manager = nullptr;
    zwlr_data_control_device_v1 *dc_device = nullptr;
    std::list<std::shared_ptr<Offer>> offers;

    std::shared_ptr<Offer> get_offer(zwlr_data_control_offer_v1 *offer, bool pop = false);

    // Callbacks
    std::function<void(std::shared_ptr<Offer>)> offer_ready_callback;

    // Static callback wrappers for registry
    static void registry_global_wrapper(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver);
    static void registry_global_remove_wrapper(void *, wl_registry *, uint32_t);

    // Static callback wrappers for data control
    static void offer_handle_wrapper(void *data, zwlr_data_control_offer_v1 *offer, const char *mime_type);
    static void selection_handle_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer);
    static void data_offer_wrapper(void *data, zwlr_data_control_device_v1 *dev, zwlr_data_control_offer_v1 *offer);

    // Member function implementations
    void handle_registry_global(wl_registry *reg, uint32_t name, const char *iface, uint32_t);
    void handle_offer(zwlr_data_control_offer_v1 *offer, const char *mime_type);
    void handle_selection(zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer);
    void handle_data_offer(zwlr_data_control_device_v1 *dev, zwlr_data_control_offer_v1 *offer);

    // Listener structures
    const wl_registry_listener registry_listener = {
        .global = registry_global_wrapper,
        .global_remove = registry_global_remove_wrapper};

    const zwlr_data_control_offer_v1_listener offer_listener = {
        .offer = offer_handle_wrapper};

    const zwlr_data_control_device_v1_listener dc_device_listener = {
        .data_offer = data_offer_wrapper,
        .selection = selection_handle_wrapper,
        .finished = [](void *data,
                       struct zwlr_data_control_device_v1 *)
        { static_cast<WaylandConnection *>(data)->cleanup(); },
        .primary_selection = [](void *, zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
        { zwlr_data_control_offer_v1_destroy(offer); },
    };
};
