#include "aiws/gui/corpus_state.hpp"

#include <utility>

namespace aiws::gui {

std::unique_ptr<CorpusState> CorpusState::build_requested(MainWindow&) {
    return nullptr;
}

std::unique_ptr<CorpusState> CorpusState::workspace_changed(MainWindow&) {
    return nullptr;
}

std::unique_ptr<CorpusState> CorpusState::reset(MainWindow&) {
    return std::make_unique<EmptyState>();
}

std::unique_ptr<CorpusState> CorpusState::build_finished(MainWindow&, BuildResult) {
    return nullptr;
}

std::unique_ptr<CorpusState> EmptyState::workspace_changed(MainWindow&) {
    return std::make_unique<OutOfDateState>();
}

std::unique_ptr<CorpusState> OutOfDateState::build_requested(MainWindow& window) {
    window.start_build();
    return std::make_unique<BuildingState>();
}

std::unique_ptr<CorpusState> BuildingState::workspace_changed(MainWindow& window) {
    window.cancel_build();
    window.show_out_of_date();
    return std::make_unique<OutOfDateState>();
}

std::unique_ptr<CorpusState> BuildingState::reset(MainWindow& window) {
    window.cancel_build();
    return std::make_unique<EmptyState>();
}

std::unique_ptr<CorpusState> BuildingState::build_finished(MainWindow& window, BuildResult result) {
    if (!result.core) {
        window.report_build_failure(result.error);
        return std::make_unique<OutOfDateState>();
    }
    window.accept_corpus(std::move(result.core));
    return std::make_unique<CurrentState>();
}

std::unique_ptr<CorpusState> CurrentState::build_requested(MainWindow& window) {
    window.start_build();
    return std::make_unique<BuildingState>();
}

std::unique_ptr<CorpusState> CurrentState::workspace_changed(MainWindow& window) {
    window.show_out_of_date();
    return std::make_unique<OutOfDateState>();
}

}  // namespace aiws::gui
