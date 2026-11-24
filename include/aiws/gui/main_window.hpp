#pragma once

#include <QMainWindow>

#include "aiws/corpus_builder.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <cstdint>
#include <memory>
#include <string>

class QLabel;
class QPushButton;
class QString;
class QWidget;

namespace aiws::ipc {
class IpcServer;
}  // namespace aiws::ipc

namespace aiws::gui {

class ContextWidget;
class CorpusState;
class SearchWidget;
class TermInspectorWidget;
class WorkspaceWidget;

/**
 * @brief Main application window and coordinator for the M3 AI Workspace GUI.
 *
 * MainWindow owns the application Workspace and ProcessingCore, composes the
 * major GUI components, and coordinates application-wide state changes and
 * backend operations in response to component events.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Function-pointer type used for the application Exit action.
     *
     * The callback receives the main window as a parent for any exit-related
     * interaction. In M3, the registered callback asks the user to confirm the
     * exit. Declining the confirmation leaves the running application and its
     * current state unchanged.
     */
    using ExitCallback = void (*)(QWidget* parent);

    /**
     * @brief Function-pointer type that creates the empty ProcessingCore each build fills.
     *
     * A null factory means a default-constructed ProcessingCore. Tests install a
     * factory whose strategies throw, to exercise the build-failure path (R3).
     */
    using CoreFactory = std::unique_ptr<ProcessingCore> (*)();

    /** @brief Read-only report of the current corpus state, for tests; never used to decide a transition. */
    enum class CorpusStatus { NotCurrent, Building, Current };

    /**
     * @brief Constructs the main AI Workspace application window.
     * @param parent Optional Qt parent widget.
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Stops the IPC server (if any), then stops and joins the build worker.
     *
     * Defined out of line because ipc_server_ holds an incomplete type here.
     */
    ~MainWindow() override;

    /**
     * @brief Registers the callback invoked when the user requests Exit.
     * @param callback Function pointer to invoke for the Exit action. A null
     * callback means that an Exit request has no registered callback to invoke.
     */
    void set_exit_callback(ExitCallback callback) noexcept;

    // ---- M4 required public API (see specification, Section 5) ----------

    /**
     * @brief Adds the file at path exactly as the Add Document action does.
     * @return false, changing nothing, if the file cannot be read.
     */
    bool add_document_file(const QString& path);

    /**
     * @brief Sets the extra build time per document for builds started after this call.
     * @param ms_per_document Delay in milliseconds (the --build-delay-ms value).
     */
    void set_build_delay_ms(int ms_per_document);

    // ---- M4 additions -----------------------------------------------------

    /**
     * @brief Sets the factory used to create the core for builds started after this call.
     * @param factory Factory to use; null restores the default ProcessingCore.
     */
    void set_core_factory(CoreFactory factory) noexcept;

    /** @brief What the current state object reports (for tests). */
    CorpusStatus corpus_state() const noexcept;

    // ---- CR-1 additions ---------------------------------------------------

    /**
     * @brief Starts serving remote Add requests on a Unix domain socket (CR-1).
     *
     * Requests are received on the IpcServer's own std::thread; each accepted
     * path is posted to the GUI thread as remote_add_requested().
     * @param socket_path Filesystem path of the socket to listen on.
     * @return false if the server could not be started (e.g. bind failed).
     */
    bool start_remote_add(const QString& socket_path);

signals:
    /**
     * @brief Emitted on the GUI thread when the current build's result is handled.
     * @param success true if the corpus became Current, false if the build failed.
     * Stale (discarded) results do not emit this signal.
     */
    void build_finished(bool success);

    /** @brief Emitted on the GUI thread whenever the current state object is replaced. */
    void corpus_state_changed();

    // Everything below this line is the M3 design. In M4 it is yours to
    // change: add, remove, or redesign private members, slots, and helpers.

private slots:
    /** @brief Handles a request to select and add a document to the workspace. */
    void add_document();

    /** @brief Rebuilds the processing corpus from the current non-empty workspace. */
    void build_corpus();

    /**
     * @brief Updates the selected-document display for the requested workspace row.
     * @param row Zero-based document row, or a negative value when no row is selected.
     */
    void document_selected(int row);

    /**
     * @brief Performs a search of the current corpus and presents the returned results.
     * @param query Query text to search for.
     * @param k Maximum number of results requested.
     */
    void search_requested(const QString& query, int k);

    /**
     * @brief Responds to a query edit by updating application action availability.
     * @param query Updated query text.
     */
    void query_changed(const QString& query);

    /**
     * @brief Builds and displays context for the current query and search settings.
     * @param token_budget Maximum token budget requested for the context.
     */
    void build_context_requested(int token_budget);
    /**
     * @brief Selects and displays the workspace document identified by a search result.
     * @param document_id Identifier of the source document to reveal.
     */
    void source_document_requested(const QString& document_id);

    /**
     * @brief Queries and presents term statistics for the current corpus.
     * @param term Term to inspect.
     * @param chunk_id Selected result chunk identifier; may be empty, in which
     * case document frequency is still available but chunk frequency is not.
     */
    void term_inspection_requested(const QString& term, const QString& chunk_id);

    /**
     * @brief Restores the application and its major GUI components to initial state.
     */
    void reset_application();

    /**
     * @brief Invokes the registered Exit callback for the current exit request.
     */
    void exit_application();

    /** @brief Takes the worker's finished build; a stale one was already discarded by take(). */
    void finish_build();

    /**
     * @brief GUI-thread handler for a path received by the IPC server (CR-1).
     *
     * Invoked via QMetaObject::invokeMethod with Qt::QueuedConnection from the
     * IPC thread; adds the file exactly as Add Document does.
     * @param path Absolute path of the document to add.
     */
    void remote_add_requested(const QString& path);

private:
    /**
     * @brief Updates availability of actions that depend on current application state.
     */
    void update_action_states();

    /**
     * @brief Clears displayed search and context output that depends on processing state.
     */
    void clear_processing_outputs();

    /** @brief Replaces the current state with next, unless next is null (stay). */
    void change_state(std::unique_ptr<CorpusState> next);

    friend class EmptyState;
    friend class OutOfDateState;
    friend class BuildingState;
    friend class CurrentState;
    void start_build();
    void cancel_build();
    void show_out_of_date();
    void accept_corpus(std::unique_ptr<ProcessingCore> corpus);
    void report_build_failure(const std::string& error);

    Workspace workspace_;
    std::unique_ptr<ProcessingCore> corpus_;     // null until a build is accepted
    std::unique_ptr<CorpusState> state_;
    ExitCallback exit_callback_{nullptr};

    int build_delay_ms_{0};
    CoreFactory core_factory_{nullptr};
    CorpusBuilder builder_;                      // its destructor stops and joins the build thread

    WorkspaceWidget* workspace_widget_{nullptr};
    SearchWidget* search_widget_{nullptr};
    TermInspectorWidget* term_inspector_widget_{nullptr};
    ContextWidget* context_widget_{nullptr};
    QLabel* status_label_{nullptr};
    QPushButton* reset_button_{nullptr};
    QPushButton* exit_button_{nullptr};

    std::unique_ptr<ipc::IpcServer> ipc_server_;
};

}  // namespace aiws::gui
