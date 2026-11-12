#include "mesh_import.hpp"

#include <fem/io/gmsh_reader.hpp>

#include <QThread>

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace fem::app {

MeshImport::MeshImport(QObject* parent)
    : QObject(parent)
{}

void MeshImport::start(const QString& path)
{
    if (m_running) {
        return;
    }
    m_running = true;
    emit started(path);

    // REQ-003-24: through UTF-16, so that accents survive on every platform.
    const std::filesystem::path file(path.toStdU16String());

    // Filled on the worker thread, read back on the thread of this object
    // once the worker has finished.
    struct Result
    {
        std::shared_ptr<const render::MeshScene> scene;
        QStringList warnings;
        QString error;
    };
    auto result = std::make_shared<Result>();

    // NOLINTNEXTLINE(bugprone-exception-escape): only allocation failures can escape.
    QThread* thread = QThread::create([result, file] {
        try {
            auto read = io::readGmsh(file);
            if (!read) {
                result->error = QString::fromStdString(read.error().message);
                return;
            }
            for (const std::string& warning : read->warnings) {
                result->warnings.push_back(QString::fromStdString(warning));
            }
            result->scene =
                render::buildMeshScene(std::make_shared<const core::Mesh>(std::move(read->mesh)));
        } catch (const std::exception& exception) {
            result->error = QString::fromUtf8(exception.what());
        } catch (...) {
            result->error = QStringLiteral("Unexpected error while importing the mesh.");
        }
    });
    // Queued: QThread::finished is emitted on the worker thread.
    connect(thread, &QThread::finished, this, [this, path, result] {
        m_running = false;
        if (result->scene) {
            emit finished(path, result->scene, result->warnings);
        } else {
            emit failed(path, result->error);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace fem::app
