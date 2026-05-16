#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace clipboard
{
using ClipboardEntry = std::map<std::string, std::string>;
using ClipboardHistory = std::vector<ClipboardEntry>;

constexpr std::size_t max_history_size = 25;

std::filesystem::path history_path();
ClipboardHistory load_history();
bool save_history(const ClipboardHistory &history);
void trim_history(ClipboardHistory &history);
}
