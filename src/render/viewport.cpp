#include "scene_renderer.hpp"

#include <fem/render/backend.hpp>
#include <fem/render/viewport.hpp>

#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPointF>
#include <QWheelEvent>

#include <rhi/qrhi.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fem::render {

namespace {

Q_LOGGING_CATEGORY(lcRender, "fem.render")

constexpr int kSampleCount = 4;
constexpr double kWheelDegreesPerStep = 15.0;
constexpr double kWheelUnitsPerDegree = 8.0;

QRhiWidget::Api toApi(Backend backend)
{
    switch (backend) {
    case Backend::Direct3D11:
        return QRhiWidget::Api::Direct3D11;
    case Backend::Direct3D12:
        return QRhiWidget::Api::Direct3D12;
    case Backend::Metal:
        return QRhiWidget::Api::Metal;
    case Backend::Vulkan:
        return QRhiWidget::Api::Vulkan;
    case Backend::OpenGL:
        return QRhiWidget::Api::OpenGL;
    case Backend::Null:
        return QRhiWidget::Api::Null;
    }
    return QRhiWidget::Api::Null;
}

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

Vec2 toVec2(const QPointF& point)
{
    return {point.x(), point.y()};
}

enum class DragMode : std::uint8_t { None, Pan, Orbit };

} // namespace

class Viewport::Impl
{
public:
    Camera camera;
    SceneRenderer renderer;

    BackendPlan plan;
    std::size_t attempt = 0;
    bool available = true;
    QString unavailableReason;
    QString backendName;

    QRhi* rhi = nullptr;
    bool initialFitDone = false;
    bool showGrid = true;
    bool showAxes = true;
    quint64 frameCount = 0;

    DragMode dragMode = DragMode::None;
    QPointF lastMousePosition;
};

Viewport::Viewport(QWidget* parent)
    : QRhiWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    const QByteArray overrideName = qgetenv("FEM_RHI_BACKEND");
    m_impl->plan = planBackends(currentPlatform(), overrideName.toStdString());
    if (!m_impl->plan.warning.empty()) {
        qCWarning(lcRender, "%s", m_impl->plan.warning.c_str());
    }

    if (m_impl->plan.attempts.empty()) {
        m_impl->available = false;
        m_impl->unavailableReason = tr("No graphics backend is available.");
    } else {
        // Chosen before the widget is shown: switching the API later forces
        // the window to be re-created.
        setApi(toApi(m_impl->plan.attempts.front()));
    }
    setSampleCount(kSampleCount);

    connect(this, &QRhiWidget::renderFailed, this,
            [this] { tryNextBackend(tr("The graphics backend could not be initialized.")); });
}

Viewport::~Viewport() = default;

bool Viewport::isAvailable() const
{
    return m_impl->available;
}

QString Viewport::unavailableReason() const
{
    return m_impl->unavailableReason;
}

QString Viewport::backendName() const
{
    return m_impl->backendName;
}

quint64 Viewport::frameCount() const
{
    return m_impl->frameCount;
}

bool Viewport::isGridVisible() const
{
    return m_impl->showGrid;
}

bool Viewport::isAxesVisible() const
{
    return m_impl->showAxes;
}

Projection Viewport::projection() const
{
    return m_impl->camera.projection();
}

const Camera& Viewport::camera() const
{
    return m_impl->camera;
}

void Viewport::setGridVisible(bool visible)
{
    if (m_impl->showGrid != visible) {
        m_impl->showGrid = visible;
        update();
    }
}

void Viewport::setAxesVisible(bool visible)
{
    if (m_impl->showAxes != visible) {
        m_impl->showAxes = visible;
        update();
    }
}

void Viewport::setStandardView(StandardView view)
{
    m_impl->camera.setStandardView(view);
    update();
}

void Viewport::setProjection(Projection projection)
{
    if (m_impl->camera.projection() != projection) {
        m_impl->camera.setProjection(projection);
        update();
    }
}

void Viewport::fitAll()
{
    // There is no scene content yet, so the default region is framed.
    m_impl->camera.setViewportSize(width(), height());
    m_impl->camera.fit(defaultSceneBounds());
    update();
}

