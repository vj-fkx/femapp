#pragma once

#include <fem/render/display_options.hpp>

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

class QAction;
class QEvent;
class QLabel;
class QProgressBar;

namespace fem::render {
class MeshScene;
class Viewport;
} // namespace fem::render

namespace fem::app {

class CommandLineWidget;
class DraftingController;
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
    void showMessage(const QString& title, const QString& text, bool error);

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
    bool m_quietMessages = false;
};

} // namespace fem::app
