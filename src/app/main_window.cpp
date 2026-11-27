#include "main_window.hpp"

#include "analysis_job.hpp"
#include "analysis_settings_dialog.hpp"
#include "analysis_text.hpp"
#include "check_panel.hpp"
#include "command_line_widget.hpp"
#include "drafting_controller.hpp"
#include "drafting_settings_dialog.hpp"
#include "mesh_import.hpp"
#include "model_controller.hpp"
#include "model_dialogs.hpp"
#include "model_tree.hpp"
#include "property_panel.hpp"
#include "result_display_bar.hpp"
#include "result_view.hpp"
#include "results_panel.hpp"
#include "section_geometry.hpp"

#include <fem/core/version.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>
#include <fem/render/viewport.hpp>

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fem::app {

namespace {
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kFailureMessageMargin = 24;
constexpr int kProgressWidth = 120;
constexpr int kAnalysisProgressMs = 100; // REQ-011-07 (design §5.4)
constexpr int kPercent = 100;

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

    createDraftingUi();
    createModelUi();
    createEditMenu();
    createViewMenu();
    createDraftingMenus();
    createModelMenu();
    createAnalysisMenu();

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
    orthographic->setChecked(m_viewport->projection() == render::Projection::Orthographic);
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
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by its Qt parent.
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
        QAction* action = displayMenu->addAction(
            entry.text, m_viewport, [this, mode] { m_viewport->setDisplayMode(mode); });
        action->setCheckable(true);
        action->setChecked(mode == m_viewport->displayOptions().mode);
        displayGroup->addAction(action);
        m_viewActions.append(action);
    }

    // REQ-004-14: colors.
    QMenu* colorMenu = viewMenu->addMenu(tr("&Color"));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by its Qt parent.
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
        QAction* action =
            showMenu->addAction(dimensionNames.at(static_cast<std::size_t>(dimension)));
        action->setCheckable(true);
        action->setChecked(m_viewport->displayOptions().isVisible(dimension));
        connect(action, &QAction::toggled, m_viewport, [this, dimension](bool visible) {
            m_viewport->setDimensionVisible(dimension, visible);
        });
        m_viewActions.append(action);
    }

    // REQ-004-34
    QAction* nodes = viewMenu->addAction(tr("Show &Nodes"));
    nodes->setCheckable(true);
    nodes->setChecked(m_viewport->displayOptions().showNodes);
    connect(nodes, &QAction::toggled, m_viewport, &render::Viewport::setNodesShown);
    m_viewActions.append(nodes);

    // REQ-004-37, 39: the viewport may refuse (empty selection) or turn it off
    // by itself (selection emptied), so the action follows it.
    QAction* isolate = viewMenu->addAction(tr("Show &Selection Only"));
    isolate->setCheckable(true);
    isolate->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(isolate, &QAction::toggled, m_viewport, &render::Viewport::setIsolated);
    connect(m_viewport, &render::Viewport::isolationChanged, isolate, [isolate](bool on) {
        const QSignalBlocker blocker(isolate);
        isolate->setChecked(on);
    });
    m_viewActions.append(isolate);

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

    // REQ-012-01: the result in place of the mesh.
    viewMenu->addSeparator();
    m_showResultsAction = viewMenu->addAction(tr("Show &Results in Viewport"));
    m_showResultsAction->setCheckable(true);
    m_showResultsAction->setEnabled(m_resultView != nullptr && m_resultView->isAvailable());
    connect(m_showResultsAction, &QAction::toggled, this, [this](bool on) {
        if (m_resultView != nullptr) {
            m_resultView->setShown(on);
        }
    });
    m_viewActions.append(m_showResultsAction);
}

void MainWindow::createEditMenu()
{
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    // REQ-005-31
    QAction* undo = editMenu->addAction(tr("&Undo"), m_drafting, &DraftingController::undo);
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = editMenu->addAction(tr("&Redo"), m_drafting, &DraftingController::redo);
    redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence::Redo});
    const auto refreshUndo = [this, undo, redo] {
        undo->setEnabled(m_drafting->undoStack()->canUndo());
        redo->setEnabled(m_drafting->undoStack()->canRedo());
    };
    connect(m_drafting->undoStack(), &QUndoStack::indexChanged, this, refreshUndo);
    refreshUndo();
    editMenu->addSeparator();

    // REQ-005-26: what the selection picks when no command asks for objects.
    QMenu* targetMenu = editMenu->addMenu(tr("Selection &Target"));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by its Qt parent.
    auto* targetGroup = new QActionGroup(targetMenu);
    const std::array<std::pair<render::SelectionTarget, QString>, 2> targets = {{
        {render::SelectionTarget::Drawing, tr("&Drawing")},
        {render::SelectionTarget::Mesh, tr("&Mesh")},
    }};
    for (const auto& [target, text] : targets) {
        QAction* action = targetMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(m_viewport->selectionTarget() == target);
        targetGroup->addAction(action);
        connect(action, &QAction::triggered, m_viewport,
                [this, target = target] { m_viewport->setSelectionTarget(target); });
        m_viewActions.append(action);
    }

    // REQ-004-19: kinds that can be combined; the last one on cannot be
    // turned off (the action is checked again).
    QMenu* selectMenu = editMenu->addMenu(tr("&Select"));
    const std::array<std::pair<render::SelectionKind, QString>, render::kSelectionKindCount> kinds =
        {{{render::SelectionKind::Element, tr("&Elements")},
          {render::SelectionKind::Face, tr("Solid &Faces")},
          {render::SelectionKind::Edge, tr("E&dges")},
          {render::SelectionKind::Node, tr("&Nodes")}}};
    for (const auto& [kind, name] : kinds) {
        QAction* action = selectMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(m_viewport->selectionFilter().isEnabled(kind));
        connect(action, &QAction::toggled, m_viewport, [this, action, kind = kind](bool on) {
            if (!m_viewport->setSelectionKindEnabled(kind, on)) {
                const QSignalBlocker blocker(action);
                action->setChecked(true);
            }
        });
        m_viewActions.append(action);
    }

    editMenu->addSeparator();

    // REQ-004-25
    QAction* clear =
        editMenu->addAction(tr("&Clear Selection"), m_drafting, &DraftingController::escape);
    clear->setShortcut(QKeySequence(Qt::Key_Escape));
    m_viewActions.append(clear);
}

