#include "aiws/ipc_protocol.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

    void write_length(char *bytes, std::uint32_t length)
    {
        bytes[0] = static_cast<char>((length >> 24) & 0xffU);
        bytes[1] = static_cast<char>((length >> 16) & 0xffU);
        bytes[2] = static_cast<char>((length >> 8) & 0xffU);
        bytes[3] = static_cast<char>(length & 0xffU);
    }

} // namespace

int main(int argc, char *argv[])
{
    std::string socket_path = aiws::ipc::default_socket_path();
    int argument_index = 1;

    if (argument_index + 1 < argc &&
        std::string(argv[argument_index]) == "--socket")
    {
        socket_path = argv[argument_index + 1];
        argument_index += 2;
    }

    if (argument_index + 2 != argc ||
        std::string(argv[argument_index]) != "add")
    {
        std::cerr << "usage: aiws_send [--socket PATH] add FILE\n";
        return 2;
    }

    std::string file_path;
    try
    {
        file_path = std::filesystem::absolute(
            std::filesystem::path(argv[argument_index + 1])).string();
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 2;
    }

    if (file_path.empty() || file_path.size() > aiws::ipc::maxPathSize)
    {
        std::cerr << "file path is too long\n";
        return 1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return 2;
    }

    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_un address{};
    if (socket_path.size() >= sizeof(address.sun_path))
    {
        close(fd);
        return 2;
    }

    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);

    if (connect(fd,
                reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) < 0)
    {
        close(fd);
        return 2;
    }

    char header[aiws::ipc::headerSize];
    std::memcpy(header, aiws::ipc::magic, aiws::ipc::magicSize);
    write_length(header + aiws::ipc::magicSize,
                 static_cast<std::uint32_t>(file_path.size()));

    if (send(fd, header, sizeof(header), 0) < 0 ||
        send(fd, file_path.data(), file_path.size(), 0) < 0)
    {
        close(fd);
        return 2;
    }

    std::uint8_t reply = 0xff;
    if (recv(fd, &reply, sizeof(reply), 0) != static_cast<ssize_t>(sizeof(reply)))
    {
        close(fd);
        return 2;
    }

    close(fd);

    if (reply == static_cast<std::uint8_t>(aiws::ipc::ReplyStatus::Accepted))
    {
        return 0;
    }

    if (reply == static_cast<std::uint8_t>(aiws::ipc::ReplyStatus::Rejected))
    {
        std::cerr << "application rejected the document\n";
        return 1;
    }

    return 2;
}
