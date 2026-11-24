#include "aiws/corpus_builder.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

namespace aiws {

namespace {

BuildResult build(std::unique_ptr<ProcessingCore> core, const Workspace& workspace, std::uint64_t generation) {
    BuildResult result;
    result.generation = generation;

    try {
        core->rebuild(workspace);
        result.core = std::move(core);
    } catch (const std::exception& e) {
        result.error = e.what();
        if (result.error.empty()) result.error = "corpus build failed";
    } catch (...) {
        result.error = "corpus build failed (unknown error)";
    }
    return result;
}

}  // namespace

CorpusBuilder::CorpusBuilder(QObject* parent) : QObject(parent) {
    thread_ = std::thread(&CorpusBuilder::run, this);
}

CorpusBuilder::~CorpusBuilder() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
#ifndef AIWS_FAULT_SILENT_SHUTDOWN
    cv_.notify_one();
#endif
    if (thread_.joinable()) thread_.join();
}

void CorpusBuilder::request(std::unique_ptr<ProcessingCore> core, Workspace workspace, int delay_ms_per_document) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        pending_ = Request{std::move(core), std::move(workspace), delay_ms_per_document, generation_};
    }
    cv_.notify_one();
}

void CorpusBuilder::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        pending_.reset();
    }
    cv_.notify_one();
}

std::optional<BuildResult> CorpusBuilder::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<BuildResult> result = std::move(finished_);
    finished_.reset();
    if (!result || result->generation != generation_) return std::nullopt;
    return result;
}

void CorpusBuilder::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        cv_.wait(lock, [this] { return stop_ || pending_; });
        if (stop_) return;
        Request request = std::move(*pending_);
        pending_.reset();

        const std::chrono::milliseconds delay{request.delay_ms_per_document > 0 ? request.delay_ms_per_document : 0};
        for (std::size_t i = 0; i < request.workspace.documents().size(); i++) {
            // system_clock: GCC 11's ThreadSanitizer cannot see steady_clock waits release the mutex.
            if (cv_.wait_until(lock, std::chrono::system_clock::now() + delay, [this, &request] {
                    bool unwanted = generation_ != request.generation;
#ifdef AIWS_FAULT_NO_CANCEL
                    unwanted = false;
#endif
                    return stop_ || unwanted;
                })) break;
        }
        if (stop_) return;
        if (request.generation != generation_) continue;

        lock.unlock();
        BuildResult result = build(std::move(request.core), request.workspace, request.generation);
        lock.lock();
        finished_ = std::move(result);
        lock.unlock();
        emit build_done();
        lock.lock();
    }
}

}  // namespace aiws
