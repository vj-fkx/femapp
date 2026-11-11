#include <fem/render/camera.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace fem::render {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kHalfPi = kPi / 2.0;
constexpr double kDegToRad = kPi / 180.0;

constexpr double kFovY = 45.0 * kDegToRad;
constexpr double kOrbitRadPerPixel = 0.5 * kDegToRad;
constexpr double kZoomBase = 1.15;
constexpr double kFitMargin = 1.1;
constexpr double kDepthRangeFactor = 100.0;
constexpr double kDefaultRegionHalfSize = 10.0;

// Elevation of the isometric view: atan(1 / sqrt(2)).
const double kIsometricPitch = std::atan(1.0 / std::numbers::sqrt2);

double wrapAngle(double angle)
{
    return std::remainder(angle, 2.0 * kPi);
}

std::array<Vec3, 8> corners(const Aabb& box)
{
    const Vec3& lo = box.min;
    const Vec3& hi = box.max;
    return {Vec3(lo.x(), lo.y(), lo.z()), Vec3(hi.x(), lo.y(), lo.z()),
            Vec3(lo.x(), hi.y(), lo.z()), Vec3(hi.x(), hi.y(), lo.z()),
            Vec3(lo.x(), lo.y(), hi.z()), Vec3(hi.x(), lo.y(), hi.z()),
            Vec3(lo.x(), hi.y(), hi.z()), Vec3(hi.x(), hi.y(), hi.z())};
}

bool isUsable(const Aabb& box)
{
    const bool finite = box.min.allFinite() && box.max.allFinite();
    const bool ordered = (box.max.array() >= box.min.array()).all();
    return finite && ordered && (box.max - box.min).norm() > 0.0;
}

} // namespace

Aabb defaultSceneBounds()
{
    return {.min = Vec3(-kDefaultRegionHalfSize, -kDefaultRegionHalfSize, 0.0),
            .max = Vec3(kDefaultRegionHalfSize, kDefaultRegionHalfSize, 0.0)};
}

Camera::Camera()
{
    setStandardView(StandardView::Isometric);
    fit(defaultSceneBounds());
}

void Camera::setViewportSize(int widthPx, int heightPx)
{
    if (widthPx <= 0 || heightPx <= 0) {
        return;
    }
    m_width = widthPx;
    m_height = heightPx;
}

void Camera::orbit(double deltaXPx, double deltaYPx)
{
    m_yaw = wrapAngle(m_yaw - kOrbitRadPerPixel * deltaXPx);
    m_pitch = std::clamp(m_pitch + kOrbitRadPerPixel * deltaYPx, -kHalfPi, kHalfPi);
}

void Camera::pan(const Vec2& fromPx, const Vec2& toPx)
{
    m_target -= focalPlanePoint(toPx) - focalPlanePoint(fromPx);
}

void Camera::zoomAt(const Vec2& cursorPx, double wheelSteps)
{
    const double newDistance =
        std::clamp(m_distance * std::pow(kZoomBase, -wheelSteps), kMinDistance, kMaxDistance);
    const double ratio = newDistance / m_distance;

    // Scaling the target towards the cursor point by the same ratio as the
    // distance keeps that point on the same pixel.
    const Vec3 anchor = focalPlanePoint(cursorPx);
    m_target = anchor + (m_target - anchor) * ratio;
    m_distance = newDistance;
}

void Camera::setStandardView(StandardView view)
{
    switch (view) {
    case StandardView::Front:
        m_yaw = -kHalfPi;
        m_pitch = 0.0;
        break;
    case StandardView::Back:
        m_yaw = kHalfPi;
        m_pitch = 0.0;
        break;
    case StandardView::Right:
        m_yaw = 0.0;
        m_pitch = 0.0;
        break;
    case StandardView::Left:
        m_yaw = kPi;
        m_pitch = 0.0;
        break;
    case StandardView::Top:
        m_yaw = -kHalfPi;
        m_pitch = kHalfPi;
        break;
    case StandardView::Bottom:
        m_yaw = -kHalfPi;
        m_pitch = -kHalfPi;
        break;
    case StandardView::Isometric:
        m_yaw = -kPi / 4.0;
        m_pitch = kIsometricPitch;
        break;
    }
}

