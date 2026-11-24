#pragma once

#include "aiws/corpus_builder.hpp"
#include "aiws/gui/main_window.hpp"

#include <memory>

namespace aiws::gui {

class CorpusState {
public:
    virtual ~CorpusState() = default;

    virtual MainWindow::CorpusStatus status() const = 0;
    virtual bool build_enabled() const = 0;
    virtual bool corpus_usable() const { return false; }

    virtual std::unique_ptr<CorpusState> build_requested(MainWindow& window);
    virtual std::unique_ptr<CorpusState> workspace_changed(MainWindow& window);
    virtual std::unique_ptr<CorpusState> reset(MainWindow& window);
    virtual std::unique_ptr<CorpusState> build_finished(MainWindow& window, BuildResult result);
};

class EmptyState final : public CorpusState {
public:
    MainWindow::CorpusStatus status() const override { return MainWindow::CorpusStatus::NotCurrent; }
    bool build_enabled() const override { return false; }
    std::unique_ptr<CorpusState> workspace_changed(MainWindow& window) override;
};

class OutOfDateState final : public CorpusState {
public:
    MainWindow::CorpusStatus status() const override { return MainWindow::CorpusStatus::NotCurrent; }
    bool build_enabled() const override { return true; }
    std::unique_ptr<CorpusState> build_requested(MainWindow& window) override;
};

class BuildingState final : public CorpusState {
public:
    MainWindow::CorpusStatus status() const override { return MainWindow::CorpusStatus::Building; }
    bool build_enabled() const override { return false; }
    std::unique_ptr<CorpusState> workspace_changed(MainWindow& window) override;
    std::unique_ptr<CorpusState> reset(MainWindow& window) override;
    std::unique_ptr<CorpusState> build_finished(MainWindow& window, BuildResult result) override;
};

class CurrentState final : public CorpusState {
public:
    MainWindow::CorpusStatus status() const override { return MainWindow::CorpusStatus::Current; }
    bool build_enabled() const override { return true; }
    bool corpus_usable() const override { return true; }
    std::unique_ptr<CorpusState> build_requested(MainWindow& window) override;
    std::unique_ptr<CorpusState> workspace_changed(MainWindow& window) override;
};

}  // namespace aiws::gui
