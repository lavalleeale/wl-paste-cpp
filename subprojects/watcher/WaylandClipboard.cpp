#include "WaylandClipboard.h"
#include "PosixIO.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <cerrno>
#include <algorithm>

// Constants
static constexpr size_t BUFFER_SIZE = 4096;
static constexpr int POLL_FD_COUNT = 2;
static constexpr int POLL_TIMEOUT_IDLE = 500;
static constexpr int POLL_TIMEOUT_EXPECTING = 1000;
static constexpr int READ_FD_INDEX = 0;
static constexpr int WRITE_FD_INDEX = 1;
static constexpr int WAYLAND_FD_INDEX = 0;
static constexpr int PIPE_FD_INDEX = 1;
static constexpr size_t MAX_MIME_CONTENT_SIZE = 1024 * 1024;

WaylandClipboard::~WaylandClipboard()
{
    cleanup();
}

bool WaylandClipboard::initialize()
{
    load_clipboard_data();

    // Register callbacks
    connection.set_offer_ready_callback([this](std::shared_ptr<Offer> offer)
                                        { this->handle_selection(offer); });
    if (!connection.initialize())
    {
        return false;
    }

    if (!connection.create_data_control_device())
    {
        return false;
    }

    return true;
}

int WaylandClipboard::run()
{
    wl_display_flush(connection.get_display());

    struct pollfd fds[POLL_FD_COUNT];
    setup_polling(fds);

    bool waited = false;

    while (connection.is_running())
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
            std::cerr << e.what() << std::endl;
            return 1;
        }

        if (offer && read_fd >= 0)
        {
            if (waited || (fds[PIPE_FD_INDEX].revents & POLLIN))
            {
                process_clipboard_data(fds[PIPE_FD_INDEX].revents & POLLIN);
            }
            if (read_fd >= 0 && !(fds[PIPE_FD_INDEX].revents & POLLIN))
            {
                waited = true;
            }
            else
            {
                waited = false;
            }
        }

        if (wl_display_flush(connection.get_display()) < 0)
        {
            std::cerr << "Failed to flush Wayland display" << std::endl;
            return 1;
        }
    }

    return 0;
}

void WaylandClipboard::setup_polling(struct pollfd fds[POLL_FD_COUNT])
{
    fds[WAYLAND_FD_INDEX].fd = wl_display_get_fd(connection.get_display());
    fds[WAYLAND_FD_INDEX].events = POLLIN;
    fds[PIPE_FD_INDEX].fd = read_fd;
    fds[PIPE_FD_INDEX].events = POLLIN;
    fds[PIPE_FD_INDEX].revents = 0;
}

bool WaylandClipboard::handle_wayland_events(struct pollfd fds[POLL_FD_COUNT])
{
    while (wl_display_prepare_read_queue(connection.get_display(), connection.get_event_queue()) != 0)
    {
        wl_display_dispatch_queue(connection.get_display(), connection.get_event_queue());
    }

    setup_polling(fds);
    int timeout = (read_fd >= 0) ? POLL_TIMEOUT_EXPECTING : POLL_TIMEOUT_IDLE;
    int ret = poll(fds, POLL_FD_COUNT, timeout);
    if (ret < 0)
    {
        perror("poll");
        throw std::runtime_error("Failed to poll Wayland events");
    }

    if (fds[WAYLAND_FD_INDEX].revents & POLLIN)
    {
        if (wl_display_read_events(connection.get_display()) < 0 ||
            wl_display_dispatch_queue(connection.get_display(), connection.get_event_queue()) < 0)
        {
            throw std::runtime_error("Failed to read Wayland events");
        }
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
    char buf[BUFFER_SIZE];
    bool saw_eof = !has_pipe_data;

    while (read_fd >= 0)
    {
        ssize_t n = read(read_fd, buf, sizeof(buf));
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            perror("read");
            saw_eof = true;
            break;
        }
        if (n == 0)
        {
            saw_eof = true;
            break;
        }
        const auto remaining = MAX_MIME_CONTENT_SIZE - current_content.size();
        current_content.append(buf, std::min<std::size_t>(static_cast<std::size_t>(n), remaining));
    }

    if (saw_eof && offer && !current_mime.empty() && !clipboard_history.empty())
    {
        clipboard_history.front()[current_mime] = current_content;
        finish_current_mime_read();

        if (offer->has_mime_types())
        {
            start_next_mime_read();
        }
        else
        {
            handle_offer_completion();
        }
    }
}

