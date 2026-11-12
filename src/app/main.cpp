#include "cli_options.hpp"
#include "main_window.hpp"

#include <fem/core/version.hpp>

#include <QApplication>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QTranslator>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace {

constexpr int kSmokeTestTimeoutMs = 10000;

int run(int argc, char** argv)
{
    const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
    const fem::app::CliOptions options = fem::app::parseCliOptions(args);
    const std::string_view version = fem::core::versionString();

    // Handled before QApplication exists so it works without a display.
    if (options.showVersion) {
        std::cout << "femapp " << version << '\n';
        return EXIT_SUCCESS;
    }

    const QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("femapp"));
    QApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("femapp"), QStringLiteral("_"),
                        QStringLiteral(":/i18n"))) {
        QApplication::installTranslator(&translator);
    }

    fem::app::MainWindow window;
    window.setQuietMessages(options.smokeTest);
    window.show();
    if (options.meshFile) {
        // REQ-004-03
        window.importMesh(QString::fromStdString(*options.meshFile));
    }

    if (options.smokeTest) {
        // REQ-002-29: succeed once the viewport has rendered a frame, or once
        // the window has reported that no graphics backend is available
        // (REQ-002-06); fail if neither happens in time.
        const auto finish = [](const char* outcome) {
            static bool finished = false;
            if (!finished) {
                finished = true;
                std::cout << "femapp: smoke test: " << outcome << '\n' << std::flush;
                QApplication::exit(EXIT_SUCCESS);
            }
        };
        // REQ-004-31: with a mesh, wait until it is on screen.
        if (options.meshFile) {
            QObject::connect(&window, &fem::app::MainWindow::meshShown, &window,
                             [finish] { finish("mesh rendered"); });
            QObject::connect(&window, &fem::app::MainWindow::meshImportFailed, &window, [] {
                std::cout << "femapp: smoke test: mesh import failed\n" << std::flush;
                QApplication::exit(EXIT_FAILURE);
            });
        } else {
            QObject::connect(&window, &fem::app::MainWindow::viewportFrameRendered, &window,
                             [finish] { finish("frame rendered"); });
        }
        QObject::connect(&window, &fem::app::MainWindow::viewportUnavailable, &window,
                         [finish] { finish("viewport unavailable"); });
        if (!window.isViewportAvailable()) {
            QTimer::singleShot(0, &window, [finish] { finish("viewport unavailable"); });
        }
        QTimer::singleShot(kSmokeTestTimeoutMs, &window, [] {
            std::cout << "femapp: smoke test: timed out\n" << std::flush;
            QApplication::exit(EXIT_FAILURE);
        });
    }

    return QApplication::exec();
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (...) {
        std::fputs("femapp: fatal error\n", stderr);
        return EXIT_FAILURE;
    }
}