void MainWindow::createModelUi()
{
    // REQ-010-19: the model tree, docked on the left.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_tree = new ModelTree(m_model, this);
    m_treeView = new QTreeView(this);
    auto* dock = new QDockWidget(tr("Model"), this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    m_tree->setWarningIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    m_treeView->setModel(m_tree);
    m_treeView->setHeaderHidden(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->expandAll();
    // The tree is rebuilt on every change: open again, as it was before.
    connect(m_tree, &QAbstractItemModel::modelReset, m_treeView, &QTreeView::expandAll);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &MainWindow::showModelMenu);
    connect(m_treeView, &QTreeView::doubleClicked, this, &MainWindow::activateItem);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                highlightTargets(current);
                m_properties->showItem(m_tree->itemAt(current));
            });
    connect(m_model, &ModelController::refused, this, [this](const setup::EditRefusal& refusal) {
        QString text;
        switch (refusal.kind) {
        case setup::EditRefusal::Kind::InUse:
            text = tr("It is in use by:");
            break;
        case setup::EditRefusal::Kind::DuplicateName:
            text = tr("The name is already taken by:");
            break;
        case setup::EditRefusal::Kind::NotFound:
            text = tr("The item is not in the model.");
            break;
        case setup::EditRefusal::Kind::NoTargets:
            text = tr("Select the objects to assign it to first.");
            break;
        case setup::EditRefusal::Kind::Unsupported:
            text = tr("This cannot be done with this kind of item.");
            break;
        }
        for (const setup::ItemId id : refusal.items) {
            if (const setup::Item* item = m_model->document().find(id)) {
                text += QLatin1Char('\n') + QStringLiteral("  ") + m_tree->label(*item);
            }
        }
        showMessage(tr("The change was refused"), text, true);
    });
    dock->setObjectName(QStringLiteral("modelTree"));
    dock->setWidget(m_treeView);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // REQ-010-19: the properties of the item selected, on the right.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_properties = new PropertyPanel(m_model, this);
    auto* properties = new QDockWidget(tr("Properties"), this);
    m_units = new QLabel(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    properties->setObjectName(QStringLiteral("properties"));
    properties->setWidget(m_properties);
    // REQ-010-05: a solid polygon from the polylines selected in the drawing.
    connect(m_properties, &PropertyPanel::solidFromSelectionRequested, this, [this](quint64 id) {
        const std::vector<geom::EntityId> selected =
            m_viewport ? m_viewport->drawingSelection() : std::vector<geom::EntityId>{};
        if (const auto problem =
                m_model->setSolidFromPolylines(static_cast<setup::ItemId>(id), selected)) {
            showMessage(tr("Section from polylines"), *problem, true);
        }
    });
    addDockWidget(Qt::RightDockWidgetArea, properties);
    // REQ-010-21: the check of the model, at the bottom, shown on demand.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_check = new CheckPanel(m_model, m_tree, this);
    m_checkDock = new QDockWidget(tr("Check"), this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    m_checkDock->setObjectName(QStringLiteral("check"));
    m_checkDock->setWidget(m_check);
    addDockWidget(Qt::BottomDockWidgetArea, m_checkDock);
    m_checkDock->hide();
    connect(m_check, &CheckPanel::itemRequested, this,
            [this](quint64 id) { selectItem(static_cast<setup::ItemId>(id)); });
    connect(m_check, &CheckPanel::idsRequested, this, &MainWindow::highlightIds);
    // REQ-011-14, 15: the results, on the right with the properties.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_results = new ResultsPanel(m_model, this);
    m_resultsDock = new QDockWidget(tr("Results"), this);
    // Feature 012: the options of the display at the top of the dock.
    auto* resultsPage = new QWidget(this);
    auto* resultsLayout = new QVBoxLayout(resultsPage);
    m_resultBar = new ResultDisplayBar(resultsPage);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->addWidget(m_resultBar);
    resultsLayout->addWidget(m_results, 1);
    m_resultsDock->setObjectName(QStringLiteral("results"));
    m_resultsDock->setWidget(resultsPage);
    addDockWidget(Qt::RightDockWidgetArea, m_resultsDock);
    tabifyDockWidget(properties, m_resultsDock);
    properties->raise();
    connect(m_results, &ResultsPanel::idsRequested, this, &MainWindow::highlightIds);
    createResultDisplay();
    // REQ-010-02: the unit system always in sight.
    statusBar()->addPermanentWidget(m_units);
    connect(m_model, &ModelController::documentChanged, this, &MainWindow::showUnits);
    showUnits();
}

void MainWindow::createModelMenu()
{
    QMenu* menu = menuBar()->addMenu(tr("&Model"));
    menu->addAction(tr("&Units..."), this, &MainWindow::chooseUnits);
    menu->addSeparator();
    menu->addAction(tr("New &material..."), this, &MainWindow::newMaterial);
    menu->addAction(tr("New &section..."), this, &MainWindow::newSection);
    menu->addSeparator();
    menu->addAction(tr("Assign &bars..."), this, &MainWindow::assignBars);
    // REQ-010-08: the beam details, on bars.
    QMenu* details = menu->addMenu(tr("Beam &details"));
    details->addAction(tr("End &connections..."), this, [this] {
        assignDetail(tr("End connections"),
                     setup::ConnectionItem{.id = {}, .targets = {}, .data = {}}, 1,
                     tr("assign end connections"));
    });
    details->addAction(tr("&Offsets..."), this, [this] {
        assignDetail(tr("Rigid offsets"), setup::OffsetItem{.id = {}, .targets = {}, .data = {}}, 1,
                     tr("assign offsets"));
    });
    details->addAction(tr("&Foundation..."), this, [this] {
        assignDetail(tr("Elastic foundation"),
                     setup::FoundationItem{.id = {}, .targets = {}, .data = {}}, 1,
                     tr("assign foundation"));
    });
    details->addAction(tr("&Warping ends..."), this, [this] {
        assignDetail(tr("Warping ends"), setup::WarpingEndItem{.id = {}, .targets = {}, .data = {}},
                     1, tr("assign warping ends"));
    });
    // REQ-010-15: supports, on nodes; the shortcuts start the dialog checked.
    QMenu* supports = menu->addMenu(tr("&Supports"));
    const auto support = [this, supports](const QString& name, SupportPreset preset) {
        supports->addAction(name, this, [this, name, preset] {
            assignDetail(tr("Support: %1").arg(name).remove(QLatin1Char('&')), supportOf(preset), 0,
                         tr("assign supports"));
        });
    };
    support(tr("&Fixed..."), SupportPreset::Fixed);
    support(tr("&Pinned..."), SupportPreset::Pinned);
    support(tr("&Roller..."), SupportPreset::Roller);
    support(tr("&Custom..."), SupportPreset::Custom);

    // REQ-010-16, 17, 26: load cases, loads and combinations.
    menu->addSeparator();
    menu->addAction(tr("Load &cases..."), this, [this] { editLoadCases(this, m_model); });
    QMenu* loads = menu->addMenu(tr("Add &load"));
    const std::array<std::pair<QString, LoadKind>, 8> kinds{{
        {tr("&Nodal..."), LoadKind::Nodal},
        {tr("&Distributed..."), LoadKind::Distributed},
        {tr("&Concentrated..."), LoadKind::Concentrated},
        {tr("&Self-weight..."), LoadKind::SelfWeight},
        {tr("&Temperature..."), LoadKind::Temperature},
        {tr("&Gradient..."), LoadKind::Gradient},
        {tr("&Initial strain..."), LoadKind::InitialStrain},
        {tr("S&ettlement..."), LoadKind::Settlement},
    }};
    for (const auto& [name, kind] : kinds) {
        loads->addAction(name, this, [this, name, kind] {
            addLoad(kind, QString(name).remove(QLatin1Char('&')).remove(QStringLiteral("...")));
        });
    }
    menu->addAction(tr("New c&ombination..."), this, [this] { editCombination(std::nullopt); });

    // REQ-010-21
    menu->addSeparator();
    menu->addAction(tr("&Check model"), this, &MainWindow::checkModel);

    // REQ-010-09, 22: the symbols of the model in the viewport.
    menu->addSeparator();
    QAction* symbols = menu->addAction(tr("Show s&ymbols"));
    symbols->setCheckable(true);
    symbols->setChecked(true);
    connect(symbols, &QAction::toggled, this, [this](bool on) {
        if (!m_viewport.isNull()) {
            m_viewport->setSymbolsVisible(on);
        }
    });
    QMenu* axes = menu->addMenu(tr("Show local &axes"));
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the menu.
    auto* axesGroup = new QActionGroup(axes);
    for (const auto& [name, shown] : std::array<std::pair<QString, AxesShown>, 3>{
             {{tr("&None"), AxesShown::None},
              {tr("Of the &selection"), AxesShown::Selected},
              {tr("Of &all bars"), AxesShown::All}}}) {
        QAction* action = axes->addAction(name);
        action->setCheckable(true);
        action->setChecked(shown == AxesShown::None);
        axesGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, shown] { showLocalAxes(shown); });
    }
    connect(m_model, &ModelController::symbolsChanged, this, [this] {
        if (!m_viewport.isNull()) {
            m_viewport->setSymbols(m_model->symbols());
        }
    });
    // The axes of the selection follow it.
    const auto selectionMoved = [this] {
        if (m_model->symbolOptions().axes == AxesShown::Selected) {
            showLocalAxes(AxesShown::Selected);
        }
    };
    connect(m_viewport, &render::Viewport::selectionChanged, this, selectionMoved);
    connect(m_viewport, &render::Viewport::drawingSelectionChanged, this, selectionMoved);
    m_model->rebuildSymbols();
}

