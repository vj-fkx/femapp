#include "main_window.hpp"

#include <fem/core/version.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/viewport.hpp>

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <array>
#include <string_view>
#include <utility>

namespace fem::app {

namespace {
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kFailureMessageMargin = 24;
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("femapp"));
    resize(kDefaultWidth, kDefaultHeight);

    // REQ-002-01: the 3D viewport is the central area of the window.
    //
    // It is created first and without a parent on purpose. Qt switches a
    // top-level window to GPU composition when a QRhiWidget is reparented into
    // it; a viewport constructed directly as a child of a window whose native
    // handle already exists (which the menu bar can cause on macOS) would be
    // left without a QRhi.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): setCentralWidget takes ownership.
    auto* viewport = new render::Viewport;
    m_viewport = viewport;
    setCentralWidget(viewport);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);
    exitAction->setMenuRole(QAction::QuitRole);

    createViewMenu();

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About femapp"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);

    statusBar()->showMessage(tr("Ready"));

    connect(m_viewport, &render::Viewport::frameRendered, this, &MainWindow::viewportFrameRendered);
    // Queued: the handler replaces the viewport, which must not happen while
    // the viewport is still emitting the signal.
    connect(m_viewport, &render::Viewport::backendUnavailable, this,
            &MainWindow::showViewportFailure, Qt::QueuedConnection);
    if (!m_viewport->isAvailable()) {
        showViewportFailure(m_viewport->unavailableReason());
    }
}

bool MainWindow::isViewportAvailable() const
{
    return !m_viewport.isNull();
}

void MainWindow::createViewMenu()
{
    using render::StandardView;

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    struct ViewEntry
    {
        QString text;
        StandardView view;
        Qt::Key key;
    };
    // REQ-002-17. Shortcuts use a modifier so that plain keys stay free for
    // the drafting command line.
    const std::array<ViewEntry, 7> views = {{
        {.text = tr("&Front"), .view = StandardView::Front, .key = Qt::Key_1},
        {.text = tr("&Back"), .view = StandardView::Back, .key = Qt::Key_2},
        {.text = tr("&Left"), .view = StandardView::Left, .key = Qt::Key_3},
        {.text = tr("&Right"), .view = StandardView::Right, .key = Qt::Key_4},
        {.text = tr("&Top"), .view = StandardView::Top, .key = Qt::Key_5},
        {.text = tr("B&ottom"), .view = StandardView::Bottom, .key = Qt::Key_6},
        {.text = tr("&Isometric"), .view = StandardView::Isometric, .key = Qt::Key_7},
    }};
    for (const ViewEntry& entry : views) {
        const StandardView view = entry.view;
        QAction* action = viewMenu->addAction(entry.text, m_viewport,
                                              [this, view] { m_viewport->setStandardView(view); });
        action->setShortcut(QKeySequence(Qt::CTRL | entry.key));
        m_viewActions.append(action);
    }

    viewMenu->addSeparator();

    // REQ-002-16
    QAction* orthographic = viewMenu->addAction(tr("Orthographic &Projection"));
    orthographic->setCheckable(true);
    orthographic->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(orthographic, &QAction::toggled, m_viewport, [this](bool checked) {
        m_viewport->setProjection(checked ? render::Projection::Orthographic
                                          : render::Projection::Perspective);
    });
    m_viewActions.append(orthographic);

    // REQ-002-18
    QAction* fitAll = viewMenu->addAction(tr("Fit &All"), m_viewport, &render::Viewport::fitAll);
    fitAll->setShortcut(QKeySequence(Qt::Key_Home));
    m_viewActions.append(fitAll);

    viewMenu->addSeparator();

    // REQ-002-26
    QAction* grid = viewMenu->addAction(tr("&Grid"));
    grid->setCheckable(true);
    grid->setChecked(m_viewport->isGridVisible());
    grid->setShortcut(QKeySequence(Qt::Key_F7));
    connect(grid, &QAction::toggled, m_viewport, &render::Viewport::setGridVisible);
    m_viewActions.append(grid);

    QAction* axes = viewMenu->addAction(tr("A&xes"));
    axes->setCheckable(true);
    axes->setChecked(m_viewport->isAxesVisible());
    connect(axes, &QAction::toggled, m_viewport, &render::Viewport::setAxesVisible);
    m_viewActions.append(axes);
}

void MainWindow::showViewportFailure(const QString& reason)
{
    if (m_viewport.isNull()) {
        return;
    }

    // REQ-002-06: the window stays open and explains what happened.
    auto* message = new QLabel(tr("The 3D viewport is not available.\n\n%1").arg(reason), this);
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    message->setMargin(kFailureMessageMargin);
    // Replacing the central widget deletes the viewport.
    setCentralWidget(message);
    m_viewport = nullptr;

    for (QAction* action : std::as_const(m_viewActions)) {
        action->setEnabled(false);
    }
    emit viewportUnavailable(reason);
}

void MainWindow::showAbout()
{
    const std::string_view version = core::versionString();
    // REQ-002-07
    QString backend = m_viewport.isNull() ? QString() : m_viewport->backendName();
    if (backend.isEmpty()) {
        backend = tr("not available");
    }
    const QString text =
        tr("<b>femapp</b> %1<br>Finite element analysis application.<br><br>"
           "Qt %2<br>Graphics backend: %3<br>License: MIT")
            .arg(QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())),
                 QString::fromLatin1(qVersion()), backend);
    QMessageBox::about(this, tr("About femapp"), text);
}

} // namespace fem::app
