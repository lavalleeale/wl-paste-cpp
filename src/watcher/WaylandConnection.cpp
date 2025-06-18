#include "WaylandConnection.h"
#include <iostream>
#include <string>
#include <algorithm>

WaylandConnection::~WaylandConnection()
{
    cleanup();
}

bool WaylandConnection::initialize()
{
    display = wl_display_connect(nullptr);
    if (!display)
    {
        std::cerr << "Failed to connect to Wayland display" << std::endl;
        return false;
    }

    event_queue = wl_display_create_queue(display);
    if (!event_queue)
    {
        std::cerr << "Failed to create event queue" << std::endl;
        return false;
    }

    registry = wl_display_get_registry(display);
    wl_proxy_set_queue((struct wl_proxy *)registry, event_queue);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip_queue(display, event_queue);

    if (!seat || !dc_manager)
    {
        std::cerr << "Required Wayland protocols not available" << std::endl;
        return false;
    }

    return true;
}

void WaylandConnection::cleanup()
{
    if (dc_device)
    {
        zwlr_data_control_device_v1_destroy(dc_device);
        dc_device = nullptr;
    }
    if (dc_manager)
    {
        zwlr_data_control_manager_v1_destroy(dc_manager);
        dc_manager = nullptr;
    }
    if (seat)
    {
        wl_seat_destroy(seat);
        seat = nullptr;
    }
    if (registry)
    {
        wl_registry_destroy(registry);
        registry = nullptr;
    }
    if (event_queue)
    {
        wl_event_queue_destroy(event_queue);
        event_queue = nullptr;
    }
    if (display)
    {
        wl_display_disconnect(display);
        display = nullptr;
    }
}

// Static callback wrapper
void WaylandConnection::registry_global_wrapper(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver)
{
    static_cast<WaylandConnection *>(data)->handle_registry_global(reg, name, iface, ver);
}

void WaylandConnection::registry_global_remove_wrapper(void *, wl_registry *, uint32_t)
{
    // Handle removal if needed
}

// Member function implementation
void WaylandConnection::handle_registry_global(wl_registry *reg, uint32_t name, const char *iface, uint32_t)
{
    if (std::string(iface) == wl_seat_interface.name)
    {
        seat = static_cast<wl_seat *>(wl_registry_bind(reg, name, &wl_seat_interface, 1));
    }
    else if (std::string(iface) == zwlr_data_control_manager_v1_interface.name)
    {
        dc_manager = static_cast<zwlr_data_control_manager_v1 *>(
            wl_registry_bind(reg, name, &zwlr_data_control_manager_v1_interface, 2));
    }
}

std::shared_ptr<Offer> WaylandConnection::get_offer(zwlr_data_control_offer_v1 *offer, bool pop)
{
    auto it = std::ranges::find_if(offers,
                                   [offer](const std::shared_ptr<Offer> &o)
                                   { return o->matches(offer); });
    if (it != offers.end())
    {
        auto offer = *it;
        if (pop)
        {
            offers.erase(it);
        }
        return offer;
    }
    throw std::runtime_error("Offer not found");
}

bool WaylandConnection::create_data_control_device()
{
    if (!dc_manager || !seat)
    {
        std::cerr << "Cannot create data control device: missing manager or seat" << std::endl;
        return false;
    }

    dc_device = zwlr_data_control_manager_v1_get_data_device(dc_manager, seat);
    if (!dc_device)
    {
        std::cerr << "Failed to create data control device" << std::endl;
        return false;
    }

    wl_proxy_set_queue((struct wl_proxy *)dc_device, event_queue);
    zwlr_data_control_device_v1_add_listener(dc_device, &dc_device_listener, this);

    return true;
}

// New static callback wrappers for data control
void WaylandConnection::offer_handle_wrapper(void *data, zwlr_data_control_offer_v1 *offer, const char *mime_type)
{
    static_cast<WaylandConnection *>(data)->handle_offer(offer, mime_type);
}

void WaylandConnection::selection_handle_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer)
{
    static_cast<WaylandConnection *>(data)->handle_selection(device, offer);
}

void WaylandConnection::data_offer_wrapper(void *data, zwlr_data_control_device_v1 *dev, zwlr_data_control_offer_v1 *offer)
{
    static_cast<WaylandConnection *>(data)->handle_data_offer(dev, offer);
}

// New member function implementations for data control
void WaylandConnection::handle_offer(zwlr_data_control_offer_v1 *offer_ptr, const char *mime_type)
{
    if (!offer_ptr || !mime_type)
    {
        std::cerr << "Received null offer or mime_type" << std::endl;
        return;
    }
    try
    {
        auto offer = get_offer(offer_ptr);
        offer->add_mime_type(mime_type);
    }
    catch (const std::runtime_error &e)
    {
        std::cerr << "Error handling offer: " << e.what() << std::endl;
    }
}

void WaylandConnection::handle_selection(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer_ptr)
{
    if (offer_ready_callback)
    {
        try
        {
            offer_ready_callback(get_offer(offer_ptr, true));
        }
        catch (const std::runtime_error &e)
        {
            std::cerr << "Error handling offer: " << e.what() << std::endl;
        }
    }
}

void WaylandConnection::handle_data_offer(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
{
    if (!offer)
    {
        std::cerr << "Received null offer" << std::endl;
        return;
    }
    wl_proxy_set_queue((struct wl_proxy *)offer, event_queue);
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, this);
    wl_display_flush(display);
    offers.push_back(std::make_shared<Offer>(offer));
}