void MainWindow::createAnalysisMenu()
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_analysis = new AnalysisJob(this);
    m_analysisTimer = new QTimer(this);
    m_analysisProgress = new QProgressBar(this);
    m_analysisCancel = new QToolButton(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    m_analysisProgress->setMaximumWidth(kProgressWidth);
    m_analysisProgress->hide();
    m_analysisCancel->setText(tr("Cancel"));
    m_analysisCancel->setAutoRaise(true);
    m_analysisCancel->hide();
    statusBar()->addPermanentWidget(m_analysisProgress);
    statusBar()->addPermanentWidget(m_analysisCancel);
    m_analysisTimer->setInterval(kAnalysisProgressMs);

    QMenu* menu = menuBar()->addMenu(tr("&Analysis"));
    menu->addAction(tr("&Settings..."), this, &MainWindow::chooseAnalysis);
    menu->addSeparator();
    QAction* run = menu->addAction(tr("&Run analysis"), this, [this] {
        const auto active = m_model->document().activeCase();
        if (!active) {
            statusBar()->showMessage(tr("No load case or combination to analyse."), 5000);
            return;
        }
        runAnalysis({*active});
    });
    run->setShortcut(QKeySequence(Qt::Key_F5));
    menu->addAction(tr("Run &all"), this, [this] {
        // Every load case and combination, in the order of their ids.
        std::vector<setup::ItemId> items;
        for (const setup::Item& item : m_model->document().items()) {
            if (std::holds_alternative<setup::LoadCase>(item) ||
                std::holds_alternative<setup::Combination>(item)) {
                items.push_back(setup::idOf(item));
            }
        }
        if (items.empty()) {
            statusBar()->showMessage(tr("No load case or combination to analyse."), 5000);
            return;
        }
        runAnalysis(std::move(items));
    });
    // Esc cancels too, before the drawing sees it (the drafting controller
    // asks first); the text of the action shows it.
    m_cancelAnalysisAction = menu->addAction(tr("&Cancel analysis") + QStringLiteral("\tEsc"), this,
                                             &MainWindow::cancelAnalysis);
    m_cancelAnalysisAction->setEnabled(false);
    menu->addSeparator();
    menu->addAction(tr("Show &Results"), this, [this] {
        m_resultsDock->show();
        m_resultsDock->raise();
    });
    m_drafting->setEscapeHandler([this] {
        if (!m_analysis->isRunning()) {
            return false;
        }
        cancelAnalysis();
        return true;
    });

    connect(m_analysisCancel, &QToolButton::clicked, this, &MainWindow::cancelAnalysis);
    connect(m_analysisTimer, &QTimer::timeout, this, &MainWindow::showAnalysisProgress);
    connect(m_analysis, &AnalysisJob::caseFinished, this, &MainWindow::analysisCaseFinished);
    connect(m_analysis, &AnalysisJob::finished, this, &MainWindow::analysisFinished);
}

