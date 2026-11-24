#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_builder.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/retrieval_engine.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
using aiws::gui::MainWindow;
using State = MainWindow::CorpusStatus;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Predicate>
bool wait_until(Predicate done, int timeout_ms = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout_ms) QTest::qWait(10);
    return done();
}

QString write_file(const QTemporaryDir& dir, const QString& name, const QByteArray& text) {
    const QString path = dir.filePath(name);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(text);
    return path;
}

QPushButton* find_button(MainWindow& window, const QString& text) {
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->text() == text) return button;
    }
    return nullptr;
}

int document_count(MainWindow& window) {
    return window.findChild<QListWidget*>()->count();
}

int run_send(const QStringList& args, const QString& working_dir = QString()) {
    QProcess process;
    process.setWorkingDirectory(working_dir);
    process.start(AIWS_SEND_PATH, args);
    process.waitForFinished(10000);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

int send_in_pieces(const std::string& socket_path, const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        close(fd);
        return -1;
    }

    const auto length = static_cast<std::uint32_t>(path.size());
    const char length_bytes[4] = {static_cast<char>(length >> 24), static_cast<char>(length >> 16),
                                  static_cast<char>(length >> 8), static_cast<char>(length)};
    send(fd, "6767", 4, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send(fd, length_bytes, 4, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send(fd, path.data(), path.size(), 0);

    std::uint8_t reply = 0xff;
    const bool received = recv(fd, &reply, 1, 0) == 1;
    close(fd);
    return received ? reply : -1;
}

class ThrowingChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document&, std::size_t) const override {
        throw std::runtime_error("chunker failed");
    }
};

class SlowChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document& document, std::size_t order) const override {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return aiws::Chunker{}.chunk(document, order);
    }
};

std::atomic<std::thread::id> recorded_thread_id;

class ThreadRecordingChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document& document, std::size_t order) const override {
        recorded_thread_id = std::this_thread::get_id();
        return aiws::Chunker{}.chunk(document, order);
    }
};

std::atomic<int> counted_builds{0};

class CountingChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document& document, std::size_t order) const override {
        ++counted_builds;
        return aiws::Chunker{}.chunk(document, order);
    }
};

std::unique_ptr<aiws::ProcessingCore> failing_core() {
    return std::make_unique<aiws::ProcessingCore>(std::make_unique<ThrowingChunker>(),
                                                  std::make_unique<aiws::RetrievalEngine>(),
                                                  std::make_unique<aiws::ContextBuilder>());
}

std::unique_ptr<aiws::ProcessingCore> slow_core() {
    return std::make_unique<aiws::ProcessingCore>(std::make_unique<SlowChunker>(),
                                                  std::make_unique<aiws::RetrievalEngine>(),
                                                  std::make_unique<aiws::ContextBuilder>());
}

std::unique_ptr<aiws::ProcessingCore> thread_recording_core() {
    return std::make_unique<aiws::ProcessingCore>(std::make_unique<ThreadRecordingChunker>(),
                                                  std::make_unique<aiws::RetrievalEngine>(),
                                                  std::make_unique<aiws::ContextBuilder>());
}

std::unique_ptr<aiws::ProcessingCore> counting_core() {
    return std::make_unique<aiws::ProcessingCore>(std::make_unique<CountingChunker>(),
                                                  std::make_unique<aiws::RetrievalEngine>(),
                                                  std::make_unique<aiws::ContextBuilder>());
}

aiws::Workspace one_document() {
    aiws::Workspace workspace;
    workspace.add_document(aiws::Document{"a", "A", "Alpha beta gamma."});
    return workspace;
}

