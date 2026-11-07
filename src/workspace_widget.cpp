#include "aiws/gui/workspace_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {
QLabel* make_title_label(const QString& text, QWidget* parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet("font-weight: bold;");
    return label;
}
}  // namespace

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    auto *workspace_frame = new QFrame(this);
    workspace_frame->setFrameShape(QFrame::StyledPanel);
    auto *document_frame = new QFrame(this);
    document_frame->setFrameShape(QFrame::StyledPanel);

    document_list_ = new QListWidget(workspace_frame);
    add_document_button_ = new QPushButton("Add Document", workspace_frame);
    build_corpus_button_ = new QPushButton("Build Corpus", workspace_frame);
    corpus_status_label_ = new QLabel(workspace_frame);
    corpus_status_label_->setStyleSheet("font-weight: bold;");

    document_title_label_ = new QLabel(document_frame);
    document_title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    document_text_ = new QPlainTextEdit(document_frame);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its contents.");

    auto *workspace_header = new QHBoxLayout;
    workspace_header->addWidget(make_title_label("Workspace", workspace_frame));
    workspace_header->addWidget(add_document_button_);
    workspace_header->addWidget(build_corpus_button_);
    workspace_header->addStretch();

    auto *corpus_row = new QHBoxLayout;
    corpus_row->addWidget(new QLabel("Corpus:", workspace_frame));
    corpus_row->addWidget(corpus_status_label_);
    corpus_row->addStretch();

    auto *workspace_layout = new QVBoxLayout(workspace_frame);
    workspace_layout->addLayout(workspace_header);
    workspace_layout->addWidget(document_list_);
    workspace_layout->addLayout(corpus_row);

    auto *document_header = new QHBoxLayout;
    document_header->addWidget(make_title_label("Selected Document -", document_frame));
    document_header->addWidget(document_title_label_, 1);

    auto *document_layout = new QVBoxLayout(document_frame);
    document_layout->addLayout(document_header);
    document_layout->addWidget(document_text_);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(workspace_frame, 1);
    layout->addWidget(document_frame, 1);

    connect(add_document_button_, &QPushButton::clicked, this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked, this, &WorkspaceWidget::build_corpus_requested);
    connect(document_list_, &QListWidget::currentRowChanged, this, &WorkspaceWidget::document_selected);

    reset();
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    document_list_->setCurrentRow(row);
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->clear();
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status("Not built");
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
