#include "aiws/ipc_server.hpp"

#include "aiws/ipc_protocol.hpp"

#include <QString>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace aiws::ipc
{
    namespace
    {

        bool receive_bytes(int fd,
                           void *data,
                           std::size_t size,
                           const std::atomic_bool &stop_requested)
        {
            auto *bytes = static_cast<char *>(data);
            std::size_t received = 0;

            while (received < size)
            {
                if (stop_requested.load())
                {
                    return false;
                }

                pollfd descriptor{fd, POLLIN, 0};
                const int ready = poll(&descriptor, 1, 100);
                if (ready < 0)
                {
                    return false;
                }
                if (ready == 0)
                {
                    continue;
                }

                const ssize_t count = recv(fd, bytes + received, size - received, 0);
                if (count <= 0)
                {
                    return false;
                }
                received += static_cast<std::size_t>(count);
            }

            return true;
        }

        std::uint32_t read_length(const char *bytes)
        {
            return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) << 24) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 8) |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
        }

    } // namespace

    IpcServer::IpcServer(QObject *parent) : QObject(parent) {}

    IpcServer::~IpcServer()
    {
        stop();
    }

    bool IpcServer::start(const std::string &socket_path)
    {
        stop();

        sockaddr_un address{};
        if (socket_path.empty() || socket_path.size() >= sizeof(address.sun_path))
        {
            return false;
        }

        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            return false;
        }

        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);

        ::unlink(socket_path.c_str());
        if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0 ||
            ::listen(fd, 8) < 0)
        {
            ::close(fd);
            ::unlink(socket_path.c_str());
            return false;
        }

        listen_fd_ = fd;
        socket_path_ = socket_path;
        stop_requested_.store(false);
        server_thread_ = std::thread(&IpcServer::run, this);
        return true;
    }

    void IpcServer::stop()
    {
        stop_requested_.store(true);

        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
    }

    void IpcServer::run()
    {
        while (!stop_requested_.load())
        {
            pollfd descriptor{listen_fd_, POLLIN, 0};
            const int ready = poll(&descriptor, 1, 100);
            if (ready < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            if (ready == 0)
            {
                continue;
            }

            const int client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }

            char header[headerSize];
            bool valid = receive_bytes(client_fd, header, sizeof(header), stop_requested_);
            std::string path;

            if (valid && std::memcmp(header, magic, magicSize) == 0)
            {
                const std::uint32_t path_size = read_length(header + magicSize);
                if (path_size == 0 || path_size > maxPathSize)
                {
                    valid = false;
                }
                else
                {
                    path.resize(path_size);
                    valid = receive_bytes(client_fd, path.data(), path.size(), stop_requested_);
                }
            }
            else
            {
                valid = false;
            }

            if (valid)
            {
                std::ifstream file(path, std::ios::binary);
                valid = file.good();
            }

            if (valid)
            {
                const auto reply = static_cast<std::uint8_t>(ReplyStatus::Accepted);
                send(client_fd, &reply, sizeof(reply), 0);
                emit document_received(QString::fromUtf8(
                    path.data(), static_cast<int>(path.size())));
            }
            else
            {
                const auto reply = static_cast<std::uint8_t>(ReplyStatus::Rejected);
                send(client_fd, &reply, sizeof(reply), 0);
            }

            close(client_fd);
        }

        if (listen_fd_ >= 0)
        {
            close(listen_fd_);
            listen_fd_ = -1;
        }

        if (!socket_path_.empty())
        {
            unlink(socket_path_.c_str());
        }
    }

} // namespace aiws::ipc
