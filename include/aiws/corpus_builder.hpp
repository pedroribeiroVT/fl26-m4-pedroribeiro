#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace aiws {

struct BuildResult {
    std::uint64_t build_id{0};
    std::unique_ptr<ProcessingCore> core;
    std::string error;
};

class CorpusBuilder {
public:
    CorpusBuilder() = default;
    ~CorpusBuilder();

    CorpusBuilder(const CorpusBuilder&) = delete;
    CorpusBuilder& operator=(const CorpusBuilder&) = delete;
    CorpusBuilder(CorpusBuilder&&) = delete;
    CorpusBuilder& operator=(CorpusBuilder&&) = delete;

    void start(std::unique_ptr<ProcessingCore> core,
               Workspace workspace,
               int delay_ms_per_document,
               std::uint64_t build_id);
    bool running() const noexcept;
    bool result_ready() const;
    BuildResult take();
    void cancel() noexcept;

private:
    std::thread thread_;
    std::future<BuildResult> result_;
    std::atomic<bool> cancel_{false};
};

}  // namespace aiws
