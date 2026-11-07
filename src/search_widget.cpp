#include "aiws/gui/search_widget.hpp"

#include "aiws/gui/search_result_model.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndexList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {

namespace {
QLabel* make_title_label(const QString& text, QWidget* parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet("font-weight: bold;");
    return label;
}

int selected_row(const QTableView* view) {
    const QModelIndexList rows = view->selectionModel()->selectedRows();
    if (rows.isEmpty()) return -1;
    return rows.first().row();
}
}  // namespace

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);

    query_edit_ = new QLineEdit(frame);
    query_edit_->setPlaceholderText("Search the current corpus");
    result_count_spin_ = new QSpinBox(frame);
    result_count_spin_->setRange(1, 100);
    search_button_ = new QPushButton("Search", frame);

    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(frame);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setWordWrap(false);
    results_view_->verticalHeader()->hide();
    results_view_->horizontalHeader()->setStretchLastSection(true);

    view_source_button_ = new QPushButton("View Source", frame);
    selected_result_text_ = new QPlainTextEdit(frame);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to inspect its details.");

    auto *query_row = new QHBoxLayout;
    query_row->addWidget(make_title_label("Query:", frame));
    query_row->addWidget(query_edit_, 1);
    query_row->addWidget(new QLabel("Results:", frame));
    query_row->addWidget(result_count_spin_);
    query_row->addWidget(search_button_);

    auto *selected_row_header = new QHBoxLayout;
    selected_row_header->addWidget(make_title_label("Selected Result:", frame));
    selected_row_header->addStretch();
    selected_row_header->addWidget(view_source_button_);

    auto *frame_layout = new QVBoxLayout(frame);
    frame_layout->addLayout(query_row);
    frame_layout->addWidget(results_view_, 3);
    frame_layout->addLayout(selected_row_header);
    frame_layout->addWidget(selected_result_text_, 2);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(frame);

    connect(search_button_, &QPushButton::clicked, this, &SearchWidget::request_search);
    connect(query_edit_, &QLineEdit::textChanged, this, &SearchWidget::query_changed);
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);
    connect(view_source_button_, &QPushButton::clicked, this, &SearchWidget::request_source_document);

    reset();
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    clear_results();
    result_model_->set_results(std::move(results));
}

void SearchWidget::clear_results() {
    result_model_->clear();
    results_view_->clearSelection();
    show_selected_result();
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const SearchResult* result = result_model_->result_at(selected_row(results_view_));
    if (!result) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    const QString details = QString("Document: %1\nChunk ID: %2\nChunk: %3\nScore: %4\nMatched terms: %5\n\n%6")
                                .arg(QString::fromStdString(result->document_id),
                                     QString::fromStdString(result->chunk_id))
                                .arg(static_cast<qulonglong>(result->chunk_sequence))
                                .arg(result->score)
                                .arg(static_cast<qulonglong>(result->matched_terms))
                                .arg(QString::fromStdString(result->text));
    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    const SearchResult* result = result_model_->result_at(selected_row(results_view_));
    if (!result) return;
    emit source_document_requested(QString::fromStdString(result->document_id));
}

}  // namespace aiws::gui
