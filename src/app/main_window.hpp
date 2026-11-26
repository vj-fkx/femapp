#pragma once

#include "check_issues.hpp"
#include "fields.hpp"
#include "symbol_builder.hpp"

#include <fem/render/display_options.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/document.hpp>

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

class QDockWidget;

class QAction;
class QEvent;
class QLabel;
class QModelIndex;
class QPoint;
class QProgressBar;
class QTimer;
class QToolButton;
class QTreeView;

namespace fem::render {
class MeshScene;
class Viewport;
} // namespace fem::render

namespace fem::app {

class AnalysisJob;
class ResultsPanel;
class CommandLineWidget;
class DraftingController;
class ModelController;
class ModelTree;
class PropertyPanel;
class CheckPanel;
class MeshImport;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // False when no graphics backend could be initialized and the window shows
    // a message in place of the 3D viewport.
    [[nodiscard]] bool isViewportAvailable() const;

    // Smoke test: messages go to the standard output instead of dialogs.
    void setQuietMessages(bool quiet) noexcept { m_quietMessages = quiet; }

public slots:
    // REQ-004-01 to 03: imports a .msh file in the background.
    void importMesh(const QString& path);

signals:
    void viewportFrameRendered();
    void viewportUnavailable(const QString& reason);
    // The imported mesh appeared on screen.
    void meshShown();
    void meshImportFailed(const QString& message);

private slots:
    void showAbout();
    void showViewportFailure(const QString& reason);
    void chooseMeshFile();
    void meshImported(const QString& path, std::shared_ptr<const fem::render::MeshScene> scene,
                      const QStringList& warnings);
    void meshFailed(const QString& path, const QString& message);
    void showSelection();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createViewMenu();
    void createEditMenu();
    void createDraftingUi();
    void createDraftingMenus();
    // Feature 010: the model tree dock, its menu and the highlight of the
    // targets of the item selected.
    void createModelUi();
    void createModelMenu();
    // REQ-010-03: the unit system, chosen and converted after a warning.
    void chooseUnits();
    // REQ-010-04, 05, 07: the items made from the Model menu.
    void newMaterial();
    void newSection();
    void assignBars();
    // REQ-010-08, 15: a beam detail or a support, from `item` as the
    // dialog starts it, on the selection or the groups of `dimension`.
    void assignDetail(const QString& title, setup::Item item, int dimension, const QString& undo);
    // REQ-010-16: a load in the active load case.
    void addLoad(LoadKind kind, const QString& title);
    // REQ-010-09: the local axes shown, of the selection or of all.
    void showLocalAxes(AxesShown shown);
    // REQ-010-21: the check of the model in its dock, and what a problem of
    // the core names shown in the mesh.
    void checkModel();
    void highlightIds(const NamedIds& ids);
    // REQ-010-17: a new combination, or the terms of `id` edited.
    void editCombination(std::optional<setup::ItemId> id);
    // The new item selected in the tree, for its properties.
    void selectItem(setup::ItemId id);
    // REQ-010-12: the targets of the viewport selection.
    [[nodiscard]] setup::Targets selectedTargets() const;
    void showUnits();
    void showModelMenu(const QPoint& position);
    void activateItem(const QModelIndex& index);
    void highlightTargets(const QModelIndex& index);
    void showMessage(const QString& title, const QString& text, bool error);
    // Feature 011: the Analysis menu, the progress on the status bar and
    // what each case gives in the console and the check (REQ-011-01, 03 to
    // 08, 10, 13, 17).
    void createAnalysisMenu();
    void chooseAnalysis();
    void runAnalysis(std::vector<setup::ItemId> items);
    void cancelAnalysis();
    void showAnalysisProgress();
    void analysisCaseFinished(const setup::CaseOutcome& outcome);
    void analysisFinished(bool cancelled);

    QPointer<render::Viewport> m_viewport;
    QList<QAction*> m_viewActions;
    MeshImport* m_import = nullptr;
    QAction* m_importAction = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_meshInfo = nullptr;
    QLabel* m_selectionInfo = nullptr;
    QLabel* m_coordinates = nullptr;
    QPointer<CommandLineWidget> m_commandLine;
    DraftingController* m_drafting = nullptr;
    // Feature 010: the model document and the mesh it is built on.
    ModelController* m_model = nullptr;
    ModelTree* m_tree = nullptr;
    QTreeView* m_treeView = nullptr;
    PropertyPanel* m_properties = nullptr;
    CheckPanel* m_check = nullptr;
    QDockWidget* m_checkDock = nullptr;
    QLabel* m_units = nullptr;
    // Feature 011: the analysis job and the run in progress.
    AnalysisJob* m_analysis = nullptr;
    QTimer* m_analysisTimer = nullptr;
    QProgressBar* m_analysisProgress = nullptr;
    QToolButton* m_analysisCancel = nullptr;
    QAction* m_cancelAnalysisAction = nullptr;
    ResultsPanel* m_results = nullptr; // REQ-011-14
    QDockWidget* m_resultsDock = nullptr;
    std::vector<setup::ItemId> m_runItems;
    std::size_t m_runIndex = 0;
    int m_runAnalysed = 0;
    QStringList m_runNotAnalysed;
    bool m_quietMessages = false;
};

} // namespace fem::app
