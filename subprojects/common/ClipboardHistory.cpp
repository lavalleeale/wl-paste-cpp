#include "ClipboardHistory.h"
#include "PosixIO.h"

#include <cstdlib>
#include <cctype>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace clipboard
{
namespace
{
constexpr const char *history_file_name = "clipboard_history.json";
constexpr const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool is_base64(unsigned char c)
{
    return std::isalnum(c) || c == '+' || c == '/';
}

std::string base64_encode(const std::string &input)
{
    int in_len = static_cast<int>(input.size());
    const unsigned char *bytes_to_encode = reinterpret_cast<const unsigned char *>(input.c_str());
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--)
    {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
            {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
        {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; j < i + 1; j++)
        {
            ret += base64_chars[char_array_4[j]];
        }
        while (i++ < 3)
        {
            ret += '=';
        }
    }

    return ret;
}

std::string base64_decode(const std::string &encoded_string)
{
    int in_len = static_cast<int>(encoded_string.size());
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && encoded_string[in_] != '=' && is_base64(encoded_string[in_]))
    {
        char_array_4[i++] = encoded_string[in_];
        in_++;
        if (i == 4)
        {
            for (i = 0; i < 4; i++)
            {
                char_array_4[i] = static_cast<unsigned char>(std::string(base64_chars).find(char_array_4[i]));
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++)
            {
                ret += char_array_3[i];
            }
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 4; j++)
        {
            char_array_4[j] = 0;
        }
        for (j = 0; j < 4; j++)
        {
            char_array_4[j] = static_cast<unsigned char>(std::string(base64_chars).find(char_array_4[j]));
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; j < i - 1; j++)
        {
            ret += char_array_3[j];
        }
    }

    return ret;
}

std::filesystem::path data_home()
{
    if (const char *xdg_data_home = std::getenv("XDG_DATA_HOME"); xdg_data_home && *xdg_data_home)
    {
        return xdg_data_home;
    }
    if (const char *home = std::getenv("HOME"); home && *home)
    {
        return std::filesystem::path(home) / ".local" / "share";
    }
    return {};
}

ClipboardHistory decode_history(const nlohmann::json &json_data)
{
    ClipboardHistory history;
    if (!json_data.is_array())
    {
        return history;
    }

    for (const auto &entry_json : json_data)
    {
        if (!entry_json.is_object())
        {
            continue;
        }

        ClipboardEntry entry;
        for (auto it = entry_json.begin(); it != entry_json.end(); ++it)
        {
            if (!it.value().is_string())
            {
                continue;
            }
            entry[it.key()] = base64_decode(it.value().get<std::string>());
        }
        if (!entry.empty())
        {
            history.push_back(std::move(entry));
        }
    }

    trim_history(history);
    return history;
}

nlohmann::json encode_history(const ClipboardHistory &history)
{
    nlohmann::json json_data = nlohmann::json::array();
    for (const auto &entry : history)
    {
        nlohmann::json encoded_entry = nlohmann::json::object();
        for (const auto &[mime, data] : entry)
        {
            encoded_entry[mime] = base64_encode(data);
        }
        if (!encoded_entry.empty())
        {
            json_data.push_back(std::move(encoded_entry));
        }
    }
    return json_data;
}
}

std::filesystem::path history_path()
{
    const auto dir = data_home();
    if (dir.empty())
    {
        return {};
    }
    return dir / history_file_name;
}

void trim_history(ClipboardHistory &history)
{
    if (history.size() > max_history_size)
    {
        history.resize(max_history_size);
    }
}

ClipboardHistory load_history()
{
    const auto path = history_path();
    if (path.empty())
    {
        std::cerr << "Cannot load clipboard history: XDG_DATA_HOME and HOME are unset" << std::endl;
        return {};
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        return {};
    }

    try
    {
        nlohmann::json json_data;
        file >> json_data;
        return decode_history(json_data);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Ignoring invalid clipboard history at " << path << ": " << e.what() << std::endl;
        return {};
    }
}

bool save_history(const ClipboardHistory &history)
{
    const auto path = history_path();
    if (path.empty())
    {
        std::cerr << "Cannot save clipboard history: XDG_DATA_HOME and HOME are unset" << std::endl;
        return false;
    }

    try
    {
        std::filesystem::create_directories(path.parent_path());
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to create clipboard history directory: " << e.what() << std::endl;
        return false;
    }

    auto tmp_template = path;
    tmp_template += ".tmp.XXXXXX";
    std::string tmp_name = tmp_template.string();
    UniqueFd fd(mkstemp(tmp_name.data()));
    if (!fd.valid())
    {
        perror("mkstemp");
        return false;
    }

    bool ok = true;
    chmod(tmp_name.c_str(), S_IRUSR | S_IWUSR);

    auto bounded_history = history;
    trim_history(bounded_history);
    const auto json_data = encode_history(bounded_history).dump();
    ok = write_all(fd.get(), json_data) && fsync(fd.get()) == 0;

    if (!ok)
    {
        perror("write clipboard history");
    }

    if (fd.valid() && close(fd.release()) != 0)
    {
        perror("close clipboard history");
        ok = false;
    }

    if (!ok)
    {
        std::filesystem::remove(tmp_name);
        std::cerr << "Failed to write clipboard history temp file: " << tmp_name << std::endl;
        return false;
    }

    try
    {
        std::filesystem::rename(tmp_name, path);
        chmod(path.c_str(), S_IRUSR | S_IWUSR);
        UniqueFd dir_fd(open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY));
        if (dir_fd.valid())
        {
            fsync(dir_fd.get());
        }
    }
    catch (const std::exception &e)
    {
        std::filesystem::remove(tmp_name);
        std::cerr << "Failed to replace clipboard history: " << e.what() << std::endl;
        return false;
    }

    return true;
}
}
