#pragma once

#include <fem/render/mesh_scene.hpp>

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace fem::app {

// Reads a .msh file and builds its scene on a worker thread, so that the
// interface stays responsive (REQ-004-04). Results come back on the thread
// of this object.
class MeshImport : public QObject
{
    Q_OBJECT

public:
    explicit MeshImport(QObject* parent = nullptr);

    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

    // Starts reading `path`; ignored while another import is running.
    void start(const QString& path);

signals:
    void started(const QString& path);
    void finished(const QString& path, std::shared_ptr<const fem::render::MeshScene> scene,
                  const QStringList& warnings);
    void failed(const QString& path, const QString& message);

private:
    bool m_running = false;
};

} // namespace fem::app
