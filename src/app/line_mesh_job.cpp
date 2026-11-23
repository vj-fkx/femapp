#include "line_mesh_job.hpp"

#include <QThread>

#include <exception>
#include <memory>
#include <optional>
#include <utility>

namespace fem::app {

LineMeshJob::LineMeshJob(QObject* parent)
    : QObject(parent)
{}

bool LineMeshJob::start(const geom::Drawing& drawing, const mesher::LineMeshOptions& options)
{
    if (m_running) {
        return false;
    }
    m_running = true;

    // Filled on the worker thread, read back on the thread of this object
    // once the worker has finished.
    struct Result
    {
        geom::Drawing drawing;
        std::uint64_t revision = 0;
        mesher::LineMeshOptions options;
        std::shared_ptr<const render::MeshScene> scene;
        mesher::LineMeshReport report;
        std::shared_ptr<const mesher::LineMeshMap> map;
        std::optional<mesher::LineMeshError> error;
    };
    auto result = std::make_shared<Result>();
    result->drawing = drawing;
    result->revision = drawing.revision();
    result->options = options;

    // NOLINTNEXTLINE(bugprone-exception-escape): only allocation failures can escape.
    QThread* thread = QThread::create([result] {
        auto meshed = mesher::meshLines(result->drawing, result->options);
        if (!meshed) {
            result->error = std::move(meshed.error());
            return;
        }
        result->report = std::move(meshed->report);
        result->map = std::make_shared<const mesher::LineMeshMap>(std::move(meshed->map));
        result->scene =
            render::buildMeshScene(std::make_shared<const core::Mesh>(std::move(meshed->mesh)));
    });
    // Queued: QThread::finished is emitted on the worker thread.
    connect(thread, &QThread::finished, this, [this, result] {
        m_running = false;
        if (result->scene) {
            emit finished(result->scene, result->report, result->map, result->revision);
        } else {
            emit failed(result->error.value_or(mesher::LineMeshError{}));
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

} // namespace fem::app