std::optional<aiws::BuildResult> wait_for_result(aiws::CorpusBuilder& builder) {
    std::optional<aiws::BuildResult> result;
    wait_until([&] {
        if (!result) result = builder.take();
        return result.has_value();
    });
    return result;
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const QString doc_a = write_file(dir, "a.txt", "Alpha beta gamma.");
    const QString doc_b = write_file(dir, "b.txt", "Delta epsilon zeta.");

    {
        auto window = std::make_unique<MainWindow>();
        window->add_document_file(doc_a);
        find_button(*window, "Build Corpus")->click();
        wait_until([&] { return window->corpus_state() == State::Current; });
        QElapsedTimer timer;
        timer.start();
        window.reset();
        check(timer.elapsed() < 1000, "exiting with no build running returns quickly");
    }

    {
        aiws::CorpusBuilder builder;
        builder.request(std::make_unique<aiws::ProcessingCore>(), one_document(), 0);
        const std::optional<aiws::BuildResult> result = wait_for_result(builder);
        check(result && result->core != nullptr && result->core->chunk_count() > 0 && result->error.empty(),
              "a successful build produces a filled core with no error");
    }

    {
        aiws::CorpusBuilder builder;
        builder.request(failing_core(), one_document(), 0);
        const std::optional<aiws::BuildResult> result = wait_for_result(builder);
        check(result && result->core == nullptr && !result->error.empty(), "a failing build reports its error message and no core");
    }

    {
        aiws::CorpusBuilder builder;
        std::vector<std::thread::id> build_threads;
        for (int i = 0; i < 2; i++) {
            builder.request(thread_recording_core(), one_document(), 0);
            wait_for_result(builder);
            build_threads.push_back(recorded_thread_id.load());
        }
        check(build_threads.size() == 2 && build_threads[0] == build_threads[1] &&
                  build_threads[0] != std::this_thread::get_id(),
              "every build runs on the same worker thread, not the gui thread");
    }

    {
        aiws::CorpusBuilder builder;
        builder.request(counting_core(), one_document(), 3000);
        builder.cancel();
        builder.request(std::make_unique<aiws::ProcessingCore>(), one_document(), 0);
        QElapsedTimer timer;
        timer.start();
        const std::optional<aiws::BuildResult> result = wait_for_result(builder);
        check(result && timer.elapsed() < 1000 && counted_builds == 0,
              "a cancelled build request is never built and does not delay the next one");
    }

    {
        MainWindow window;
        QPushButton* build = find_button(window, "Build Corpus");
        const bool disabled_when_empty = !build->isEnabled();
        window.add_document_file(doc_a);
        const bool enabled_with_document = build->isEnabled();
        find_button(window, "Reset")->click();
        check(disabled_when_empty && enabled_with_document && !build->isEnabled(),
              "Build Corpus is enabled only once the workspace has a document, and disabled again after Reset");
    }

    {
        MainWindow window;
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        check(window.corpus_state() == State::Building, "clicking Build Corpus starts a background build");
        check(wait_until([&] { return window.corpus_state() == State::Current; }),
              "a finished build makes the corpus current");
    }

    {
        MainWindow window;
        window.set_core_factory(slow_core);
        window.add_document_file(doc_a);
        QPushButton* build = find_button(window, "Build Corpus");
        build->click();
        QTest::qWait(100);
        window.add_document_file(doc_b);
        check(build->isEnabled(), "Build is enabled as soon as a document is added mid-build");
        QTest::qWait(600);
        check(window.corpus_state() == State::NotCurrent,
              "a build that finishes after a document was added is rejected");
    }

    {
        MainWindow window;
        window.set_core_factory(slow_core);
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        QTest::qWait(100);
        find_button(window, "Reset")->click();
        QTest::qWait(600);
        check(window.corpus_state() == State::NotCurrent, "a build that finishes after Reset is rejected");
    }

    {
        MainWindow window;
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        wait_until([&] { return window.corpus_state() == State::Current; });
        window.findChild<QLineEdit*>()->setText("alpha");
        find_button(window, "Search")->click();
        check(window.findChild<QTableView*>()->model()->rowCount() > 0, "a search on a current corpus shows results");

        window.set_core_factory(failing_core);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() != State::Building; }),
              "a failed build leaves the building state");
        check(window.corpus_state() == State::NotCurrent, "a failed build leaves the corpus not current");
        check(!find_button(window, "Search")->isEnabled() && !find_button(window, "Inspect")->isEnabled() &&
                  !find_button(window, "Build Context")->isEnabled(),
              "search, inspect, and context are unavailable after a failed build");
        check(window.findChild<QTableView*>()->model()->rowCount() == 0,
              "results from the old corpus are not shown after a failed build");

        window.set_core_factory(nullptr);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() == State::Current; }),
              "after a failed build the user can build again successfully");
    }

    {
        MainWindow window;
        window.set_build_delay_ms(3000);
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        QTest::qWait(100);
        find_button(window, "Reset")->click();
        window.add_document_file(doc_a);
        window.set_build_delay_ms(0);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() == State::Current; }, 1000),
              "a reset during a long build lets the next build finish without waiting");
    }

    {
        MainWindow window;
        window.set_build_delay_ms(3000);
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        QTest::qWait(100);
        window.add_document_file(doc_b);
        window.set_build_delay_ms(0);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() == State::Current; }, 1000),
              "adding a document during a long build lets the next build finish without waiting");
    }

    {
        auto window = std::make_unique<MainWindow>();
        window->set_core_factory(slow_core);
        window->add_document_file(doc_a);
        find_button(*window, "Build Corpus")->click();
        QTest::qWait(100);
        QElapsedTimer timer;
        timer.start();
        window.reset();
        check(timer.elapsed() < 1000, "exiting while a build is processing returns quickly");
    }

    {
        auto window = std::make_unique<MainWindow>();
        window->set_build_delay_ms(5000);
        window->add_document_file(doc_a);
        find_button(*window, "Build Corpus")->click();
        QElapsedTimer timer;
        timer.start();
        window.reset();
        check(timer.elapsed() < 1000, "destroying the window during a slow build returns quickly");
    }

    const QString socket_path = dir.filePath("s.sock");

    {
        MainWindow window;
        check(window.start_remote_add(socket_path), "the window starts serving remote adds");
        write_file(dir, "rel.txt", "Relative path document.");
        check(run_send({"--socket", socket_path, "add", "rel.txt"}, dir.path()) == 0,
              "aiws_send add with a relative path exits 0");
        check(wait_until([&] { return document_count(window) == 1; }),
              "a remote add adds the document like Add Document");

        const QStringList names = {"my doc.txt", QString::fromUtf8("\xC3\xA9t\xC3\xA9.txt"), "line\nbreak.txt"};
        for (const QString& name : names) {
            const QString path = write_file(dir, name, "Unusual file name.");
            check(run_send({"--socket", socket_path, "add", path}) == 0,
                  "aiws_send accepts a path with spaces, UTF-8, or a newline");
        }
        check(wait_until([&] { return document_count(window) == 4; }),
              "documents with spaces, UTF-8, and newlines in their paths are all added");

        check(send_in_pieces(socket_path.toStdString(), doc_a.toStdString()) == 0,
              "a request split into pieces with pauses is accepted");
        check(wait_until([&] { return document_count(window) == 5; }),
              "a request split into pieces adds the document");

        check(run_send({"--socket", socket_path, "add", dir.filePath("missing.txt")}) == 1,
              "aiws_send exits 1 when the application rejects an unreadable file");
        check(run_send({"--socket", dir.filePath("none.sock"), "add", doc_a}) == 2,
              "aiws_send exits 2 when no application is listening");
    }

    {
        auto window = std::make_unique<MainWindow>();
        window->start_remote_add(socket_path);
        check(QFile::exists(socket_path), "the socket file exists while serving");
        QElapsedTimer timer;
        timer.start();
        window.reset();
        check(timer.elapsed() < 500, "the application stops serving within 500 ms on exit");
        check(!QFile::exists(socket_path), "the socket file is removed on exit");
    }

    if (failures == 0) {
        std::cout << "All M4 student tests passed.\n";
        return 0;
    }
    std::cerr << failures << " M4 student test(s) failed.\n";
    return 1;
}
