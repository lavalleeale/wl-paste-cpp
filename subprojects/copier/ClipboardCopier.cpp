#include "ClipboardCopier.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sys/wait.h>
#include "utils.h"

ClipboardCopier::ClipboardCopier(const std::string &command)
{
    load_clipboard_data();
    if (command == "")
    {
        clipboard_data = clipboard_history.front();
    }
    else
    {
        // run the command with the clipboard history as stdin and save the output
        clipboard_data.clear();
        std::vector<std::string> options;
        for (const auto &entry : clipboard_history)
        {
            if (entry.find("text/plain") != entry.end())
            {
                std::string option = entry.at("text/plain");
                if (option.length() == 0)
                {
                    continue; // Skip empty options
                }
                options.push_back(option);
            }
        }
        // Execute the command and capture its output
        int to_child_pipe[2];
        int from_child_pipe[2];

        if (pipe(to_child_pipe) == -1 || pipe(from_child_pipe) == -1)
        {
            std::cerr << "Failed to create pipes" << std::endl;
            return;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            std::cerr << "Failed to fork" << std::endl;
            return;
        }

        if (pid == 0)
        {
            // Child process
            // Redirect stdin
            dup2(to_child_pipe[0], STDIN_FILENO);
            close(to_child_pipe[0]);
            close(to_child_pipe[1]);

            // Redirect stdout
            dup2(from_child_pipe[1], STDOUT_FILENO);
            close(from_child_pipe[0]);
            close(from_child_pipe[1]);

            execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
            // If execl returns, it failed
            std::cerr << "Failed to execute command: " << command << std::endl;
            exit(1);
        }
        else
        {
            // Parent process
            close(to_child_pipe[0]);
            close(from_child_pipe[1]);

            // Write options to child stdin
            for (const auto &option : options)
            {
                ssize_t bytes_written = write(to_child_pipe[1], option.c_str(), option.length());
                if (bytes_written == -1)
                {
                    std::cerr << "Failed to write to pipe" << std::endl;
                }
                if (write(to_child_pipe[1], "\n", 1) == -1)
                {
                    std::cerr << "Failed to write newline to pipe" << std::endl;
                }
            }
            close(to_child_pipe[1]);

            // Read from child stdout
            char buffer[1024];
            std::string choice = "";
            ssize_t count;
            do
            {
                count = read(from_child_pipe[0], buffer, sizeof(buffer));
                if (count > 0)
                {
                    choice.append(buffer, count);
                }
            } while (count > 0);
            trim(choice);
            close(from_child_pipe[0]);

            int status;
            waitpid(pid, &status, 0);
            for (const auto &entry : clipboard_history)
            {
                if (entry.find("text/plain") != entry.end() && entry.at("text/plain") == choice)
                {
                    clipboard_data = entry;
                    break; // Found the matching entry
                }
            }
        }
    }
}

int ClipboardCopier::run()
{
    if (!init())
    {
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
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display); // Wait for registry events

    if (!seat || !data_control_manager)
    {
        std::cerr << "Failed to get seat or data control manager." << std::endl;
        return false;
    }

    data_control_device = zwlr_data_control_manager_v1_get_data_device(data_control_manager, seat);
    data_source = zwlr_data_control_manager_v1_create_data_source(data_control_manager);
    zwlr_data_control_source_v1_add_listener(data_source, &data_source_listener, this);
    for (const auto &entry : clipboard_data)
    {
        zwlr_data_control_source_v1_offer(data_source, entry.first.c_str());
    }
    zwlr_data_control_device_v1_set_selection(data_control_device, data_source);
    wl_display_flush(display);

    return true;
}

void ClipboardCopier::cleanup()
{
    if (data_source)
    {
        zwlr_data_control_source_v1_destroy(data_source);
    }
    if (data_control_device)
    {
        zwlr_data_control_device_v1_destroy(data_control_device);
    }
    if (display)
    {
        wl_display_disconnect(display);
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
    ClipboardCopier *self = static_cast<ClipboardCopier *>(data);
    for (const auto &entry : self->clipboard_data)
    {
        if (strcmp(entry.first.c_str(), mime) == 0)
        {
            ssize_t bytes_written = write(fd, entry.second.c_str(), entry.second.length());
            if (bytes_written == -1)
            {
                std::cerr << "Failed to write to fd" << std::endl;
            }
            break;
        }
    }
    close(fd);
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
    clipboard_history = json_data.get<std::vector<std::map<std::string, std::string>>>();
}