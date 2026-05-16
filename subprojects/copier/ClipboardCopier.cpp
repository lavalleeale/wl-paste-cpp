#include "ClipboardCopier.h"
#include "PosixIO.h"
#include "StringUtils.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/wait.h>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <format>
#include <poll.h>

namespace
{
constexpr std::size_t max_picker_output_size = 1024 * 1024;

bool set_nonblocking_or_log(int fd)
{
    if (!clipboard::set_nonblocking(fd))
    {
        perror("fcntl");
        return false;
    }
    return true;
}

bool run_picker_command(const std::string &command, const std::vector<std::string> &options, std::string &choice)
{
    int to_child_pipe[2] = {-1, -1};
    int from_child_pipe[2] = {-1, -1};

    if (pipe(to_child_pipe) == -1 || pipe(from_child_pipe) == -1)
    {
        perror("pipe");
        clipboard::UniqueFd to_child_read(to_child_pipe[0]);
        clipboard::UniqueFd to_child_write(to_child_pipe[1]);
        clipboard::UniqueFd from_child_read(from_child_pipe[0]);
        clipboard::UniqueFd from_child_write(from_child_pipe[1]);
        return false;
    }

    clipboard::UniqueFd to_child_read(to_child_pipe[0]);
    clipboard::UniqueFd to_child_write(to_child_pipe[1]);
    clipboard::UniqueFd from_child_read(from_child_pipe[0]);
    clipboard::UniqueFd from_child_write(from_child_pipe[1]);

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return false;
    }

    if (pid == 0)
    {
        dup2(to_child_read.get(), STDIN_FILENO);
        dup2(from_child_write.get(), STDOUT_FILENO);
        to_child_read.reset();
        to_child_write.reset();
        from_child_read.reset();
        from_child_write.reset();
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    to_child_read.reset();
    from_child_write.reset();

    if (!set_nonblocking_or_log(to_child_write.get()) || !set_nonblocking_or_log(from_child_read.get()))
    {
        int status = 0;
        waitpid(pid, &status, 0);
        return false;
    }

    std::string input;
    for (const auto &option : options)
    {
        input += option;
        input += '\n';
    }

    std::size_t input_offset = 0;
    bool read_open = true;
    char buffer[1024];

    while (read_open || to_child_write.valid())
    {
        struct pollfd fds[2] = {
            {.fd = from_child_read.get(), .events = static_cast<short>(read_open ? POLLIN : 0), .revents = 0},
            {.fd = to_child_write.get(), .events = static_cast<short>(to_child_write.valid() ? POLLOUT : 0), .revents = 0},
        };

        if (poll(fds, 2, -1) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("poll");
            break;
        }

        if (to_child_write.valid() && (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            to_child_write.reset();
        }

        if (to_child_write.valid() && (fds[1].revents & POLLOUT))
        {
            const char *data = input.data() + input_offset;
            const std::size_t remaining = input.size() - input_offset;
            ssize_t n = remaining == 0 ? 0 : write(to_child_write.get(), data, remaining);
            if (n > 0)
            {
                input_offset += static_cast<std::size_t>(n);
            }
            else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                to_child_write.reset();
            }
            if (input_offset == input.size())
            {
                to_child_write.reset();
            }
        }

        if (read_open && (fds[0].revents & (POLLERR | POLLNVAL)))
        {
            read_open = false;
            from_child_read.reset();
        }

        if (read_open && (fds[0].revents & (POLLIN | POLLHUP)))
        {
            while (true)
            {
                ssize_t n = read(from_child_read.get(), buffer, sizeof(buffer));
                if (n > 0)
                {
                    const auto remaining = max_picker_output_size - choice.size();
                    choice.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(n), remaining));
                }
                else if (n == 0)
                {
                    read_open = false;
                    from_child_read.reset();
                    break;
                }
                else if (errno == EINTR)
                {
                    continue;
                }
                else if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                else
                {
                    perror("read");
                    read_open = false;
                    from_child_read.reset();
                    break;
                }
            }
        }
    }

    to_child_write.reset();
    from_child_read.reset();

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        perror("waitpid");
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        std::cerr << "Picker command failed" << std::endl;
        return false;
    }

    clipboard::trim(choice);
    return true;
}

std::string picker_label(std::size_t index, const clipboard::ClipboardEntry &entry)
{
    const auto text = entry.find("text/plain");
    if (text != entry.end())
    {
        auto preview = clipboard::single_line_preview(text->second);
        if (!preview.empty())
        {
            return std::format("{}: {}", index + 1, preview);
        }
    }
    return std::format("{}: Non-text Clipboard Entry", index + 1);
}
}

ClipboardCopier::ClipboardCopier(const std::string &command)
{
    load_clipboard_data();
    choose_clipboard_data(command);
}

