#include "WaylandClipboard.h"
#include "utils.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>
#include "base64.h"
#include <fstream>

// Constants
static constexpr size_t BUFFER_SIZE = 4096;
static constexpr int POLL_FD_COUNT = 2;
static constexpr int POLL_TIMEOUT_IDLE = 500;
static constexpr int POLL_TIMEOUT_EXPECTING = 50;
static constexpr int READ_FD_INDEX = 0;
static constexpr int WRITE_FD_INDEX = 1;
static constexpr int WAYLAND_FD_INDEX = 0;
static constexpr int PIPE_FD_INDEX = 1;
static constexpr size_t MAX_CLIPBOARD_HISTORY_SIZE = 25;

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

    if (!setup_pipe())
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

        if (offer)
        {
            if (waited || (fds[PIPE_FD_INDEX].revents & POLLIN))
            {
                process_clipboard_data(fds[PIPE_FD_INDEX].revents & POLLIN);
            }
            if (offer->has_mime_types() && !(fds[PIPE_FD_INDEX].revents & POLLIN))
            {
                waited = true;
            }
        }

        // If we have MIME types to process and there is nothing

        wl_display_flush(connection.get_display());
    }

    return 0;
}

void WaylandClipboard::setup_polling(struct pollfd fds[POLL_FD_COUNT])
{
    fds[WAYLAND_FD_INDEX].fd = wl_display_get_fd(connection.get_display());
    fds[WAYLAND_FD_INDEX].events = POLLIN;
    fds[PIPE_FD_INDEX].fd = pipe_fds[READ_FD_INDEX];
    fds[PIPE_FD_INDEX].events = POLLIN;
}

bool WaylandClipboard::handle_wayland_events(struct pollfd fds[POLL_FD_COUNT])
{
    while (wl_display_prepare_read_queue(connection.get_display(), connection.get_event_queue()) != 0)
    {
        wl_display_dispatch_queue(connection.get_display(), connection.get_event_queue());
    }

    int timeout = (offer && offer->has_mime_types()) ? POLL_TIMEOUT_EXPECTING : POLL_TIMEOUT_IDLE;
    int ret = poll(fds, POLL_FD_COUNT, timeout);
    if (ret < 0)
    {
        perror("poll");
        throw std::runtime_error("Failed to poll Wayland events");
    }

    if (fds[WAYLAND_FD_INDEX].revents & POLLIN)
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
    char buf[BUFFER_SIZE];
    std::string content = "";
    bool more_data = has_pipe_data;

    while (more_data)
    {
        ssize_t n = read(pipe_fds[READ_FD_INDEX], buf, sizeof(buf));
        if (n < 0)
        {
            break;
        }
        content.append(buf, n);
        if (n != BUFFER_SIZE)
        {
            more_data = false; // No more data available
        }
    }

    if ((!has_pipe_data || !content.empty()) && offer && offer->has_mime_types())
    {
        if (content.size() > BUFFER_SIZE)
        {
            content = content.substr(0, BUFFER_SIZE);
        }
        trim(content);
        clipboard_history.front()[offer->pop_mime_type()] = content;

        if (offer->has_mime_types())
        {
            offer->read_next_mime(pipe_fds[WRITE_FD_INDEX]);
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
    // Check if we already have this entry (important on startup)
    if (clipboard_history.size() > 1)
    {
        auto first = clipboard_history.front();
        auto second = *std::next(clipboard_history.begin());
        for (const auto &[key, value] : second)
        {
            if (copied)
            {
                if (first == second)
                {
                    // If the first and second entries are the same, we can skip this check
                    std::cerr << "Skipping duplicate entry in clipboard history" << std::endl;
                    clipboard_history.pop_front();
                    break;
                }
            }
            else
            {
                if (value != "" && first.find(key) != first.end() && first[key] == value)
                {
                    clipboard_history.erase(std::next(clipboard_history.begin()));
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
    // Load clipboard_history from $XDG_DATA_HOME/clipboard_history.json
    nlohmann::json json_data;
    std::string data_home = getenv("XDG_DATA_HOME") ? getenv("XDG_DATA_HOME") : std::string(getenv("HOME")) + "/.local/share";
    std::string file_path = data_home + "/clipboard_history.json";
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file for reading: " << file_path << std::endl;
        return;
    }

    file >> json_data;
    clipboard_history = json_data.get<std::list<std::map<std::string, std::string>>>();
    for (auto &entry : clipboard_history)
    {
        for (auto &[mime, data] : entry)
        {
            data = base64_decode(data);
        }
    }
}

void WaylandClipboard::save_clipboard_data()
{
    // Save clipboard_history to $XDG_DATA_HOME/clipboard_history.json
    std::string data_home = getenv("XDG_DATA_HOME") ? getenv("XDG_DATA_HOME") : std::string(getenv("HOME")) + "/.local/share";
    std::string file_path = data_home + "/clipboard_history.json";
    std::ofstream file(file_path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file for writing: " << file_path << std::endl;
        return;
    }

    auto encoded_history = clipboard_history;
    for (auto &entry : encoded_history)
    {
        for (auto &[mime, data] : entry)
        {
            data = base64_encode(data);
        }
    }
    nlohmann::json json_data = encoded_history;
    file << json_data.dump(4);
}

void WaylandClipboard::handle_selection(std::shared_ptr<Offer> offer)
{
    clipboard_history.push_front(std::map<std::string, std::string>());
    if (clipboard_history.size() > MAX_CLIPBOARD_HISTORY_SIZE)
    {
        clipboard_history.pop_back(); // Limit history size to 25 entries
    }

    if (offer->has_mime_types())
    {
        this->offer = offer;
        offer->read_next_mime(pipe_fds[WRITE_FD_INDEX]);

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
    if (pipe_fds[READ_FD_INDEX] != -1)
    {
        close(pipe_fds[READ_FD_INDEX]);
        pipe_fds[READ_FD_INDEX] = -1;
    }
    if (pipe_fds[WRITE_FD_INDEX] != -1)
    {
        close(pipe_fds[WRITE_FD_INDEX]);
        pipe_fds[WRITE_FD_INDEX] = -1;
    }
}