void MainWindow::chooseAnalysis()
{
    AnalysisSettingsDialog dialog(m_model->document().analysis(), this);
    if (dialog.exec() == QDialog::Accepted) {
        m_model->setAnalysis(dialog.settings());
    }
}

void MainWindow::runAnalysis(std::vector<setup::ItemId> items)
{
    // REQ-011-07: one run at a time.
    if (m_analysis->isRunning()) {
        statusBar()->showMessage(analysisAlreadyRunning(), 5000);
        return;
    }
    m_runItems = items;
    m_runIndex = 0;
    m_runAnalysed = 0;
    m_runNotAnalysed.clear();
    if (!m_analysis->start(m_model->document(), m_model->drawing(), m_model->mesh(),
                           std::move(items))) {
        statusBar()->showMessage(analysisAlreadyRunning(), 5000);
        return;
    }
    m_cancelAnalysisAction->setEnabled(true);
    m_analysisCancel->show();
    m_analysisProgress->setRange(0, 0);
    m_analysisProgress->show();
    m_analysisTimer->start();
    showAnalysisProgress();
}

void MainWindow::cancelAnalysis()
{
    if (m_analysis->isRunning()) {
        m_analysis->cancel();
        statusBar()->showMessage(tr("Cancelling the analysis..."));
    }
}

void MainWindow::showAnalysisProgress()
{
    if (!m_analysis->isRunning()) {
        return;
    }
    const AnalysisJob::Progress p = m_analysis->progress();
    statusBar()->showMessage(
        progressText(caseName(m_model->document(), p.item), p.index, p.count, p.core, p.building));
    // Determinate when the fraction of the stage is known.
    if (!p.building && p.core.fraction >= 0.0) {
        m_analysisProgress->setRange(0, kPercent);
        m_analysisProgress->setValue(static_cast<int>(kPercent * p.core.fraction));
    } else {
        m_analysisProgress->setRange(0, 0);
    }
}

void MainWindow::analysisCaseFinished(const setup::CaseOutcome& outcome)
{
    const setup::ItemId item =
        m_runIndex < m_runItems.size() ? m_runItems[m_runIndex] : setup::ItemId{};
    ++m_runIndex;
    const QString name = caseName(m_model->document(), item);
    m_model->caseFinished(outcome);
    const auto console = [this](const QString& line) {
        if (m_commandLine) {
            m_commandLine->appendHistory(line);
        }
    };
    if (outcome.result) {
        // REQ-011-13: the summary; the warnings of the check after it.
        ++m_runAnalysed;
        m_results->showItem(item);
        for (const QString& line :
             summaryLines(name, *outcome.result, m_model->document().units())) {
            console(line);
        }
        for (const setup::BuildIssue& warning : outcome.result->warnings) {
            console(tr("Warning: %1").arg(issueText(warning)));
        }
        return;
    }
    m_runNotAnalysed << name;
    if (outcome.error) {
        console(failureText(name, *outcome.error));
        if (outcome.error->cancelled) {
            return;
        }
        // REQ-011-10: in the check as a problem of the core, and what it
        // names shown in the mesh.
        m_check->showIssues(name, {setup::BuildIssue{.severity = setup::Severity::Error,
                                                     .kind = setup::IssueKind::Core,
                                                     .item = std::nullopt,
                                                     .detail = outcome.error->message}});
        m_checkDock->show();
        m_checkDock->raise();
        const NamedIds ids = idsInMessage(outcome.error->message);
        if (!ids.elements.empty() || !ids.nodes.empty()) {
            highlightIds(ids);
        }
        return;
    }
    // REQ-011-05: refused by the check, which opens with its problems.
    const auto errors = std::ranges::count_if(outcome.issues, [](const setup::BuildIssue& i) {
        return i.severity == setup::Severity::Error;
    });
    console(refusedText(name, static_cast<int>(errors)));
    m_check->showIssues(name, outcome.issues);
    m_checkDock->show();
    m_checkDock->raise();
}

void MainWindow::analysisFinished(bool cancelled)
{
    m_analysisTimer->stop();
    m_analysisProgress->hide();
    m_analysisCancel->hide();
    m_cancelAnalysisAction->setEnabled(false);
    if (m_runItems.size() > 1 && m_commandLine) {
        m_commandLine->appendHistory(runAllText(m_runAnalysed, m_runNotAnalysed, cancelled));
    }
    if (m_runAnalysed > 0) {
        m_resultsDock->show();
        m_resultsDock->raise();
    }
    statusBar()->showMessage(cancelled ? tr("Analysis cancelled.") : tr("Analysis done."), 5000);
}

void MainWindow::checkModel()
{
    m_checkDock->show();
    m_checkDock->raise();
    m_check->check();
}

void MainWindow::highlightIds(const NamedIds& ids)
{
    // On the mesh shown, if it is the model's (the ids are its own).
    const setup::MeshContext& mesh = m_model->mesh();
    if (m_viewport.isNull() || !mesh.mesh || !m_viewport->scene() ||
        m_viewport->scene()->sharedMesh() != mesh.mesh) {
        statusBar()->showMessage(tr("The mesh of the model is not shown."), 5000);
        return;
    }
    render::SelectionItems items;
    for (const core::ExternalId id : ids.elements) {
        if (const auto e = mesh.mesh->findElement(id)) {
            items[render::SelectionKind::Element].push_back(*e);
        }
    }
    for (const core::ExternalId id : ids.nodes) {
        if (const auto n = mesh.mesh->findNode(id)) {
            items[render::SelectionKind::Node].push_back(*n);
        }
    }
    if (!items.empty()) {
        m_viewport->setSelection(items);
    }
}

// Defined here, where ResultSink is complete.
MainWindow::~MainWindow() = default;