bool ClipboardCopier::choose_clipboard_data(const std::string &command)
{
    if (clipboard_history.empty())
    {
        std::cerr << "Clipboard history is empty" << std::endl;
        return false;
    }

    if (command == "")
    {
        clipboard_data = clipboard_history.front();
        return !clipboard_data.empty();
    }

    clipboard_data.clear();
    std::vector<std::string> options;
    std::vector<std::size_t> option_indexes;
    for (std::size_t i = 0; i < clipboard_history.size(); ++i)
    {
        const auto &entry = clipboard_history[i];
        if (!entry.empty())
        {
            options.push_back(picker_label(i, entry));
            option_indexes.push_back(i);
        }
    }

    if (options.empty())
    {
        std::cerr << "Clipboard history has no selectable entries" << std::endl;
        return false;
    }

    std::string choice;
    if (!run_picker_command(command, options, choice))
    {
        return false;
    }

    for (std::size_t i = 0; i < options.size(); ++i)
    {
        if (choice == options[i])
        {
            clipboard_data = clipboard_history[option_indexes[i]];
            return true;
        }
    }

    std::cerr << "Picker selection did not match clipboard history" << std::endl;
    return false;
}

int ClipboardCopier::run()
{
    if (clipboard_data.empty())
    {
        return 1;
    }
    if (!init())
    {
        cleanup();
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        std::cerr << "Failed to fork process." << std::endl;
        cleanup();
        return 1;
    }
    else if (pid > 0)
    {
        // Parent process
        return 0; // Exit parent process
    }

    while (running && wl_display_dispatch(display) != -1)
    {
        // Keep dispatching events
    }

    cleanup();
    return 0;
}

bool ClipboardCopier::init()
{
    display = wl_display_connect(nullptr);
    if (!display)
    {
        std::cerr << "Failed to connect to Wayland display." << std::endl;
        return false;
    }

    registry = wl_display_get_registry(display);
    if (!registry)
    {
        std::cerr << "Failed to get Wayland registry." << std::endl;
        return false;
    }
    wl_registry_add_listener(registry, &registry_listener, this);
    if (wl_display_roundtrip(display) < 0)
    {
        std::cerr << "Failed during Wayland registry roundtrip." << std::endl;
        return false;
    }

    if (!seat || !data_control_manager)
    {
        std::cerr << "Failed to get seat or data control manager." << std::endl;
        return false;
    }

    data_control_device = zwlr_data_control_manager_v1_get_data_device(data_control_manager, seat);
    data_source = zwlr_data_control_manager_v1_create_data_source(data_control_manager);
    if (!data_control_device || !data_source)
    {
        std::cerr << "Failed to create Wayland data control objects." << std::endl;
        return false;
    }
    zwlr_data_control_source_v1_add_listener(data_source, &data_source_listener, this);
    for (const auto &entry : clipboard_data)
    {
        zwlr_data_control_source_v1_offer(data_source, entry.first.c_str());
    }
    zwlr_data_control_device_v1_set_selection(data_control_device, data_source);
    if (wl_display_flush(display) < 0)
    {
        std::cerr << "Failed to flush Wayland display." << std::endl;
        return false;
    }

    return true;
}

void ClipboardCopier::cleanup()
{
    if (data_source)
    {
        zwlr_data_control_source_v1_destroy(data_source);
        data_source = nullptr;
    }
    if (data_control_device)
    {
        zwlr_data_control_device_v1_destroy(data_control_device);
        data_control_device = nullptr;
    }
    if (data_control_manager)
    {
        zwlr_data_control_manager_v1_destroy(data_control_manager);
        data_control_manager = nullptr;
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
    if (display)
    {
        wl_display_disconnect(display);
        display = nullptr;
    }
}

void ClipboardCopier::registry_global_s(void *data, struct wl_registry *reg, uint32_t name, const char *interface, uint32_t)
{
    ClipboardCopier *self = static_cast<ClipboardCopier *>(data);
    if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        self->seat = (wl_seat *)wl_registry_bind(reg, name, &wl_seat_interface, 1);
    }
    else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0)
    {
        self->data_control_manager = (zwlr_data_control_manager_v1 *)wl_registry_bind(reg, name, &zwlr_data_control_manager_v1_interface, 1);
    }
}

void ClipboardCopier::registry_global_remove_s(void *, struct wl_registry *, uint32_t) {}

void ClipboardCopier::data_source_send_s(void *data, struct zwlr_data_control_source_v1 *, const char *mime, int32_t fd)
{
    clipboard::UniqueFd output(fd);
    ClipboardCopier *self = static_cast<ClipboardCopier *>(data);
    for (const auto &entry : self->clipboard_data)
    {
        if (strcmp(entry.first.c_str(), mime) == 0)
        {
            if (!clipboard::write_all(output.get(), entry.second))
            {
                std::cerr << "Failed to write to fd" << std::endl;
            }
            break;
        }
    }
}

void ClipboardCopier::data_source_cancelled_s(void *data, struct zwlr_data_control_source_v1 *)
{
    ClipboardCopier *self = static_cast<ClipboardCopier *>(data);
    self->running = false;
    self->data_source = nullptr; // The source is destroyed by the compositor
}

const struct wl_registry_listener ClipboardCopier::registry_listener = {
    .global = registry_global_s,
    .global_remove = registry_global_remove_s,
};

const struct zwlr_data_control_source_v1_listener ClipboardCopier::data_source_listener = {
    .send = data_source_send_s,
    .cancelled = data_source_cancelled_s,
};

void ClipboardCopier::load_clipboard_data()
{
    clipboard_history = clipboard::load_history();
}
