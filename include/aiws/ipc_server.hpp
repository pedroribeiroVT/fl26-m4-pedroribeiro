#pragma once

#include <QObject>

#include <string>
#include <thread>

class QString;

namespace aiws::ipc
{
    // using QObject for allowing signals
    class IpcServer final : public QObject
    {
        Q_OBJECT

    public:
        explicit IpcServer(QObject *parent = nullptr);
        // to join thread
        ~IpcServer() override;

        // start server
        bool start(const std::string &socket_path);

        // stop server
        void stop();

    signals:
        // signal for when document is accepted
        void document_received(const QString &path);

    private:
        void run();

        int listen_fd_{-1};
        std::string socket_path_;
        int wake_fds_[2]{-1, -1};
        std::thread server_thread_;
    };

} // namespace aiws::ipc
