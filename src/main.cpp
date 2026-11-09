#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QString>

#include <iostream>

namespace {
void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Are you sure you want to exit AI Workspace?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) QApplication::quit();
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption delay_option("build-delay-ms", "Extra build time per document.", "N", "0");
    const QCommandLineOption socket_option("ipc-socket", "Socket for remote Add requests.", "PATH",
                                           QString::fromStdString(aiws::ipc::default_socket_path()));
    parser.addOption(delay_option);
    parser.addOption(socket_option);
    parser.process(app);

    aiws::gui::MainWindow window;
    window.set_exit_callback(confirm_exit);
    window.set_build_delay_ms(parser.value(delay_option).toInt());
    if (!window.start_remote_add(parser.value(socket_option))) {
        std::cerr << "warning: could not listen on " << parser.value(socket_option).toStdString() << '\n';
    }
    window.show();

    return app.exec();
}
