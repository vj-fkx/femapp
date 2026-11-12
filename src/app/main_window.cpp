#include "main_window.hpp"

#include "mesh_import.hpp"

#include <fem/core/version.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/viewport.hpp>

#include <QAction>
#include <QActionGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMimeData>
#include <QProgressBar>
#include <QUrl>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>

#include <array>
#include <iostream>
#include <string_view>
#include <utility>

namespace fem::app {

namespace {
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kFailureMessageMargin = 24;
constexpr int kProgressWidth = 120;

// REQ-004-02: local files ending in .msh, in any case.
QString droppedMeshFile(const QMimeData* data)
{
    if (data == nullptr || !data->hasUrls()) {
        return {};
    }
    for (const QUrl& url : data->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && path.endsWith(QStringLiteral(".msh"), Qt::CaseInsensitive)) {
            return path;
        }
    }
    return {};
}
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
    // REQ-004-01
    m_importAction = fileMenu->addAction(tr("&Import Mesh..."), this, &MainWindow::chooseMeshFile);
    m_importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);
    exitAction->setMenuRole(QAction::QuitRole);

    createEditMenu();
    createViewMenu();

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About femapp"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);

    statusBar()->showMessage(tr("Ready"));
    // REQ-004-04, 08, 27
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setMaximumWidth(kProgressWidth);
    m_progress->hide();
    m_meshInfo = new QLabel(this);
    m_selectionInfo = new QLabel(this);
    m_import = new MeshImport(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    statusBar()->addPermanentWidget(m_selectionInfo);
    statusBar()->addPermanentWidget(m_meshInfo);
    statusBar()->addPermanentWidget(m_progress);
    setAcceptDrops(true);

    connect(m_import, &MeshImport::finished, this, &MainWindow::meshImported);
    connect(m_import, &MeshImport::failed, this, &MainWindow::meshFailed);
    connect(m_viewport, &render::Viewport::sceneShown, this, &MainWindow::meshShown);
    connect(m_viewport, &render::Viewport::selectionChanged, this, &MainWindow::showSelection);

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

    // REQ-004-11: display modes.
    QMenu* displayMenu = viewMenu->addMenu(tr("&Display"));
    auto* displayGroup = new QActionGroup(displayMenu);
    struct DisplayEntry
    {
        QString text;
        render::DisplayMode mode;
    };
    const std::array<DisplayEntry, 3> displayModes = {{
        {.text = tr("Shaded with &Edges"), .mode = render::DisplayMode::ShadedWithEdges},
        {.text = tr("&Shaded"), .mode = render::DisplayMode::Shaded},
        {.text = tr("&Wireframe"), .mode = render::DisplayMode::Wireframe},
    }};
    for (const DisplayEntry& entry : displayModes) {
        const render::DisplayMode mode = entry.mode;
        QAction* action = displayMenu->addAction(entry.text, m_viewport,
                                                 [this, mode] { m_viewport->setDisplayMode(mode); });
        action->setCheckable(true);
        action->setChecked(mode == m_viewport->displayOptions().mode);
        displayGroup->addAction(action);
        m_viewActions.append(action);
    }

    // REQ-004-14: colors.
    QMenu* colorMenu = viewMenu->addMenu(tr("&Color"));
    auto* colorGroup = new QActionGroup(colorMenu);
    struct ColorEntry
    {
        QString text;
        render::ColorMode mode;
    };
    const std::array<ColorEntry, 2> colorModes = {{
        {.text = tr("By &Dimension"), .mode = render::ColorMode::ByDimension},
        {.text = tr("By &Group"), .mode = render::ColorMode::ByGroup},
    }};
    for (const ColorEntry& entry : colorModes) {
        const render::ColorMode mode = entry.mode;
        QAction* action = colorMenu->addAction(entry.text, m_viewport,
                                               [this, mode] { m_viewport->setColorMode(mode); });
        action->setCheckable(true);
        action->setChecked(mode == m_viewport->displayOptions().color);
        colorGroup->addAction(action);
        m_viewActions.append(action);
    }

    // REQ-004-16: visibility per dimension.
    QMenu* showMenu = viewMenu->addMenu(tr("S&how Elements"));
    const std::array<QString, 4> dimensionNames = {tr("&0D (Points)"), tr("&1D (Lines)"),
                                                   tr("&2D (Surfaces)"), tr("&3D (Solids)")};
    for (int dimension = 0; dimension < 4; ++dimension) {
        QAction* action = showMenu->addAction(dimensionNames.at(static_cast<std::size_t>(dimension)));
        action->setCheckable(true);
        action->setChecked(m_viewport->displayOptions().isVisible(dimension));
        connect(action, &QAction::toggled, m_viewport,
                [this, dimension](bool visible) { m_viewport->setDimensionVisible(dimension, visible); });
        m_viewActions.append(action);
    }

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

void MainWindow::createEditMenu()
{
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    // REQ-004-19
    auto* modeGroup = new QActionGroup(editMenu);
    QAction* elements = editMenu->addAction(tr("Select &Elements"), m_viewport, [this] {
        m_viewport->setSelectionMode(render::SelectionMode::Elements);
    });
    QAction* nodes = editMenu->addAction(tr("Select &Nodes"), m_viewport, [this] {
        m_viewport->setSelectionMode(render::SelectionMode::Nodes);
    });
    for (QAction* action : {elements, nodes}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
        m_viewActions.append(action);
    }
    elements->setChecked(true);

    editMenu->addSeparator();

    // REQ-004-25
    QAction* clear =
        editMenu->addAction(tr("&Clear Selection"), m_viewport, &render::Viewport::clearSelection);
    clear->setShortcut(QKeySequence(Qt::Key_Escape));
    m_viewActions.append(clear);
}

void MainWindow::chooseMeshFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Mesh"), QString(), tr("Gmsh meshes (*.msh);;All files (*)"));
    if (!path.isEmpty()) {
        importMesh(path);
    }
}

