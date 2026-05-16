#include "ClipboardHistory.h"
#include "PosixIO.h"
#include "StringUtils.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
std::filesystem::path make_temp_dir()
{
    std::string tmpl = "/tmp/wl-paste-cpp-test.XXXXXX";
    char *path = mkdtemp(tmpl.data());
    assert(path != nullptr);
    return path;
}

void use_data_home(const std::filesystem::path &path)
{
    setenv("XDG_DATA_HOME", path.c_str(), 1);
    unsetenv("HOME");
}

void test_missing_and_invalid_history()
{
    const auto dir = make_temp_dir();
    use_data_home(dir);

    assert(clipboard::load_history().empty());

    std::filesystem::create_directories(dir);
    std::ofstream file(clipboard::history_path());
    file << "{not json";
    file.close();
    assert(clipboard::load_history().empty());

    std::filesystem::remove_all(dir);
}

void test_history_round_trip_and_limit()
{
    const auto dir = make_temp_dir();
    use_data_home(dir);

    clipboard::ClipboardHistory history;
    for (std::size_t i = 0; i < clipboard::max_history_size + 5; ++i)
    {
        history.push_back({{"text/plain", "entry " + std::to_string(i)}});
    }

    assert(clipboard::save_history(history));
    auto loaded = clipboard::load_history();
    assert(loaded.size() == clipboard::max_history_size);
    assert(loaded.front().at("text/plain") == "entry 0");
    assert(loaded.back().at("text/plain") == "entry 24");

    std::filesystem::remove_all(dir);
}

void test_history_preserves_binary_and_whitespace()
{
    const auto dir = make_temp_dir();
    use_data_home(dir);

    const std::string binary_payload{" \0hello\n\t ", 10};
    clipboard::ClipboardHistory history = {
        {{"text/plain", "  keep surrounding whitespace  "},
         {"application/octet-stream", binary_payload}},
    };

    assert(clipboard::save_history(history));
    auto loaded = clipboard::load_history();
    assert(loaded.size() == 1);
    assert(loaded.front().at("text/plain") == "  keep surrounding whitespace  ");
    assert(loaded.front().at("application/octet-stream") == binary_payload);

    std::filesystem::remove_all(dir);
}

void test_home_fallback()
{
    const auto dir = make_temp_dir();
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", dir.c_str(), 1);

    assert(clipboard::history_path() == dir / ".local" / "share" / "clipboard_history.json");

    std::filesystem::remove_all(dir);
}

void test_write_all()
{
    int fds[2] = {-1, -1};
    assert(pipe(fds) == 0);
    const std::string payload = "hello";
    assert(clipboard::write_all(fds[1], payload));
    close(fds[1]);

    char buffer[16] = {};
    const ssize_t n = read(fds[0], buffer, sizeof(buffer));
    close(fds[0]);
    assert(n == static_cast<ssize_t>(payload.size()));
    assert(std::string(buffer, static_cast<std::size_t>(n)) == payload);
}

void test_single_line_preview()
{
    auto preview = clipboard::single_line_preview("  one\n\t two  ");
    assert(preview == "one two");

    preview = clipboard::single_line_preview("abcdef", 3);
    assert(preview == "abc...");
}
}

int main()
{
    test_missing_and_invalid_history();
    test_history_round_trip_and_limit();
    test_history_preserves_binary_and_whitespace();
    test_home_fallback();
    test_write_all();
    test_single_line_preview();
    return 0;
}
