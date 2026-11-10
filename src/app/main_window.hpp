#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QString>

class QAction;

namespace fem::render {
class Viewport;
} // namespace fem::render

namespace fem::app {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // False when no graphics backend could be initialized and the window shows
    // a message in place of the 3D viewport.
    [[nodiscard]] bool isViewportAvailable() const;

signals:
    void viewportFrameRendered();
    void viewportUnavailable(const QString& reason);

private slots:
    void showAbout();
    void showViewportFailure(const QString& reason);

private:
    void createViewMenu();

    QPointer<render::Viewport> m_viewport;
    QList<QAction*> m_viewActions;
};

} // namespace fem::app
