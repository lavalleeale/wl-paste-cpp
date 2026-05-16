#include "StringUtils.h"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace clipboard
{
void ltrim(std::string &s)
{
    s.erase(s.begin(), std::ranges::find_if(s, [](unsigned char ch)
                                            { return !std::isspace(ch); }));
}

void rtrim(std::string &s)
{
    s.erase(std::ranges::find_if(s | std::views::reverse, [](unsigned char ch)
                                 { return !std::isspace(ch); })
                .base(),
            s.end());
}

void trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}

std::string single_line_preview(const std::string &s, std::size_t max_size)
{
    std::string preview;
    preview.reserve(std::min(s.size(), max_size));
    bool previous_space = false;

    for (unsigned char ch : s)
    {
        if (preview.size() >= max_size)
        {
            preview += "...";
            break;
        }

        if (std::isspace(ch))
        {
            if (!previous_space)
            {
                preview += ' ';
                previous_space = true;
            }
            continue;
        }

        preview += static_cast<char>(ch);
        previous_space = false;
    }

    trim(preview);
    return preview;
}
}
