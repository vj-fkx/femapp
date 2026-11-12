#include "scene_renderer.hpp"
#include "selection_overlay.hpp"

#include <fem/render/backend.hpp>
#include <fem/render/mesh_picker.hpp>
#include <fem/render/viewport.hpp>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPointF>
#include <QWheelEvent>
#include <QWindow>

#include <rhi/qrhi.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fem::render {

namespace {

Q_LOGGING_CATEGORY(lcRender, "fem.render")

constexpr int kSampleCount = 4;
constexpr double kWheelDegreesPerStep = 15.0;
constexpr double kWheelUnitsPerDegree = 8.0;
// A left-button press that moves less than this is a click (design §5.3).
constexpr double kDragThresholdPx = 4.0;
constexpr double kPickTolerancePx = 5.0;

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
    bool showGrid = false; // REQ-002-26: off by default; the axis lines stay
    bool showAxes = true;
    quint64 frameCount = 0;

    DragMode dragMode = DragMode::None;
    QPointF lastMousePosition;

    // Mesh and selection.
    std::shared_ptr<const MeshScene> scene;
    std::unique_ptr<MeshPicker> picker;
    DisplayOptions display;
    Selection selection;
    std::uint64_t sceneVersion = 0;
    std::uint64_t colorVersion = 0;
    std::uint64_t nodeVersion = 0;
    SelectionFilter filter;
    std::uint64_t shownSceneVersion = 0;

    // Left button: click or rectangle.
    bool leftPressed = false;
    bool rectangle = false;
    QPointF pressPosition;
    SelectionOverlay* overlay = nullptr;
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
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by its Qt parent.
    m_impl->overlay = new SelectionOverlay(this);

    connect(this, &QRhiWidget::renderFailed, this, &Viewport::handleRenderFailed);
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
    // REQ-004-17: the mesh if there is one, else the default grid region.
    m_impl->camera.setViewportSize(width(), height());
    if (m_impl->scene && !m_impl->scene->bounds().isEmpty()) {
        const core::BoundingBox& box = m_impl->scene->bounds();
        m_impl->camera.fit({.min = box.min, .max = box.max});
    } else {
        m_impl->camera.fit(defaultSceneBounds());
    }
    update();
}

void Viewport::setScene(std::shared_ptr<const MeshScene> scene)
{
    m_impl->scene = std::move(scene);
    ++m_impl->sceneVersion;
    ++m_impl->colorVersion;
    if (m_impl->scene) {
        m_impl->picker = std::make_unique<MeshPicker>(m_impl->scene);
        m_impl->selection.reset(m_impl->scene.get());
    } else {
        m_impl->picker.reset();
        m_impl->selection.reset(nullptr);
    }
    m_impl->initialFitDone = true;
    // REQ-004-36: a flat mesh without solids is seen straight on.
    if (m_impl->scene) {
        if (const auto view = planarView(*m_impl->scene)) {
            m_impl->camera.setStandardView(*view);
        }
    }
    fitAll();
    emit selectionChanged();
}

const std::shared_ptr<const MeshScene>& Viewport::scene() const
{
    return m_impl->scene;
}

const DisplayOptions& Viewport::displayOptions() const
{
    return m_impl->display;
}

const Selection& Viewport::selection() const
{
    return m_impl->selection;
}

void Viewport::setDisplayMode(DisplayMode mode)
{
    if (m_impl->display.mode != mode) {
        m_impl->display.mode = mode;
        update();
    }
}

void Viewport::setColorMode(ColorMode mode)
{
    if (m_impl->display.color != mode) {
        m_impl->display.color = mode;
        ++m_impl->colorVersion;
        update();
    }
}

void Viewport::setDimensionVisible(int dimension, bool visible)
{
    if (dimension < 0 || dimension > 3 || m_impl->display.isVisible(dimension) == visible) {
        return;
    }
    m_impl->display.dimensionVisible.at(static_cast<std::size_t>(dimension)) = visible;
    // REQ-004-28: hidden elements leave the selection.
    if (!visible && m_impl->scene) {
        const int before = m_impl->selection.total();
        m_impl->selection.removeElementsOfDimension(*m_impl->scene, dimension);
        if (m_impl->selection.total() != before) {
            selectionEdited();
        }
    }
    ++m_impl->nodeVersion; // the nodes shown follow the visible dimensions
    update();
}

const SelectionFilter& Viewport::selectionFilter() const
{
    return m_impl->filter;
}

bool Viewport::setSelectionKindEnabled(SelectionKind kind, bool enabled)
{
    bool& slot = m_impl->filter.enabled.at(toIndex(kind));
    if (slot == enabled) {
        return true;
    }
    // REQ-004-19: at least one kind stays on.
    if (!enabled && m_impl->filter.enabledCount() == 1) {
        return false;
    }
    slot = enabled;
    // REQ-004-33: a kind turned off leaves the selection.
    if (!enabled && m_impl->selection.count(kind) > 0) {
        m_impl->selection.clear(kind);
        selectionEdited();
    }
    return true;
}

void Viewport::setNodesShown(bool shown)
{
    if (m_impl->display.showNodes != shown) {
        m_impl->display.showNodes = shown;
        ++m_impl->nodeVersion;
        update();
    }
}

void Viewport::clearSelection()
{
    if (m_impl->selection.total() > 0) {
        m_impl->selection.clear();
        selectionEdited();
    }
}

void Viewport::selectionEdited()
{
    ++m_impl->colorVersion;
    update();
    emit selectionChanged();
}

