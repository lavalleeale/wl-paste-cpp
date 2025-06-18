#include "WaylandClipboard.h"
#include "utils.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <format>
#include <algorithm>
#include <ranges>
#include "base64.h"

WaylandClipboard::~WaylandClipboard()
{
    cleanup();
}

bool WaylandClipboard::initialize()
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

    dc_device = zwlr_data_control_manager_v1_get_data_device(dc_manager, seat);
    wl_proxy_set_queue((struct wl_proxy *)dc_device, event_queue);
    zwlr_data_control_device_v1_add_listener(dc_device, &dc_device_listener, this);

    if (!setup_pipe())
    {
        return false;
    }

    return true;
}

int WaylandClipboard::run()
{
    wl_display_flush(display);

    struct pollfd fds[2];
    fds[0].fd = wl_display_get_fd(display);
    fds[0].events = POLLIN;
    fds[1].fd = pipe_fds[0];
    fds[1].events = POLLIN;

    char buf[4096];
    bool pipe_closed = false;
    bool waited = false;

    while (!pipe_closed)
    {
        while (wl_display_prepare_read_queue(display, event_queue) != 0)
        {
            wl_display_dispatch_queue(display, event_queue);
        }

        int ret = poll(fds, 2, 1000); // Wait for events for 10 ms
        if (ret < 0)
        {
            perror("poll");
            break;
        }
        std::cout << "Poll returned: " << ret << std::endl;

        if (fds[0].revents & POLLIN)
        {
            wl_display_read_events(display);
            wl_display_dispatch_queue(display, event_queue);
        }
        else
        {
            wl_display_cancel_read(display);
        }

        if (fds[1].revents && !(fds[1].revents & POLLIN))
        {
            std::cout << "No data available on pipe with fd " << fds[1].revents << std::endl;
        }
        if (offer && (waited || (fds[1].revents & POLLIN)))
        {
            ssize_t n = 0;
            if (fds[1].revents & POLLIN)
            {
                n = read(pipe_fds[0], buf, sizeof(buf));
            }
            waited = false;
            if ((n >= 0) && !mime_types.empty())
            {
                std::cout << std::format("Received {} bytes for MIME type: {}", n, mime_types.front()) << std::endl;
                std::string content = std::string(buf, n);
                trim(content);
                clipboard_history.back()[mime_types.front()] = content;

                mime_types.pop();
                if (!mime_types.empty())
                {
                    zwlr_data_control_offer_v1_receive(this->offer, mime_types.front().c_str(), pipe_fds[1]);
                }
                else
                {
                    zwlr_data_control_offer_v1_destroy(this->offer);
                    this->offer = nullptr;
                    std::cout << "Finished offer " << clipboard_history.size() << std::endl;
                    if (!clipboard_history.back().empty())
                    {
                        auto item = std::ranges::find_if(clipboard_history.back(),
                                                         [](const auto &pair)
                                                         { return pair.first.contains("text/"); });
                        if (item != clipboard_history.back().end())
                        {
                            std::cout << "Clipboard content (text): " << item->second << std::endl;
                        }
                        else
                        {
                            std::cout << std::format("Clipboard content ({}): {}", item->first, base64_encode(item->second.c_str(), item->second.size())) << std::endl;
                        }
                    }
                    else
                    {
                        std::cout << "Clipboard is empty" << std::endl;
                    }
                }
            }
        }
        // If we have MIME types to process and there is nothing
        if (!mime_types.empty() && !(fds[1].revents & POLLIN))
        {
            waited = true;
        }

        wl_display_flush(display);
    }

    return 0;
}

// Static callback wrappers
void WaylandClipboard::offer_handle_wrapper(void *data, zwlr_data_control_offer_v1 *offer, const char *mime_type)
{
    static_cast<WaylandClipboard *>(data)->handle_offer(offer, mime_type);
}

void WaylandClipboard::selection_handle_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer)
{
    static_cast<WaylandClipboard *>(data)->handle_selection(device, offer);
}

void WaylandClipboard::data_offer_wrapper(void *data, zwlr_data_control_device_v1 *dev, zwlr_data_control_offer_v1 *offer)
{
    static_cast<WaylandClipboard *>(data)->handle_data_offer(dev, offer);
}

void WaylandClipboard::primary_selection_wrapper(void *data, zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer)
{
    static_cast<WaylandClipboard *>(data)->handle_primary_selection(device, offer);
}

void WaylandClipboard::registry_global_wrapper(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver)
{
    static_cast<WaylandClipboard *>(data)->handle_registry_global(reg, name, iface, ver);
}

void WaylandClipboard::registry_global_remove_wrapper(void *, wl_registry *, uint32_t)
{
    // Handle removal if needed
}

void WaylandClipboard::data_finished_wrapper(void *data, zwlr_data_control_device_v1 *)
{
    std::cout << "Data control device finished" << std::endl;
    static_cast<WaylandClipboard *>(data)->cleanup();
}

// Member function implementations
void WaylandClipboard::handle_offer(zwlr_data_control_offer_v1 *, const char *mime_type)
{
    mime_types.push(mime_type);
}

void WaylandClipboard::handle_selection(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
{
    if (!offer)
    {
        std::cerr << "Received null offer" << std::endl;
        return;
    }
    this->offer = offer;
    clipboard_history.push_back(std::map<std::string, std::string>());

    if (!mime_types.empty())
    {
        std::string mime_type = mime_types.front();

        zwlr_data_control_offer_v1_receive(offer, mime_type.c_str(), pipe_fds[1]);
        wl_display_flush(display);
    }
    else
    {
        std::cerr << "No MIME types available in the offer" << std::endl;
    }
}

void WaylandClipboard::handle_data_offer(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
{
    this->mime_types = std::queue<std::string>();
    wl_proxy_set_queue((struct wl_proxy *)offer, event_queue);
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, this);
    wl_display_flush(display);
}

void WaylandClipboard::handle_primary_selection(zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
{
    zwlr_data_control_offer_v1_destroy(offer);
    // Handle primary selection if needed
}

void WaylandClipboard::handle_registry_global(wl_registry *reg, uint32_t name, const char *iface, uint32_t)
{
    if (strcmp(iface, wl_seat_interface.name) == 0)
    {
        seat = static_cast<wl_seat *>(wl_registry_bind(reg, name, &wl_seat_interface, 1));
    }
    else if (strcmp(iface, zwlr_data_control_manager_v1_interface.name) == 0)
    {
        dc_manager = static_cast<zwlr_data_control_manager_v1 *>(
            wl_registry_bind(reg, name, &zwlr_data_control_manager_v1_interface, 2));
    }
}

bool WaylandClipboard::setup_pipe()
{
    if (pipe(pipe_fds) < 0)
    {
        perror("pipe");
        return false;
    }

    int flags = fcntl(pipe_fds[0], F_GETFL);
    fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    return true;
}

void WaylandClipboard::cleanup()
{
    if (pipe_fds[0] != -1)
    {
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    if (pipe_fds[1] != -1)
    {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }

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