void Viewport::tryNextBackend(const QString& failure)
{
    if (!m_impl->available) {
        return;
    }

    const std::vector<Backend>& attempts = m_impl->plan.attempts;
    if (m_impl->attempt < attempts.size()) {
        qCWarning(lcRender, "%s backend failed: %s",
                  std::string(backendDisplayName(attempts.at(m_impl->attempt))).c_str(),
                  qUtf8Printable(failure));
    }

    ++m_impl->attempt;
    if (m_impl->attempt < attempts.size()) {
        m_impl->renderer.release();
        m_impl->rhi = nullptr;
        setApi(toApi(attempts.at(m_impl->attempt)));
        update();
        return;
    }

    m_impl->available = false;
    m_impl->unavailableReason = failure;
    m_impl->backendName.clear();
    emit backendUnavailable(failure);
}

void Viewport::initialize(QRhiCommandBuffer* /*commandBuffer*/)
{
    if (!m_impl->available || m_impl->rhi == rhi()) {
        return;
    }

    // First call, or the QRhi was re-created (e.g. the widget moved to
    // another top-level window).
    m_impl->rhi = rhi();
    if (!m_impl->renderer.initialize(rhi(), renderTarget()->renderPassDescriptor(),
                                     renderTarget()->sampleCount())) {
        const QString failure = m_impl->renderer.errorString();
        m_impl->rhi = nullptr;
        tryNextBackend(failure);
        return;
    }

    m_impl->backendName = toQString(backendDisplayName(m_impl->plan.attempts.at(m_impl->attempt)));
    emit backendChanged(m_impl->backendName);
}

void Viewport::render(QRhiCommandBuffer* commandBuffer)
{
    if (!m_impl->available || !m_impl->renderer.isInitialized()) {
        return;
    }

    // The camera works in logical pixels, the unit of the mouse events.
    m_impl->camera.setViewportSize(width(), height());
    if (!m_impl->initialFitDone) {
        m_impl->camera.fit(defaultSceneBounds());
        m_impl->initialFitDone = true;
    }

    const FrameParams params{.camera = &m_impl->camera,
                             .pixelSize = renderTarget()->pixelSize(),
                             .devicePixelRatio = devicePixelRatioF(),
                             .showGrid = m_impl->showGrid,
                             .showAxes = m_impl->showAxes};
    m_impl->renderer.render(commandBuffer, renderTarget(), params);

    ++m_impl->frameCount;
    emit frameRendered();
}

void Viewport::releaseResources()
{
    m_impl->renderer.release();
    m_impl->rhi = nullptr;
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::MiddleButton) {
        QRhiWidget::mousePressEvent(event);
        return;
    }
    m_impl->dragMode =
        event->modifiers().testFlag(Qt::ShiftModifier) ? DragMode::Orbit : DragMode::Pan;
    m_impl->lastMousePosition = event->position();
    event->accept();
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (m_impl->dragMode == DragMode::None) {
        QRhiWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF position = event->position();
    const QPointF delta = position - m_impl->lastMousePosition;
    if (delta.isNull()) {
        return;
    }

    m_impl->camera.setViewportSize(width(), height());
    if (m_impl->dragMode == DragMode::Orbit) {
        m_impl->camera.orbit(delta.x(), delta.y());
    } else {
        m_impl->camera.pan(toVec2(m_impl->lastMousePosition), toVec2(position));
    }
    m_impl->lastMousePosition = position;
    event->accept();
    update();
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton && m_impl->dragMode != DragMode::None) {
        m_impl->dragMode = DragMode::None;
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event)
{
    // angleDelta() is in eighths of a degree; one mouse wheel notch is 15 degrees.
    const double steps = event->angleDelta().y() / (kWheelUnitsPerDegree * kWheelDegreesPerStep);
    if (steps == 0.0) {
        QRhiWidget::wheelEvent(event);
        return;
    }

    m_impl->camera.setViewportSize(width(), height());
    m_impl->camera.zoomAt(toVec2(event->position()), steps);
    event->accept();
    update();
}

} // namespace fem::render