void MainWindow::createResultDisplay()
{
    // The viewport may go (no graphics backend): the sink then does nothing.
    class ViewportSink : public ResultSink
    {
    public:
        explicit ViewportSink(render::Viewport* viewport)
            : m_viewport(viewport)
        {}
        void setResultScene(std::shared_ptr<const render::ResultScene> scene,
                            std::shared_ptr<const std::vector<float>> values) override
        {
            if (m_viewport) {
                m_viewport->setResultScene(std::move(scene), std::move(values));
            }
        }
        void setResultValues(std::shared_ptr<const std::vector<float>> values) override
        {
            if (m_viewport) {
                m_viewport->setResultValues(std::move(values));
            }
        }
        void setResultDisplay(const render::ResultDisplay& display) override
        {
            if (m_viewport) {
                m_viewport->setResultDisplay(display);
            }
        }
        void setResultAnnotations(render::ResultAnnotations annotations) override
        {
            if (m_viewport) {
                m_viewport->setResultAnnotations(std::move(annotations));
            }
        }
        void setResultsShown(bool shown) override
        {
            if (m_viewport) {
                m_viewport->setResultsShown(shown);
            }
        }

    private:
        QPointer<render::Viewport> m_viewport;
    };

    m_resultSink = std::make_unique<ViewportSink>(m_viewport);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by this window.
    m_resultView = new ResultView(m_model, m_resultSink.get(), this);
    m_resultBar->setSettings(m_resultView->settings());
    connect(m_resultBar, &ResultDisplayBar::settingsChanged, m_resultView,
            &ResultView::setSettings);
    connect(m_resultBar, &ResultDisplayBar::shownRequested, m_resultView, &ResultView::setShown);
    connect(m_resultView, &ResultView::shownChanged, this, [this](bool shown) {
        m_resultBar->setShownState(shown);
        if (m_showResultsAction != nullptr) {
            const QSignalBlocker block(m_showResultsAction);
            m_showResultsAction->setChecked(shown);
        }
    });
    connect(m_resultView, &ResultView::availabilityChanged, this, [this](bool available) {
        m_resultBar->setAvailable(available);
        if (m_showResultsAction != nullptr) {
            m_showResultsAction->setEnabled(available && m_viewport != nullptr);
        }
    });
    // REQ-012-04: the result chosen in the dock, and the quantities it offers.
    connect(m_results, &ResultsPanel::resultChosen, this, [this](setup::ItemId item, int mode) {
        if (const setup::AnalysisResult* result = m_model->results().find(item)) {
            m_resultBar->setQuantities(quantitiesOf(*result));
        }
        m_resultView->showResult(item, mode);
    });
    // REQ-012-18: the point clicked on the result.
    connect(m_viewport, &render::Viewport::resultPicked, this,
            [this](const render::ResultPick& pick) { m_resultView->setQuery(pick); });
    connect(m_viewport, &render::Viewport::resultPickMissed, this,
            [this] { m_resultView->setQuery(std::nullopt); });
}

void MainWindow::showLocalAxes(AxesShown shown)
{
    // The rest of the options (the display of a result) stays.
    SymbolOptions options = m_model->symbolOptions();
    options.axes = shown;
    options.axesOf = {};
    if (shown == AxesShown::Selected) {
        options.axesOf = selectedTargets();
    }
    m_model->setSymbolOptions(std::move(options));
}

void MainWindow::addLoad(LoadKind kind, const QString& title)
{
    // The active load case; the first one is made when there is none.
    const setup::Document& document = m_model->document();
    const std::optional<setup::ItemId> activeCase = document.activeCase();
    const setup::Item* active = activeCase ? document.find(*activeCase) : nullptr;
    if (active != nullptr && !std::holds_alternative<setup::LoadCase>(*active)) {
        showMessage(tr("Add load"),
                    tr("A combination is active: activate the load case to add the load to."),
                    true);
        return;
    }
    std::optional<setup::ItemId> made;
    if (active == nullptr) {
        setup::LoadCase first{.id = {}, .name = {}, .standard = {}, .category = {}};
        first.name = setup::freeName(document, first, "Load case");
        made = m_model->add(std::move(first), tr("new load case"));
    }
    const std::optional<setup::ItemId> chosen = active != nullptr ? activeCase : made;
    if (!chosen) {
        return;
    }
    const setup::ItemId loadCase = *chosen;
    const bool onNodes = loadOnNodes(kind);
    const bool needsTargets = kind != LoadKind::SelfWeight;
    const setup::Targets targets = needsTargets ? selectedTargets() : setup::Targets{};
    const std::vector<setup::GroupTarget> groups = m_model->meshGroups(onNodes ? 0 : 1);
    const setup::Item item = setup::LoadItem{
        .id = {}, .loadCase = loadCase, .targets = {}, .action = newAction(kind, document.units())};
    const QString caseName =
        QString::fromStdString(setup::nameOf(*m_model->document().find(loadCase)));
    auto choice = chooseDetail(this, tr("%1 in %2").arg(title, caseName), item, m_model->document(),
                               targets, groups, needsTargets);
    if (!choice) {
        return;
    }
    auto& load = std::get<setup::LoadItem>(choice->item);
    load.targets = targets;
    if (load.targets.empty()) {
        load.targets.groups = std::move(choice->groups);
    }
    m_model->add(std::move(choice->item), tr("add load"));
}

void MainWindow::editCombination(std::optional<setup::ItemId> id)
{
    const setup::Document& document = m_model->document();
    setup::Combination start{.id = {}, .name = {}, .terms = {}};
    if (id) {
        const setup::Item* item = document.find(*id);
        const auto* existing = item != nullptr ? std::get_if<setup::Combination>(item) : nullptr;
        if (existing == nullptr) {
            return;
        }
        start = *existing;
    } else {
        start.name = setup::freeName(document, start, "Combination");
    }
    auto chosen = chooseCombination(this, document, std::move(start));
    if (!chosen) {
        return;
    }
    if (id) {
        m_model->push(setup::changeItem(m_model->document(), *chosen), tr("edit combination"));
    } else {
        m_model->add(std::move(*chosen), tr("new combination"));
    }
}

void MainWindow::assignDetail(const QString& title, setup::Item item, int dimension,
                              const QString& undo)
{
    const setup::Targets targets = selectedTargets();
    const std::vector<setup::GroupTarget> groups = m_model->meshGroups(dimension);
    auto choice = chooseDetail(this, title, std::move(item), m_model->document(), targets, groups);
    if (!choice) {
        return;
    }
    setup::Targets* chosen = setup::targetsOf(choice->item);
    if (chosen == nullptr) {
        return;
    }
    *chosen = targets;
    if (chosen->empty()) {
        chosen->groups = std::move(choice->groups);
    }
    m_model->push(setup::assign(m_model->documentForIds(), std::move(choice->item)), undo);
}

