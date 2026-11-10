#include "main_window.hpp"

#include <fem/core/version.hpp>

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <string_view>

namespace fem::app {

namespace {
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("femapp"));
    resize(kDefaultWidth, kDefaultHeight);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);
    exitAction->setMenuRole(QAction::QuitRole);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About femapp"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);

    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::showAbout()
{
    const std::string_view version = core::versionString();
    const QString text =
        tr("<b>femapp</b> %1<br>Finite element analysis application.<br><br>"
           "Qt %2<br>License: MIT")
            .arg(QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())),
                 QString::fromLatin1(qVersion()));
    QMessageBox::about(this, tr("About femapp"), text);
}

} // namespace fem::app
