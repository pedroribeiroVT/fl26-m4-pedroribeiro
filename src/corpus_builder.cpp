#include "aiws/corpus_builder.hpp"

#include <chrono>
#include <exception>
#include <functional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace aiws {

namespace {

void run_build(std::promise<BuildResult> promise,
               std::unique_ptr<ProcessingCore> core,
               Workspace workspace,
               int delay_ms_per_document,
               std::uint64_t build_id,
               const std::atomic<bool>& cancel) {
    constexpr std::chrono::milliseconds kStep{10};

    BuildResult result;
    result.build_id = build_id;

    try {
        auto remaining = std::chrono::milliseconds{
            static_cast<std::int64_t>(delay_ms_per_document > 0 ? delay_ms_per_document : 0) *
            static_cast<std::int64_t>(workspace.documents().size())};
        while (remaining.count() > 0) {
            if (cancel.load()) {
                promise.set_value(std::move(result));
                return;
            }
            const auto step = remaining < kStep ? remaining : kStep;
            std::this_thread::sleep_for(step);
            remaining -= step;
        }

        if (cancel.load()) {
            promise.set_value(std::move(result));
            return;
        }

        core->rebuild(workspace);
        result.core = std::move(core);
    } catch (const std::exception& e) {
        result.core.reset();
        result.error = e.what();
        if (result.error.empty()) {
            result.error = "corpus build failed";
        }
    } catch (...) {
        result.core.reset();
        result.error = "corpus build failed (unknown error)";
    }

    promise.set_value(std::move(result));
}

}  // namespace

CorpusBuilder::~CorpusBuilder() {
    cancel();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void CorpusBuilder::start(std::unique_ptr<ProcessingCore> core,
                          Workspace workspace,
                          int delay_ms_per_document,
                          std::uint64_t build_id) {
    if (thread_.joinable()) {
        throw std::logic_error("CorpusBuilder::start called while a build is running");
    }

    cancel_.store(false);

    std::promise<BuildResult> promise;
    std::future<BuildResult> future = promise.get_future();

    thread_ = std::thread(run_build,
                          std::move(promise),
                          std::move(core),
                          std::move(workspace),
                          delay_ms_per_document,
                          build_id,
                          std::cref(cancel_));

    result_ = std::move(future);
}

bool CorpusBuilder::running() const noexcept {
    return thread_.joinable();
}

bool CorpusBuilder::result_ready() const {
    return result_.valid() &&
           result_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

BuildResult CorpusBuilder::take() {
    if (!result_.valid()) {
        return {};
    }

    BuildResult result;
    try {
        result = result_.get();
    } catch (const std::future_error& e) {
        result.error = std::string("corpus build failed: ") + e.what();
    }

    if (thread_.joinable()) {
        thread_.join();
    }

#ifdef AIWS_FAULT_LOSE_FAILURE
    if (!result.error.empty()) {
        result.build_id = 0;
        result.error.clear();
    }
#endif

    return result;
}

void CorpusBuilder::cancel() noexcept {
    cancel_.store(true);
}

}  // namespace aiws