void Camera::fit(const Aabb& bounds)
{
    const Aabb box = isUsable(bounds) ? bounds : defaultSceneBounds();
    const Vec3 center = 0.5 * (box.min + box.max);

    const Vec3 r = right();
    const Vec3 u = up();
    const Vec3 f = forward();
    const double tanV = tanHalfFov();
    const double tanH = tanV * aspectRatio();

    // Smallest distance at which every corner is inside the frame. In
    // perspective, a corner at depth z (along the viewing direction, relative
    // to the centre) and lateral offset x needs |x| <= (distance + z) * tan;
    // in the orthographic projection the depth does not matter.
    // The margin widens the lateral offsets, so the limiting corner always
    // ends up at 1 / kFitMargin of the half-frame.
    const bool perspective = m_projection == Projection::Perspective;
    double required = 0.0;
    for (const Vec3& corner : corners(box)) {
        const Vec3 offset = corner - center;
        const double depth = perspective ? offset.dot(f) : 0.0;
        required = std::max(required, kFitMargin * std::abs(offset.dot(r)) / tanH - depth);
        required = std::max(required, kFitMargin * std::abs(offset.dot(u)) / tanV - depth);
    }

    m_target = center;
    m_distance = std::clamp(required, kMinDistance, kMaxDistance);
}

Vec3 Camera::forward() const
{
    const double cosPitch = std::cos(m_pitch);
    return {-cosPitch * std::cos(m_yaw), -cosPitch * std::sin(m_yaw), -std::sin(m_pitch)};
}

Vec3 Camera::right() const
{
    // Written directly from the yaw so that it stays defined when looking
    // straight down or up, where a cross product with Z would vanish.
    return {-std::sin(m_yaw), std::cos(m_yaw), 0.0};
}

Vec3 Camera::up() const
{
    return right().cross(forward());
}

Vec3 Camera::position() const
{
    return m_target - m_distance * forward();
}

double Camera::worldUnitsPerPixel() const
{
    return 2.0 * m_distance * tanHalfFov() / static_cast<double>(m_height);
}

Mat4 Camera::viewMatrixRelativeToTarget() const
{
    const Vec3 r = right();
    const Vec3 u = up();
    const Vec3 f = forward();

    Mat4 view = Mat4::Identity();
    view.block<1, 3>(0, 0) = r.transpose();
    view.block<1, 3>(1, 0) = u.transpose();
    view.block<1, 3>(2, 0) = -f.transpose();
    // The eye sits at -distance * forward from the target.
    view(2, 3) = -m_distance;
    return view;
}

Mat4 Camera::viewMatrix() const
{
    Mat4 translation = Mat4::Identity();
    translation.block<3, 1>(0, 3) = -m_target;
    return viewMatrixRelativeToTarget() * translation;
}

Mat4 Camera::projectionMatrix() const
{
    const double tanV = tanHalfFov();
    const double aspect = aspectRatio();

    Mat4 projection = Mat4::Zero();
    if (m_projection == Projection::Perspective) {
        const double zNear = m_distance / kDepthRangeFactor;
        const double zFar = m_distance * kDepthRangeFactor;
        projection(0, 0) = 1.0 / (tanV * aspect);
        projection(1, 1) = 1.0 / tanV;
        projection(2, 2) = zFar / (zNear - zFar);
        projection(2, 3) = zNear * zFar / (zNear - zFar);
        projection(3, 2) = -1.0;
    } else {
        // The visible height at the target matches the perspective one.
        const double halfHeight = m_distance * tanV;
        const double zNear = -m_distance * kDepthRangeFactor;
        const double zFar = m_distance * kDepthRangeFactor;
        projection(0, 0) = 1.0 / (halfHeight * aspect);
        projection(1, 1) = 1.0 / halfHeight;
        projection(2, 2) = 1.0 / (zNear - zFar);
        projection(2, 3) = zNear / (zNear - zFar);
        projection(3, 3) = 1.0;
    }
    return projection;
}

Ray Camera::rayThrough(const Vec2& pixel) const
{
    const double ndcX = 2.0 * pixel.x() / static_cast<double>(m_width) - 1.0;
    const double ndcY = 1.0 - 2.0 * pixel.y() / static_cast<double>(m_height);
    const double tanV = tanHalfFov();
    const double tanH = tanV * aspectRatio();

    const Vec3 lateral = right() * (ndcX * tanH) + up() * (ndcY * tanV);
    if (m_projection == Projection::Perspective) {
        return {.origin = position(), .direction = (forward() + lateral).normalized()};
    }
    return {.origin = position() + lateral * m_distance, .direction = forward()};
}

Vec3 Camera::focalPlanePoint(const Vec2& pixel) const
{
    const Ray ray = rayThrough(pixel);
    const Vec3 f = forward();
    // The ray never runs parallel to the focal plane: its direction always has
    // a positive component along the viewing direction.
    const double t = (m_target - ray.origin).dot(f) / ray.direction.dot(f);
    return ray.origin + t * ray.direction;
}

double Camera::aspectRatio() const noexcept
{
    return static_cast<double>(m_width) / static_cast<double>(m_height);
}

double Camera::tanHalfFov() const noexcept
{
    return std::tan(kFovY / 2.0);
}

} // namespace fem::render
