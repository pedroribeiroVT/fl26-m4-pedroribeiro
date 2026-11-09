#include "aiws/ipc_protocol.hpp"

#include <cstdlib>
#include <string>

#include <unistd.h>

namespace aiws::ipc
{

    std::string default_socket_path()
    {
        const char *user = std::getenv("USER");
        if (user != nullptr && *user != '\0')
        {
            return std::string("/tmp/aiws-") + user + ".sock";
        }
        return "/tmp/aiws-" +
               std::to_string(static_cast<unsigned long long>(getuid())) +
               ".sock";
    }

} // namespace aiws::ipc