void MainWindow::importMesh(const QString& path)
{
    if (m_viewport.isNull() || m_import->isRunning()) {
        return;
    }
    // REQ-004-04: one import at a time, with a progress indicator.
    m_importAction->setEnabled(false);
    m_progress->show();
    statusBar()->showMessage(tr("Importing %1...").arg(QFileInfo(path).fileName()));
    m_import->start(path);
}

void MainWindow::meshImported(const QString& path,
                              std::shared_ptr<const render::MeshScene> scene,
                              const QStringList& warnings)
{
    m_importAction->setEnabled(true);
    m_progress->hide();
    statusBar()->clearMessage();
    if (m_viewport.isNull()) {
        return;
    }
    // REQ-004-08
    const core::Mesh& mesh = scene->mesh();
    m_meshInfo->setText(tr("%1: %2 nodes, %3 elements, %4 groups")
                            .arg(QFileInfo(path).fileName())
                            .arg(mesh.nodeCount())
                            .arg(mesh.elementCount())
                            .arg(mesh.groups().size()));
    // REQ-004-05
    m_viewport->setScene(std::move(scene));
    // REQ-004-06
    if (!warnings.isEmpty()) {
        showMessage(tr("Mesh imported with warnings"), warnings.join(QLatin1Char('\n')), false);
    }
}

void MainWindow::meshFailed(const QString& /*path*/, const QString& message)
{
    m_importAction->setEnabled(true);
    m_progress->hide();
    statusBar()->clearMessage();
    // REQ-004-07: the mesh shown stays as it was.
    showMessage(tr("Could not import the mesh"), message, true);
    emit meshImportFailed(message);
}

void MainWindow::showSelection(int count, render::SelectionMode mode)
{
    // REQ-004-27
    if (count == 0) {
        m_selectionInfo->clear();
        return;
    }
    m_selectionInfo->setText(mode == render::SelectionMode::Elements
                                 ? tr("%n element(s) selected", nullptr, count)
                                 : tr("%n node(s) selected", nullptr, count));
}

void MainWindow::showMessage(const QString& title, const QString& text, bool error)
{
    if (m_quietMessages) {
        std::cout << "femapp: " << title.toStdString() << ": " << text.toStdString() << '\n'
                  << std::flush;
        return;
    }
    if (error) {
        QMessageBox::warning(this, title, text);
    } else {
        QMessageBox::information(this, title, text);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!droppedMeshFile(event->mimeData()).isEmpty() && !m_import->isRunning()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QString path = droppedMeshFile(event->mimeData());
    if (!path.isEmpty()) {
        event->acceptProposedAction();
        importMesh(path);
    }
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
