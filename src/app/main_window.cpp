#include "main_window.hpp"

#include "command_line_widget.hpp"
#include "drafting_controller.hpp"
#include "drafting_settings_dialog.hpp"
#include "mesh_import.hpp"
#include "model_controller.hpp"
#include "model_tree.hpp"
#include "property_panel.hpp"

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
#include <QProgressBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QUndoStack>
#include <QUrl>

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
    addDockWidget(Qt::RightDockWidgetArea, properties);
    // REQ-010-02: the unit system always in sight.
    statusBar()->addPermanentWidget(m_units);
    connect(m_model, &ModelController::documentChanged, this, &MainWindow::showUnits);
    showUnits();
}

void MainWindow::createModelMenu()
{
    QMenu* menu = menuBar()->addMenu(tr("&Model"));
    menu->addAction(tr("&Units..."), this, &MainWindow::chooseUnits);
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
                                             setup::LoadCase{.id = {}, .name = name.toStdString()}),
                              tr("new load case"));
            }
        });
    }
    if (item != nullptr) {
        const setup::ItemId itemId = *id;
        if (std::holds_alternative<setup::LoadCase>(*item) ||
            std::holds_alternative<setup::Combination>(*item)) {
            menu.addAction(tr("Activate"), this, [this, index] { activateItem(index); });
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
