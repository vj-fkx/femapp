#pragma once

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/render/mesh_scene.hpp>

#include <QObject>

#include <cstdint>
#include <memory>

namespace fem::app {

// REQ-009-13: meshes a copy of the drawing and builds its scene on a worker
// thread, so that the interface stays responsive, as MeshImport does for a
// .msh file. Results come back on the thread of this object.
class LineMeshJob : public QObject
{
    Q_OBJECT

public:
    explicit LineMeshJob(QObject* parent = nullptr);

    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

    // Copies the drawing and its revision; ignored (returns false) while
    // another job is running.
    bool start(const geom::Drawing& drawing, const mesher::LineMeshOptions& options);

signals:
    // `revision` is the revision of the drawing that was meshed; `map` ties
    // the mesh to the drawing (feature 010, design §5.8).
    void finished(std::shared_ptr<const fem::render::MeshScene> scene,
                  const fem::mesher::LineMeshReport& report,
                  std::shared_ptr<const fem::mesher::LineMeshMap> map, std::uint64_t revision);
    void failed(const fem::mesher::LineMeshError& error);

private:
    bool m_running = false;
};

} // namespace fem::app
