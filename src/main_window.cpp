#include "aiws/gui/main_window.hpp"

#include "aiws/document.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *header = new QFrame(central);
    header->setObjectName("header");
    header->setStyleSheet("#header { background-color: #6b2730; } QLabel { color: white; }");
    auto *badge_label = new QLabel("AI", header);
    badge_label->setAlignment(Qt::AlignCenter);
    badge_label->setFixedSize(40, 40);
    badge_label->setStyleSheet("background-color: #8a3a44; border: 1px solid white; border-radius: 20px; font-weight: bold;");
    auto *title_label = new QLabel("AI Workspace", header);
    title_label->setStyleSheet("font-size: 22px; font-weight: bold;");
    auto *subtitle_label = new QLabel("Document Search and Context Explorer", header);
    subtitle_label->setStyleSheet("font-style: italic;");

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    auto *status_title_label = new QLabel("Current status:", central);
    status_title_label->setStyleSheet("font-weight: bold;");
    status_label_ = new QLabel("Ready", central);
    reset_button_ = new QPushButton("Reset", central);
    reset_button_->setStyleSheet("background-color: #d4a84b; color: #222222; font-weight: bold; padding: 4px 24px;");
    exit_button_ = new QPushButton("Exit", central);
    exit_button_->setStyleSheet("background-color: #4a78c2; color: white; font-weight: bold; padding: 4px 24px;");

    auto *title_column = new QVBoxLayout;
    title_column->addWidget(title_label);
    title_column->addWidget(subtitle_label);

    auto *header_layout = new QHBoxLayout(header);
    header_layout->addWidget(badge_label);
    header_layout->addLayout(title_column);
    header_layout->addStretch();

    auto *tools_row = new QHBoxLayout;
    tools_row->addWidget(term_inspector_widget_, 1);
    tools_row->addWidget(context_widget_, 1);

    auto *status_row = new QHBoxLayout;
    status_row->addWidget(status_title_label);
    status_row->addWidget(status_label_, 1);
    status_row->addWidget(reset_button_);
    status_row->addWidget(exit_button_);

    auto *layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(workspace_widget_, 3);
    layout->addWidget(search_widget_, 4);
    layout->addLayout(tools_row, 2);
    layout->addLayout(status_row);

    connect(workspace_widget_, &WorkspaceWidget::add_document_requested, this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested, this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected, this, &MainWindow::document_selected);
    connect(search_widget_, &SearchWidget::search_requested, this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed, this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested, this, &MainWindow::source_document_requested);
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);
    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested, this, &MainWindow::build_context_requested);
    connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked, this, &MainWindow::exit_application);

    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(this, "Add Document", QString(),
                                                      "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;

    const QFileInfo file_info(path);
    const std::string document_id = file_info.absoluteFilePath().toStdString();
    const QString title = file_info.fileName();

    for (const Document& document : workspace_.documents()) {
        if (document.id() == document_id) {
            status_label_->setText(title + " is already in the workspace.");
            return;
        }
    }

    QFile file(file_info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        status_label_->setText("Could not open " + title + ".");
        return;
    }
    const std::string text = QString::fromUtf8(file.readAll()).toStdString();

    workspace_.add_document(Document{document_id, title.toStdString(), text});
    workspace_widget_->add_document(title);
    invalidate_corpus();
    status_label_->setText("Added " + title + ". Build Corpus to process the workspace.");
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty()) return;

    try {
        processing_core_.rebuild(workspace_);
    } catch (const std::invalid_argument& error) {
        status_label_->setText(QString("Build failed: ") + error.what());
        update_action_states();
        return;
    }

    corpus_current_ = true;
    workspace_widget_->set_corpus_status(QString("%1 chunks").arg(static_cast<qulonglong>(processing_core_.chunk_count())));
    clear_processing_outputs();
    update_action_states();
    status_label_->setText("Corpus built.");
}

void MainWindow::document_selected(int row) {
    const std::vector<Document>& documents = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }
    const Document& document = documents[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(document.title()),
                                              QString::fromStdString(document.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || !search_widget_->has_query()) return;

    std::vector<SearchResult> results = processing_core_.search(query.toStdString(), k);
    const std::size_t result_total = results.size();
    search_widget_->set_results(std::move(results));
    context_widget_->clear_context();
    update_action_states();
    status_label_->setText(QString("Search returned %1 result(s).").arg(static_cast<qulonglong>(result_total)));
}

void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query()) return;

    const std::vector<ContextItem> items = processing_core_.build_context(
        search_widget_->query().toStdString(), search_widget_->result_count(),
        static_cast<std::size_t>(token_budget));
    context_widget_->set_context(items);
    status_label_->setText(QString("Built %1 context item(s).").arg(static_cast<qulonglong>(items.size())));
}

void MainWindow::source_document_requested(const QString& document_id) {
    const std::string id = document_id.toStdString();
    const std::vector<Document>& documents = workspace_.documents();
    for (std::size_t row = 0; row < documents.size(); row++) {
        if (documents[row].id() == id) {
            workspace_widget_->select_document(static_cast<int>(row));
            status_label_->setText("Showing source document " + QString::fromStdString(documents[row].title()) + ".");
            return;
        }
    }
    status_label_->setText("Source document not found.");
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    if (!corpus_current_) return;

    const std::string term_text = term.toStdString();
    try {
        const std::size_t document_frequency = processing_core_.document_frequency(term_text);
        if (chunk_id.isEmpty()) {
            term_inspector_widget_->set_statistics(document_frequency, false, 0);
        } else {
            const std::size_t chunk_frequency = processing_core_.term_frequency(term_text, chunk_id.toStdString());
            term_inspector_widget_->set_statistics(document_frequency, true, chunk_frequency);
        }
    } catch (const std::invalid_argument&) {
        term_inspector_widget_->clear_statistics();
        status_label_->setText("Inspect needs a single term.");
        return;
    }
    status_label_->setText("Inspected term \"" + term.trimmed() + "\".");
}

void MainWindow::reset_application() {
    workspace_.clear();
    corpus_current_ = false;

    const std::array<Resettable*, 4> components{workspace_widget_, search_widget_,
                                                term_inspector_widget_, context_widget_};
    for (Resettable* component : components) {
        component->reset();
    }

    update_action_states();
    status_label_->setText("Application reset.");
}

void MainWindow::exit_application() {
    if (exit_callback_) exit_callback_(this);
}

void MainWindow::update_action_states() {
    const bool can_query = corpus_current_ && search_widget_->has_query();
    workspace_widget_->set_build_corpus_enabled(!workspace_.documents().empty());
    search_widget_->set_search_enabled(can_query);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
    context_widget_->set_build_context_enabled(can_query);
}

void MainWindow::invalidate_corpus() {
    if (corpus_current_) workspace_widget_->set_corpus_status("Out of date");
    corpus_current_ = false;
    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
