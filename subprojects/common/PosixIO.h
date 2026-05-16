#pragma once

#include <cstddef>
#include <string>

namespace clipboard
{
class UniqueFd
{
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd();

    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;

    UniqueFd(UniqueFd &&other) noexcept;
    UniqueFd &operator=(UniqueFd &&other) noexcept;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release();
    void reset(int fd = -1);

private:
    int fd_ = -1;
};

bool set_nonblocking(int fd);
bool write_all(int fd, const char *data, std::size_t size);
bool write_all(int fd, const std::string &data);
}
