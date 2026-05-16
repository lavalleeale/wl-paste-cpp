#pragma once

#include <cstddef>
#include <string>

namespace clipboard
{
void ltrim(std::string &s);
void rtrim(std::string &s);
void trim(std::string &s);
std::string single_line_preview(const std::string &s, std::size_t max_size = 200);
}
