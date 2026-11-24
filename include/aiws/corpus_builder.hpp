#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <QObject>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aiws {

struct BuildResult {
    std::uint64_t generation{0};
    std::unique_ptr<ProcessingCore> core;
    std::string error;
};

class CorpusBuilder final : public QObject {
    Q_OBJECT

public:
    explicit CorpusBuilder(QObject* parent = nullptr);
    ~CorpusBuilder() override;

    CorpusBuilder(const CorpusBuilder&) = delete;
    CorpusBuilder& operator=(const CorpusBuilder&) = delete;
    CorpusBuilder(CorpusBuilder&&) = delete;
    CorpusBuilder& operator=(CorpusBuilder&&) = delete;

    void request(std::unique_ptr<ProcessingCore> core, Workspace workspace, int delay_ms_per_document);
    void cancel();
    std::optional<BuildResult> take();

signals:
    void build_done();

private:
    struct Request {
        std::unique_ptr<ProcessingCore> core;
        Workspace workspace;
        int delay_ms_per_document{0};
        std::uint64_t generation{0};
    };

    void run();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Request> pending_;
    std::optional<BuildResult> finished_;
    std::uint64_t generation_{0};
    bool stop_{false};
    std::thread thread_;
};

}  // namespace aiws