void WaylandClipboard::handle_offer_completion()
{
    std::cout << "Offer completed, processing clipboard data" << std::endl;
    offer.reset();
    current_mime.clear();
    current_content.clear();
    if (clipboard_history.empty() || clipboard_history.front().empty())
    {
        if (!clipboard_history.empty())
        {
            clipboard_history.erase(clipboard_history.begin());
        }
        return;
    }
    // Check if we already have this entry (important on startup)
    if (clipboard_history.size() > 1)
    {
        const auto first = clipboard_history.front();
        const auto second = clipboard_history[1];
        for (const auto &[key, value] : second)
        {
            if (copied)
            {
                if (first == second)
                {
                    // If the first and second entries are the same, we can skip this check
                    std::cerr << "Skipping duplicate entry in clipboard history" << std::endl;
                    clipboard_history.erase(clipboard_history.begin());
                    break;
                }
            }
            else
            {
                auto first_value = first.find(key);
                if (value != "" && first_value != first.end() && first_value->second == value)
                {
                    clipboard_history.erase(clipboard_history.begin() + 1);
                    break;
                }
            }
        }
        copied = true; // Indicate that we have copied data
    }
    save_clipboard_data();
}

void WaylandClipboard::load_clipboard_data()
{
    clipboard_history = clipboard::load_history();
}

void WaylandClipboard::save_clipboard_data()
{
    clipboard::save_history(clipboard_history);
}

void WaylandClipboard::handle_selection(std::shared_ptr<Offer> offer)
{
    if (this->offer && !clipboard_history.empty() && clipboard_history.front().empty())
    {
        clipboard_history.erase(clipboard_history.begin());
    }
    finish_current_mime_read();
    this->offer = offer;
    if (!offer || !offer->has_mime_types())
    {
        std::cerr << "No MIME types available in the offer" << std::endl;
        return;
    }

    clipboard_history.insert(clipboard_history.begin(), clipboard::ClipboardEntry());
    clipboard::trim_history(clipboard_history);
    if (!start_next_mime_read())
    {
        clipboard_history.erase(clipboard_history.begin());
        this->offer.reset();
    }
}

bool WaylandClipboard::start_next_mime_read()
{
    if (!offer || !offer->has_mime_types())
    {
        return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) < 0)
    {
        perror("pipe");
        return false;
    }
    clipboard::UniqueFd read_pipe(pipe_fds[READ_FD_INDEX]);
    clipboard::UniqueFd write_pipe(pipe_fds[WRITE_FD_INDEX]);

    if (!clipboard::set_nonblocking(read_pipe.get()))
    {
        perror("fcntl");
        return false;
    }

    read_fd = read_pipe.release();
    current_mime = offer->pop_mime_type();
    current_content.clear();
    offer->receive_mime(current_mime, write_pipe.get());
    write_pipe.reset();
    if (wl_display_flush(connection.get_display()) < 0)
    {
        std::cerr << "Failed to flush Wayland display" << std::endl;
        finish_current_mime_read();
        return false;
    }
    return true;
}

void WaylandClipboard::finish_current_mime_read()
{
    if (read_fd >= 0)
    {
        close(read_fd);
        read_fd = -1;
    }
    current_mime.clear();
    current_content.clear();
}

void WaylandClipboard::cleanup()
{
    finish_current_mime_read();
    offer.reset();
}
