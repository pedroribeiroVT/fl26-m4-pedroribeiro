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
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
using aiws::gui::MainWindow;
using State = MainWindow::CorpusState;

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

aiws::Workspace one_document() {
    aiws::Workspace workspace;
    workspace.add_document(aiws::Document{"a", "A", "Alpha beta gamma."});
    return workspace;
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    const QString doc_a = write_file(dir, "a.txt", "Alpha beta gamma.");
    const QString doc_b = write_file(dir, "b.txt", "Delta epsilon zeta.");

    {
        aiws::CorpusBuilder builder;
        builder.start(std::make_unique<aiws::ProcessingCore>(), one_document(), 0, 7);
        check(wait_until([&] { return builder.result_ready(); }), "a successful build produces a result");
        aiws::BuildResult result = builder.take();
        check(result.build_id == 7, "the result carries the id the build was started with");
        check(result.core != nullptr && result.core->chunk_count() > 0, "a successful build returns a filled core");
        check(result.error.empty(), "a successful build reports no error");
        check(!builder.running(), "taking the result joins the build thread");
    }

    {
        aiws::CorpusBuilder builder;
        builder.start(failing_core(), one_document(), 0, 3);
        check(wait_until([&] { return builder.result_ready(); }), "a failing build still produces a result");
        aiws::BuildResult result = builder.take();
        check(result.core == nullptr, "a failing build returns no core");
        check(!result.error.empty(), "a failing build returns its error message");
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
        check(wait_until([&] { return build->isEnabled(); }), "the stale build finishes and Build is enabled again");
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
        window.set_core_factory(failing_core);
        window.add_document_file(doc_a);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() != State::Building; }),
              "a failed build leaves the building state");
        check(window.corpus_state() == State::NotCurrent, "a failed build leaves the corpus not current");

        window.set_core_factory(nullptr);
        find_button(window, "Build Corpus")->click();
        check(wait_until([&] { return window.corpus_state() == State::Current; }),
              "after a failed build the user can build again successfully");
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
