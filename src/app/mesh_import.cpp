#include "mesh_import.hpp"

#include <fem/io/gmsh_reader.hpp>

#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <filesystem>
#include <string>
#include <utility>

namespace fem::app {

MeshImport::MeshImport(QObject* parent)
    : QObject(parent)
{
}

void MeshImport::start(const QString& path)
{
    if (m_running) {
        return;
    }
    m_running = true;
    emit started(path);

    const QPointer<MeshImport> self(this);
    // REQ-003-24: through UTF-16, so that accents survive on every platform.
    const std::filesystem::path file(path.toStdU16String());

    QThread* thread = QThread::create([self, path, file] {
        auto result = io::readGmsh(file);
        std::shared_ptr<const render::MeshScene> scene;
        QStringList warnings;
        QString error;
        if (result) {
            for (const std::string& warning : result->warnings) {
                warnings.push_back(QString::fromStdString(warning));
            }
            scene = render::buildMeshScene(
                std::make_shared<const core::Mesh>(std::move(result->mesh)));
        } else {
            error = QString::fromStdString(result.error().message);
        }

        // Back on the thread of the MeshImport object.
        QMetaObject::invokeMethod(
            self.data(),
            [self, path, scene = std::move(scene), warnings = std::move(warnings),
             error = std::move(error)] {
                if (self.isNull()) {
                    return;
                }
                self->m_running = false;
                if (scene) {
                    emit self->finished(path, scene, warnings);
                } else {
                    emit self->failed(path, error);
                }
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace fem::app
