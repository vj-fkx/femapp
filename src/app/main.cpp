#include "cli_options.hpp"
#include "main_window.hpp"

#include <fem/core/version.hpp>

#include <QApplication>
#include <QLocale>
#include <QString>
#include <QTimer>
#include <QTranslator>

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

int main(int argc, char* argv[])
{
    const std::span<const char* const> args(argv, static_cast<std::size_t>(argc));
    const fem::app::CliOptions options = fem::app::parseCliOptions(args);
    const std::string_view version = fem::core::versionString();

    // Handled before QApplication exists so it works without a display.
    if (options.showVersion) {
        std::cout << "femapp " << version << '\n';
        return 0;
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
    window.show();

    if (options.smokeTest) {
        QTimer::singleShot(0, &app, &QApplication::quit);
    }

    return QApplication::exec();
}
