#pragma once
#include <wlr-data-control-unstable-v1-client-protocol.h>
#include <string>
#include <queue>
#include <list>
#include <map>

class Offer
{
public:
    Offer(zwlr_data_control_offer_v1 *offer) : offer(offer) {}
    ~Offer() { zwlr_data_control_offer_v1_destroy(offer); }

    void add_mime_type(const std::string &mime_type);
    bool matches(zwlr_data_control_offer_v1 *other_offer) const { return offer == other_offer; }
    bool has_mime_types() const { return !mime_types.empty(); }
    std::string pop_mime_type();
    void read_next_mime(int fd);

private:
    std::queue<std::string> mime_types = std::queue<std::string>();
    zwlr_data_control_offer_v1 *offer = nullptr;
};