#include "utils.h"
#include <string>
#include <algorithm>
#include <ranges>

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