#include "aiws/gui/context_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {
constexpr int kMinimumBudget = 50;
constexpr int kMaximumBudget = 1000;
constexpr int kBudgetStep = 50;
constexpr int kInitialBudget = 300;
}  // namespace

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);

    context_budget_slider_ = new QSlider(Qt::Horizontal, frame);
    context_budget_slider_->setRange(kMinimumBudget, kMaximumBudget);
    context_budget_slider_->setSingleStep(kBudgetStep);
    context_budget_slider_->setPageStep(kBudgetStep);
    budget_value_label_ = new QLabel(frame);
    build_context_button_ = new QPushButton("Build Context", frame);
    context_text_ = new QPlainTextEdit(frame);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    auto *title_label = new QLabel("Context:", frame);
    title_label->setStyleSheet("font-weight: bold;");

    auto *budget_row = new QHBoxLayout;
    budget_row->addWidget(new QLabel("Token budget:", frame));
    budget_row->addWidget(context_budget_slider_, 1);
    budget_row->addWidget(budget_value_label_);
    budget_row->addWidget(build_context_button_);

    auto *frame_layout = new QVBoxLayout(frame);
    frame_layout->addWidget(title_label);
    frame_layout->addLayout(budget_row);
    frame_layout->addWidget(context_text_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(frame);

    connect(context_budget_slider_, &QSlider::valueChanged, this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked, this, &ContextWidget::request_context);

    reset();
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText("No context items were returned.");
        return;
    }

    QStringList blocks;
    for (const ContextItem& item : items) {
        blocks.append(QString("Document: %1\nChunk: %2\nTokens: %3\nScore: %4\nTruncated: %5\nText: %6")
                          .arg(QString::fromStdString(item.document_id))
                          .arg(static_cast<qulonglong>(item.chunk_sequence))
                          .arg(static_cast<qulonglong>(item.token_count))
                          .arg(item.score)
                          .arg(item.truncated ? QString("yes") : QString("no"))
                          .arg(QString::fromStdString(item.text)));
    }
    context_text_->setPlainText(blocks.join("\n\n"));
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(kInitialBudget);
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    const int snapped = (value + kBudgetStep / 2) / kBudgetStep * kBudgetStep;
    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
