#pragma once

#include "aiws/ipc_protocol.hpp"

#include <string>

namespace aiws::ipc
{

    class IpcClient final
    {
    public:
        ReplyStatus send_add_request(const std::string &socket_path,
                                     const std::string &file_path,
                                     std::string &error_message) const;
    };

} // namespace aiws::ipc
