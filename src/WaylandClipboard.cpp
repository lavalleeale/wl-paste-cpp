#include "WaylandClipboard.h"
#include "utils.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <chrono>
#include "base64.h"

WaylandClipboard::~WaylandClipboard()
{
    cleanup();
}

bool WaylandClipboard::initialize()
{
    if (!connection.initialize())
    {
        return false;
    }

    // Register callbacks
    connection.set_offer_callback([this](zwlr_data_control_offer_v1 *, const char *mime_type)
                                  { mime_types.push(mime_type); });
    connection.set_selection_callback([this](zwlr_data_control_device_v1 *device, zwlr_data_control_offer_v1 *offer)
                                      { this->handle_selection(device, offer); });
    connection.set_data_offer_callback([this](zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *)
                                       { this->mime_types = std::queue<std::string>(); });
    connection.set_primary_selection_callback([this](zwlr_data_control_device_v1 *, zwlr_data_control_offer_v1 *offer)
                                              { zwlr_data_control_offer_v1_destroy(offer); });
    connection.set_data_finished_callback([this](zwlr_data_control_device_v1 *)
                                          { cleanup(); });

    if (!connection.create_data_control_device())
    {
        return false;
    }

    if (!setup_pipe())
    {
        return false;
    }

    return true;
}

int WaylandClipboard::run()
{
    wl_display_flush(connection.get_display());

    struct pollfd fds[2];
    setup_polling(fds);

    bool pipe_closed = false;
    bool waited = false;

    while (!pipe_closed)
    {
        try
        {
            // If wayland events are ready then poll was not because of clipboard data
            if (handle_wayland_events(fds))
            {
                continue;
            }
        }
        catch (const std::exception &e)
        {
            return 0;
        }

        if (offer && (waited || (fds[1].revents & POLLIN)))
        {
            process_clipboard_data(fds[1].revents & POLLIN);
        }

        // If we have MIME types to process and there is nothing
        if (!mime_types.empty() && !(fds[1].revents & POLLIN))
        {
            waited = true;
        }

        wl_display_flush(connection.get_display());
    }

    return 0;
}

void WaylandClipboard::setup_polling(struct pollfd fds[2])
{
    fds[0].fd = wl_display_get_fd(connection.get_display());
    fds[0].events = POLLIN;
    fds[1].fd = pipe_fds[0];
    fds[1].events = POLLIN;
}

bool WaylandClipboard::handle_wayland_events(struct pollfd fds[2])
{
    while (wl_display_prepare_read_queue(connection.get_display(), connection.get_event_queue()) != 0)
    {
        wl_display_dispatch_queue(connection.get_display(), connection.get_event_queue());
    }

    auto poll_time = std::chrono::milliseconds(10);
    int ret = poll(fds, 2, poll_time.count());
    if (ret < 0)
    {
        perror("poll");
        throw std::runtime_error("Failed to poll Wayland events");
    }

    if (fds[0].revents & POLLIN)
    {
        wl_display_read_events(connection.get_display());
        wl_display_dispatch_queue(connection.get_display(), connection.get_event_queue());
        return true;
    }
    else
    {
        wl_display_cancel_read(connection.get_display());
        return false;
    }
}

void WaylandClipboard::process_clipboard_data(bool has_pipe_data)
{
    char buf[4096];
    ssize_t n = 0;

    if (has_pipe_data)
    {
        n = read(pipe_fds[0], buf, sizeof(buf));
    }

    if ((n >= 0) && !mime_types.empty())
    {
        std::cout << "Received " << n << " bytes for MIME type: " << mime_types.front() << std::endl;
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
            handle_offer_completion();
        }
    }
}

void WaylandClipboard::handle_offer_completion()
{
    zwlr_data_control_offer_v1_destroy(this->offer);
    this->offer = nullptr;
    std::cout << "Finished offer " << clipboard_history.size() << std::endl;

    if (!clipboard_history.back().empty())
    {
        auto item = std::ranges::find_if(clipboard_history.back(),
                                         [](const auto &pair)
                                         { return pair.first.contains("text/plain;charset=utf-8"); });
        if (item != clipboard_history.back().end())
        {
            std::cout << "Clipboard content (text): " << item->second << std::endl;
        }
        else
        {
            auto first_item = clipboard_history.back().begin();
            std::cout << "Clipboard content (" << first_item->first << "): " << base64_encode(first_item->second.c_str(), first_item->second.size()) << std::endl;
        }
    }
    else
    {
        std::cout << "Clipboard is empty" << std::endl;
    }
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
        wl_display_flush(connection.get_display());
    }
    else
    {
        std::cerr << "No MIME types available in the offer" << std::endl;
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
}