void MainWindow::selectItem(setup::ItemId id)
{
    const QModelIndex index = m_tree->indexOf(id);
    if (index.isValid()) {
        m_treeView->setCurrentIndex(index);
        m_treeView->scrollTo(index);
    }
}

setup::Targets MainWindow::selectedTargets() const
{
    if (m_viewport.isNull()) {
        return {};
    }
    if (m_viewport->selectionTarget() == render::SelectionTarget::Drawing) {
        return m_model->targetsFrom(m_viewport->drawingSelection(), {});
    }
    // Picked in the mesh shown, only if it is the model's.
    const auto& scene = m_viewport->scene();
    if (!scene || !m_model->mesh().mesh || scene->sharedMesh() != m_model->mesh().mesh) {
        return {};
    }
    render::SelectionItems picked;
    for (const render::SelectionKind kind :
         {render::SelectionKind::Element, render::SelectionKind::Node}) {
        picked[kind] = m_viewport->selection().items(kind);
    }
    return m_model->targetsFrom({}, picked);
}

void MainWindow::newMaterial()
{
    auto material = chooseMaterial(this, m_model->document().units());
    if (!material) {
        return;
    }
    material->data.name = setup::freeName(m_model->document(), *material, material->data.name);
    if (const auto id = m_model->add(std::move(*material), tr("new material"))) {
        selectItem(*id);
    }
}

void MainWindow::newSection()
{
    const auto shape = chooseSectionShape(this);
    if (!shape) {
        return;
    }
    setup::SectionItem section{.id = {}, .data = {}};
    const std::string name = setup::freeName(m_model->document(), section, "Section");
    section.data = app::newSection(name, *shape, m_model->document().units());
    const auto id = m_model->add(std::move(section), tr("new section"));
    if (!id) {
        return;
    }
    // REQ-010-05: a solid polygon takes the closed polylines selected, if any.
    if (*shape == static_cast<int>(std::variant_size_v<core::SectionShape>) - 1 &&
        !m_viewport.isNull() && !m_viewport->drawingSelection().empty()) {
        if (const auto problem =
                m_model->setSolidFromPolylines(*id, m_viewport->drawingSelection())) {
            showMessage(tr("Section from polylines"), *problem, true);
        }
    }
    selectItem(*id);
}

void MainWindow::assignBars()
{
    const setup::Targets targets = selectedTargets();
    const std::vector<setup::GroupTarget> groups = m_model->meshGroups(1);
    auto choice = chooseBars(this, m_model->document(), targets, groups);
    if (!choice) {
        return;
    }
    setup::BarAssignment bars{.id = {}, .targets = targets, .data = std::move(choice->data)};
    if (bars.targets.empty()) {
        bars.targets.groups = std::move(choice->groups);
    }
    m_model->push(setup::assign(m_model->documentForIds(), std::move(bars)), tr("assign bars"));
}

void MainWindow::showUnits()
{
    const setup::UnitNames names = setup::unitNames(m_model->document().units());
    m_units->setText(
        tr("Units: %1, %2, %3, %4, %5")
            .arg(QString::fromStdString(names.force), QString::fromStdString(names.length),
                 QString::fromStdString(names.mass), QString::fromStdString(names.time),
                 QString::fromStdString(names.temperature)));
}

void MainWindow::chooseUnits()
{
    QStringList labels;
    for (const setup::UnitSystem system : setup::kUnitSystems) {
        const setup::UnitNames n = setup::unitNames(system);
        labels.append(QStringLiteral("%1, %2, %3, %4, %5")
                          .arg(QString::fromStdString(n.force), QString::fromStdString(n.length),
                               QString::fromStdString(n.mass), QString::fromStdString(n.time),
                               QString::fromStdString(n.temperature)));
    }
    const setup::UnitSystem current = m_model->document().units();
    const auto currentIndex = static_cast<int>(std::ranges::find(setup::kUnitSystems, current) -
                                               setup::kUnitSystems.begin());
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Units"), tr("Unit system:"), labels,
                                                 currentIndex, false, &ok);
    if (!ok) {
        return;
    }
    const auto at = static_cast<std::size_t>(labels.indexOf(chosen));
    const setup::UnitSystem to = setup::kUnitSystems.at(at);
    if (to == current) {
        return;
    }
    // REQ-010-03: the values of the model are converted, the drawing is not.
    const QString length = QString::fromStdString(setup::unitNames(to).length);
    const auto answer = QMessageBox::question(
        this, tr("Change the units"),
        tr("The values of the model will be converted to the new units. The drawing is not "
           "converted: its coordinates stay as drawn, read in %1 from now on. Change the "
           "units?")
            .arg(length));
    if (answer == QMessageBox::Yes) {
        m_model->push(setup::changeUnits(m_model->document(), to), tr("change units"));
    }
}

void MainWindow::activateItem(const QModelIndex& index)
{
    // REQ-010-16: a double click on a load case or combination makes it the
    // active one.
    const auto id = m_tree->itemAt(index);
    if (!id) {
        return;
    }
    const setup::Item* item = m_model->document().find(*id);
    if (item != nullptr && (std::holds_alternative<setup::LoadCase>(*item) ||
                            std::holds_alternative<setup::Combination>(*item))) {
        m_model->push(setup::setActiveCase(m_model->document(), *id), tr("activate load case"));
    }
}

void MainWindow::highlightTargets(const QModelIndex& index)
{
    // REQ-010-19: the targets of the item selected in the tree, selected in
    // the viewport.
    const auto id = m_tree->itemAt(index);
    if (!id || m_viewport.isNull()) {
        return;
    }
    const setup::Item* item = m_model->document().find(*id);
    const setup::Targets* targets = item != nullptr ? setup::targetsOf(*item) : nullptr;
    if (targets == nullptr) {
        return;
    }
    if (!targets->entities.empty()) {
        m_viewport->setDrawingSelection(targets->entities);
    }
    // Groups and picked items, on the mesh shown, if it is the model's.
    const setup::MeshContext& mesh = m_model->mesh();
    const auto& scene = m_viewport->scene();
    if (!mesh.mesh || !scene || scene->sharedMesh() != mesh.mesh) {
        return;
    }
    render::SelectionItems items;
    auto& elements = items[render::SelectionKind::Element];
    auto& nodes = items[render::SelectionKind::Node];
    for (const setup::GroupTarget& g : targets->groups) {
        if (const core::Group* group = mesh.mesh->findGroup(g.name, g.dimension)) {
            elements.insert(elements.end(), group->elements.begin(), group->elements.end());
        }
    }
    if (targets->meshGeneration == mesh.generation) {
        for (const core::ExternalId e : targets->elements) {
            if (const auto found = mesh.mesh->findElement(e)) {
                elements.push_back(*found);
            }
        }
        for (const core::ExternalId n : targets->nodes) {
            if (const auto found = mesh.mesh->findNode(n)) {
                nodes.push_back(*found);
            }
        }
    }
    if (!items.empty()) {
        m_viewport->setSelection(items);
    }
}