void Viewport::finishPick(const QPointF& releasePosition, bool remove)
{
    if (!m_impl->picker) {
        return;
    }
    m_impl->camera.setViewportSize(width(), height());
    SelectionItems items;
    if (m_impl->rectangle) {
        const PickRect rect{.from = toVec2(m_impl->pressPosition), .to = toVec2(releasePosition)};
        items = m_impl->picker->pickRect(m_impl->camera, rect, m_impl->filter, m_impl->display);
    } else if (const auto item =
                   m_impl->picker->pick(m_impl->camera, toVec2(releasePosition), m_impl->filter,
                                        m_impl->display, kPickTolerancePx)) {
        items[item->kind].push_back(item->index);
    } else if (!remove) {
        // REQ-004-25: a plain click off the mesh clears the selection.
        clearSelection();
        return;
    }
    if (items.empty()) {
        return;
    }
    if (remove) {
        m_impl->selection.remove(items);
    } else {
        m_impl->selection.add(items);
    }
    selectionEdited();
}

void Viewport::handleRenderFailed()
{
    if (!m_impl->available) {
        return;
    }

    // "Surface type 0" (raster) here means the top-level window was never
    // switched to GPU composition; see the note in MainWindow's constructor.
    const QWindow* topLevel = window()->windowHandle();
    qCWarning(lcRender,
              "no QRhi for the viewport (platform %s, window surface type %d, exposed %d)",
              qUtf8Printable(QGuiApplication::platformName()),
              topLevel != nullptr ? static_cast<int>(topLevel->surfaceType()) : -1,
              static_cast<int>(topLevel != nullptr && topLevel->isExposed()));
    tryNextBackend(tr("The graphics backend could not be initialized."));
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

    MeshFrame mesh;
    mesh.scene = m_impl->scene.get();
    mesh.sceneVersion = m_impl->sceneVersion;
    mesh.colorVersion = m_impl->colorVersion;
    mesh.nodeVersion = m_impl->nodeVersion;
    mesh.display = m_impl->display;
    mesh.selection = &m_impl->selection;
    const FrameParams params{.camera = &m_impl->camera,
                             .pixelSize = renderTarget()->pixelSize(),
                             .devicePixelRatio = devicePixelRatioF(),
                             .showGrid = m_impl->showGrid,
                             .showAxes = m_impl->showAxes,
                             .mesh = mesh};
    if (!m_impl->renderer.render(commandBuffer, renderTarget(), params)) {
        tryNextBackend(m_impl->renderer.errorString());
        return;
    }

    ++m_impl->frameCount;
    emit frameRendered();
    if (m_impl->scene && m_impl->shownSceneVersion != m_impl->sceneVersion) {
        m_impl->shownSceneVersion = m_impl->sceneVersion;
        emit sceneShown();
    }
}

void Viewport::releaseResources()
{
    m_impl->renderer.release();
    m_impl->rhi = nullptr;
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_impl->leftPressed = true;
        m_impl->rectangle = false;
        m_impl->pressPosition = event->position();
        event->accept();
        return;
    }
    if (event->button() != Qt::MiddleButton) {
        QRhiWidget::mousePressEvent(event);
        return;
    }
    m_impl->dragMode =
        event->modifiers().testFlag(Qt::ShiftModifier) ? DragMode::Orbit : DragMode::Pan;
    if (m_impl->dragMode == DragMode::Orbit) {
        // REQ-002-13: orbit around the point at the centre of the screen: the
        // mesh there, else the work plane, else the current target.
        m_impl->camera.setViewportSize(width(), height());
        const Vec2 centre(0.5 * width(), 0.5 * height());
        std::optional<Vec3> pivot;
        if (m_impl->picker) {
            pivot = m_impl->picker->surfacePoint(m_impl->camera, centre, m_impl->display);
        }
        if (!pivot) {
            pivot = m_impl->camera.workPlanePoint(centre);
        }
        if (pivot) {
            m_impl->camera.setPivot(*pivot);
        }
    }
    m_impl->lastMousePosition = event->position();
    event->accept();
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
    if (m_impl->leftPressed) {
        const QPointF delta = event->position() - m_impl->pressPosition;
        if (!m_impl->rectangle && std::hypot(delta.x(), delta.y()) > kDragThresholdPx) {
            m_impl->rectangle = true;
        }
        if (m_impl->rectangle) {
            // REQ-004-23: right to left is a crossing.
            const bool crossing = event->position().x() < m_impl->pressPosition.x();
            m_impl->overlay->showRect(
                QRect(m_impl->pressPosition.toPoint(), event->position().toPoint()), crossing);
        }
        event->accept();
        return;
    }
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
    if (event->button() == Qt::LeftButton && m_impl->leftPressed) {
        m_impl->leftPressed = false;
        m_impl->overlay->hideRect();
        // REQ-004-24: Shift removes instead of adding.
        finishPick(event->position(), event->modifiers().testFlag(Qt::ShiftModifier));
        m_impl->rectangle = false;
        event->accept();
        return;
    }
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
    // REQ-002-14: anchored on the mesh under the cursor when there is one, so
    // that zooming in never goes through it.
    const Vec2 cursor = toVec2(event->position());
    std::optional<Vec3> anchor;
    if (m_impl->picker) {
        anchor = m_impl->picker->surfacePoint(m_impl->camera, cursor, m_impl->display);
    }
    m_impl->camera.zoomAt(cursor, steps, anchor);
    event->accept();
    update();
}

} // namespace fem::render
