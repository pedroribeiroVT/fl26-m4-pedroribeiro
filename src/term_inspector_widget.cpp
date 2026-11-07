#include "aiws/gui/term_inspector_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {
const QString kNeutralValue = QString::fromUtf8("—");
}  // namespace

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);

    term_edit_ = new QLineEdit(frame);
    term_edit_->setPlaceholderText("Enter a term");
    inspect_button_ = new QPushButton("Inspect", frame);
    document_frequency_value_ = new QLabel(frame);
    chunk_frequency_value_ = new QLabel(frame);

    auto *title_label = new QLabel("Term Inspector:", frame);
    title_label->setStyleSheet("font-weight: bold;");

    auto *term_row = new QHBoxLayout;
    term_row->addWidget(new QLabel("Term:", frame));
    term_row->addWidget(term_edit_, 1);
    term_row->addWidget(inspect_button_);

    auto *statistics_row = new QHBoxLayout;
    statistics_row->addWidget(new QLabel("Document Frequency:", frame));
    statistics_row->addWidget(document_frequency_value_);
    statistics_row->addSpacing(24);
    statistics_row->addWidget(new QLabel("Selected Chunk TF:", frame));
    statistics_row->addWidget(chunk_frequency_value_);
    statistics_row->addStretch();

    auto *frame_layout = new QVBoxLayout(frame);
    frame_layout->addWidget(title_label);
    frame_layout->addLayout(term_row);
    frame_layout->addLayout(statistics_row);
    frame_layout->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(frame);

    connect(inspect_button_, &QPushButton::clicked, this, &TermInspectorWidget::request_inspection);

    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(document_frequency));
    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(QString::number(chunk_frequency));
    } else {
        chunk_frequency_value_->setText(kNeutralValue);
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText(kNeutralValue);
    chunk_frequency_value_->setText(kNeutralValue);
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text();
    if (term.trimmed().isEmpty()) return;
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
