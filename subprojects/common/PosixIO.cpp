#include "PosixIO.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace clipboard
{
UniqueFd::~UniqueFd()
{
    reset();
}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : fd_(other.release()) {}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept
{
    if (this != &other)
    {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::release()
{
    int fd = fd_;
    fd_ = -1;
    return fd;
}

void UniqueFd::reset(int fd)
{
    if (fd_ >= 0)
    {
        close(fd_);
    }
    fd_ = fd;
}

bool set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0)
    {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool write_all(int fd, const char *data, std::size_t size)
{
    std::size_t written = 0;
    while (written < size)
    {
        ssize_t n = write(fd, data + written, size - written);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (n == 0)
        {
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool write_all(int fd, const std::string &data)
{
    return write_all(fd, data.data(), data.size());
}
}