void MainWindow::showModelMenu(const QPoint& position)
{
    // REQ-010-18: new, rename, duplicate, copy and delete, from the tree.
    const QModelIndex index = m_treeView->indexAt(position);
    const auto group = m_tree->groupAt(index);
    const auto id = m_tree->itemAt(index);
    const setup::Document& document = m_model->document();
    const setup::Item* item = id ? document.find(*id) : nullptr;
    QMenu menu;
    if (item == nullptr && group == ModelTree::Group::LoadCases) {
        menu.addAction(tr("New load case..."), this, [this] {
            bool ok = false;
            const QString name =
                QInputDialog::getText(this, tr("New load case"), tr("Name:"), QLineEdit::Normal,
                                      QString::fromStdString(setup::freeName(
                                          m_model->document(), setup::LoadCase{}, "Load case")),
                                      &ok);
            if (ok && !name.isEmpty()) {
                m_model->push(setup::addItem(m_model->documentForIds(),
                                             setup::LoadCase{.id = {},
                                                             .name = name.toStdString(),
                                                             .standard = {},
                                                             .category = {}}),
                              tr("new load case"));
            }
        });
    }
    if (item != nullptr) {
        const setup::ItemId itemId = id.value_or(setup::ItemId{});
        if (std::holds_alternative<setup::LoadCase>(*item) ||
            std::holds_alternative<setup::Combination>(*item)) {
            menu.addAction(tr("Activate"), this, [this, index] { activateItem(index); });
        }
        if (std::holds_alternative<setup::Combination>(*item)) {
            menu.addAction(tr("Edit factors..."), this,
                           [this, itemId] { editCombination(itemId); });
        }
        if (!setup::nameOf(*item).empty()) {
            menu.addAction(tr("Rename..."), this, [this, itemId] {
                const setup::Item* current = m_model->document().find(itemId);
                if (current == nullptr) {
                    return;
                }
                bool ok = false;
                const QString name =
                    QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal,
                                          QString::fromStdString(setup::nameOf(*current)), &ok);
                if (!ok || name.isEmpty()) {
                    return;
                }
                setup::Item renamed = *current;
                std::visit(
                    [&](auto& i) {
                        using T = std::decay_t<decltype(i)>;
                        if constexpr (std::is_same_v<T, setup::MaterialItem> ||
                                      std::is_same_v<T, setup::SectionItem>) {
                            i.data.name = name.toStdString();
                        } else if constexpr (std::is_same_v<T, setup::LoadCase> ||
                                             std::is_same_v<T, setup::Combination>) {
                            i.name = name.toStdString();
                        }
                    },
                    renamed);
                m_model->push(setup::changeItem(m_model->document(), renamed), tr("rename"));
            });
        }
        if (setup::targetsOf(*item) == nullptr || std::holds_alternative<setup::LoadItem>(*item)) {
            menu.addAction(tr("Duplicate"), this, [this, itemId] {
                m_model->push(setup::duplicateItem(m_model->documentForIds(), itemId),
                              tr("duplicate"));
            });
        }
        if (const auto* load = std::get_if<setup::LoadItem>(item)) {
            menu.addAction(tr("Copy to load case..."), this, [this, itemId, from = load->loadCase] {
                QStringList names;
                std::vector<setup::ItemId> cases;
                for (const setup::Item& other : m_model->document().items()) {
                    const auto* c = std::get_if<setup::LoadCase>(&other);
                    if (c != nullptr && c->id != from) {
                        names.append(QString::fromStdString(c->name));
                        cases.push_back(c->id);
                    }
                }
                if (cases.empty()) {
                    showMessage(tr("Copy loads"), tr("There is no other load case."), false);
                    return;
                }
                bool ok = false;
                const QString chosen = QInputDialog::getItem(
                    this, tr("Copy loads"), tr("Load case:"), names, 0, false, &ok);
                if (!ok) {
                    return;
                }
                const auto at = static_cast<std::size_t>(names.indexOf(chosen));
                const std::array<setup::ItemId, 1> loads{itemId};
                m_model->push(setup::copyLoads(m_model->documentForIds(), loads, cases.at(at)),
                              tr("copy loads"));
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, itemId] {
            m_model->push(setup::removeItem(m_model->document(), itemId), tr("delete"));
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(m_treeView->viewport()->mapToGlobal(position));
    }
}

void MainWindow::createDraftingUi()
{
    // REQ-005-04: the command line, docked at the bottom.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_commandLine = new CommandLineWidget(this);
    auto* dock = new QDockWidget(tr("Command Line"), this);
    m_drafting = new DraftingController(m_viewport, m_commandLine, this);
    // Feature 010: the model document shares the undo stack of the drawing.
    m_model = new ModelController(m_drafting->undoStack(), m_drafting->drawing(), this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    dock->setObjectName(QStringLiteral("commandLine"));
    dock->setWidget(m_commandLine);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    // Keys typed on the viewport go to the command line (as in AutoCAD).
    m_viewport->installEventFilter(this);
}

void MainWindow::createDraftingMenus()
{
    using geom::CommandName;
    const auto command = [this](QMenu* menu, const QString& text, CommandName name) {
        QAction* action =
            menu->addAction(text, m_drafting, [this, name] { m_drafting->run(name); });
        m_viewActions.append(action);
        return action;
    };

    // REQ-005-15 to 17
    QMenu* drawMenu = menuBar()->addMenu(tr("&Draw"));
    command(drawMenu, tr("&Point"), CommandName::Point);
    command(drawMenu, tr("&Line"), CommandName::Line);
    command(drawMenu, tr("P&olyline"), CommandName::Pline);

    // REQ-005-28 to 30
    QMenu* modifyMenu = menuBar()->addMenu(tr("&Modify"));
    command(modifyMenu, tr("&Move"), CommandName::Move);
    command(modifyMenu, tr("&Copy"), CommandName::Copy);
    QAction* erase =
        modifyMenu->addAction(tr("&Erase"), m_drafting, &DraftingController::deleteSelection);
    erase->setShortcut(QKeySequence::Delete);
    m_viewActions.append(erase);

    // REQ-005-21 to 24: toggles in the Tools menu and on the status bar.
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    const auto toggle = [&](const QString& text, const QString& button, Qt::Key key, bool on,
                            void (DraftingController::*slot)(bool)) {
        QAction* action = toolsMenu->addAction(text);
        action->setCheckable(true);
        action->setChecked(on);
        action->setShortcut(QKeySequence(key));
        connect(action, &QAction::toggled, m_drafting, slot);
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the status bar.
        auto* toolButton = new QToolButton(this);
        toolButton->setText(button);
        toolButton->setCheckable(true);
        toolButton->setChecked(on);
        toolButton->setAutoRaise(true);
        toolButton->setToolTip(text);
        connect(toolButton, &QToolButton::toggled, action, &QAction::setChecked);
        connect(action, &QAction::toggled, toolButton, &QToolButton::setChecked);
        statusBar()->addPermanentWidget(toolButton);
        m_viewActions.append(action);
    };
    const geom::DraftingSettings& settings = m_drafting->settings();
    toggle(tr("&Object Snap"), tr("SNAP"), Qt::Key_F3, settings.objectSnap,
           &DraftingController::setObjectSnap);
    toggle(tr("&Grid Snap"), tr("GRID"), Qt::Key_F9, settings.gridSnap,
           &DraftingController::setGridSnap);
    toggle(tr("O&rtho"), tr("ORTHO"), Qt::Key_F8, settings.ortho, &DraftingController::setOrtho);

    QMenu* modes = toolsMenu->addMenu(tr("Object Snap &Modes"));
    const std::array<std::pair<geom::SnapMode, QString>, 3> snapModes = {{
        {geom::SnapMode::Endpoint, tr("&Endpoint")},
        {geom::SnapMode::Midpoint, tr("&Midpoint")},
        {geom::SnapMode::Intersection, tr("&Intersection")},
    }};
    for (const auto& [mode, text] : snapModes) {
        QAction* action = modes->addAction(text);
        action->setCheckable(true);
        action->setChecked(settings.modes.at(static_cast<std::size_t>(mode)));
        connect(action, &QAction::toggled, m_drafting,
                [this, mode = mode](bool on) { m_drafting->setSnapMode(mode, on); });
        m_viewActions.append(action);
    }

    toolsMenu->addSeparator();
    QAction* settingsAction = toolsMenu->addAction(tr("&Drafting Settings..."), this, [this] {
        DraftingSettingsDialog dialog(m_drafting->settings().gridStep,
                                      m_drafting->settings().aperturePx, this);
        if (dialog.exec() == QDialog::Accepted) {
            m_drafting->setGridStep(dialog.gridStep());
            m_drafting->setAperture(dialog.aperturePx());
        }
    });
    m_viewActions.append(settingsAction);

    // REQ-005-24: coordinates of the point a click would give.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the status bar.
    m_coordinates = new QLabel(this);
    m_coordinates->setMinimumWidth(m_coordinates->fontMetrics().horizontalAdvance(
        QStringLiteral("-000000.0000, -000000.0000, -000000.0000")));
    statusBar()->insertPermanentWidget(0, m_coordinates);
    connect(m_drafting, &DraftingController::cursorPointChanged, m_coordinates, &QLabel::setText);
    // Feature 010 (design §5.8): the mesh of MESH and its map, for the model.
    connect(m_drafting, &DraftingController::drawingMeshed, m_model,
            &ModelController::meshGenerated);
    // REQ-009-13: the mesh of MESH, as an imported one is shown (REQ-004-08).
    connect(m_drafting, &DraftingController::meshGenerated, this,
            [this](int nodes, int elements, int groups) {
                m_meshInfo->setText(tr("Drawing: %1 nodes, %2 elements, %3 groups")
                                        .arg(nodes)
                                        .arg(elements)
                                        .arg(groups));
            });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Text typed with the viewport focused goes to the command line; Enter
    // and Space send it (REQ-005-04, 06).
    if (watched == m_viewport && event->type() == QEvent::KeyPress && m_commandLine) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool plain = (key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier)) == 0;
        if (plain && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
                      key->key() == Qt::Key_Space)) {
            m_drafting->enterText(QString());
            return true;
        }
        if (plain && key->key() == Qt::Key_Delete) {
            m_drafting->deleteSelection();
            return true;
        }
        const QString text = key->text();
        if (plain && !text.isEmpty() && text.at(0).isPrint()) {
            m_commandLine->typeText(text);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::chooseMeshFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Mesh"), QString(),
                                                      tr("Gmsh meshes (*.msh);;All files (*)"));
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

void MainWindow::meshImported(const QString& path, std::shared_ptr<const render::MeshScene> scene,
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
    // Feature 010: a new mesh generation, without a map to the drawing.
    m_model->meshImported(scene);
    // REQ-004-05
    m_viewport->setScene(std::move(scene));
    // REQ-009-15: the mesh of MESH is no longer the one shown.
    m_drafting->meshReplaced();
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

void MainWindow::showSelection()
{
    // REQ-004-27: one part per kind with items.
    if (m_viewport.isNull()) {
        return;
    }
    const render::Selection& selection = m_viewport->selection();
    QStringList parts;
    const auto add = [&](render::SelectionKind kind, const char* text) {
        const int count = selection.count(kind);
        if (count > 0) {
            parts.append(tr(text, nullptr, count));
        }
    };
    add(render::SelectionKind::Element, QT_TR_NOOP("%n element(s)"));
    add(render::SelectionKind::Face, QT_TR_NOOP("%n face(s)"));
    add(render::SelectionKind::Edge, QT_TR_NOOP("%n edge(s)"));
    add(render::SelectionKind::Node, QT_TR_NOOP("%n node(s)"));
    if (parts.isEmpty()) {
        m_selectionInfo->clear();
        return;
    }
    m_selectionInfo->setText(tr("Selected: %1").arg(parts.join(QStringLiteral(", "))));
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
