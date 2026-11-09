#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace aiws::ipc
{

    // format: 4 bytes for magic number, 4 bytes for length, N bytes for path
    constexpr char magic[] = {'6', '7', '6', '7'};
    constexpr std::size_t magicSize = sizeof(magic);

    enum class ReplyStatus : std::uint8_t
    {
        Accepted = 0,
        Rejected = 1,
    };

    constexpr std::size_t headerSize = 8;
    constexpr std::size_t replyHeaderSize = 1;
    constexpr std::uint32_t maxPathSize = 1024 * 1024;

    // returns /tmp/aiws-$USER.sock, or the numeric-UID fallback
    std::string default_socket_path();

} // namespace aiws::ipc
