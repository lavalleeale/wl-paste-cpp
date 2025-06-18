#include "Offer.h"

void Offer::add_mime_type(const std::string &mime_type)
{
    mime_types.push(mime_type);
}

std::string Offer::pop_mime_type()
{
    if (mime_types.empty())
    {
        return std::string();
    }
    std::string mime_type = mime_types.front();
    mime_types.pop();
    return mime_type;
}

void Offer::read_next_mime(int fd)
{
    zwlr_data_control_offer_v1_receive(offer, mime_types.front().c_str(), fd);
}
