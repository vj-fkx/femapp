#pragma once

#include <Eigen/Core>
#include <cstdint>
#include <optional>

namespace fem::render {

using Vec2 = Eigen::Vector2d;
using Vec3 = Eigen::Vector3d;
using Mat4 = Eigen::Matrix4d;

enum class Projection : std::uint8_t { Perspective, Orthographic };

enum class StandardView : std::uint8_t { Front, Back, Left, Right, Top, Bottom, Isometric };

// Axis-aligned bounding box.
struct Aabb
{
    Vec3 min = Vec3::Zero();
    Vec3 max = Vec3::Zero();
};

struct Ray
{
    Vec3 origin = Vec3::Zero();
    Vec3 direction = Vec3::UnitZ(); // unit length
};

// Region framed when there is nothing else to show: the default grid area.
[[nodiscard]] Aabb defaultSceneBounds();

// Turntable camera: it looks at a target point from a given distance, with the
// world Z axis always pointing up on screen. All math is in double precision
// and independent of Qt and of the GPU.
//
// Pixel coordinates have their origin at the top-left corner, Y pointing down.
class Camera
{
public:
    // Starts in the isometric view, perspective projection, framing
    // defaultSceneBounds().
    Camera();

    // Ignored when either dimension is not positive.
    void setViewportSize(int widthPx, int heightPx);
    [[nodiscard]] int viewportWidth() const noexcept { return m_width; }
    [[nodiscard]] int viewportHeight() const noexcept { return m_height; }

    // Rotates around the target. Positive deltaX moves the scene to the right.
    void orbit(double deltaXPx, double deltaYPx);
    // Moves the target to the depth of `pivot` along the viewing direction,
    // without changing the image, so that a following orbit turns around the
    // point at the centre of the screen (REQ-002-13). In perspective the eye
    // stays put; in orthographic the distance (the scale) is kept. Ignored for
    // a pivot behind or too close to the eye.
    void setPivot(const Vec3& pivot);
    // Moves the view so that the point under `fromPx` ends up under `toPx`.
    void pan(const Vec2& fromPx, const Vec2& toPx);
    // Zooms keeping the point under the cursor fixed. Positive steps zoom in.
    void zoomAt(const Vec2& cursorPx, double wheelSteps);
    void setStandardView(StandardView view);
    void setProjection(Projection projection) noexcept { m_projection = projection; }
    // Frames `bounds`; an empty or invalid box frames defaultSceneBounds().
    void fit(const Aabb& bounds);

    [[nodiscard]] Projection projection() const noexcept { return m_projection; }
    [[nodiscard]] const Vec3& target() const noexcept { return m_target; }
    [[nodiscard]] double distance() const noexcept { return m_distance; }
    [[nodiscard]] double yaw() const noexcept { return m_yaw; }
    [[nodiscard]] double pitch() const noexcept { return m_pitch; }

    [[nodiscard]] Vec3 forward() const; // viewing direction
    [[nodiscard]] Vec3 right() const;
    [[nodiscard]] Vec3 up() const;
    [[nodiscard]] Vec3 position() const;

    // Size of one pixel, in world units, at the target.
    [[nodiscard]] double worldUnitsPerPixel() const;

    // View matrix for coordinates given relative to the target. Its entries
    // stay small whatever the target is, so it converts to float safely.
    [[nodiscard]] Mat4 viewMatrixRelativeToTarget() const;
    [[nodiscard]] Mat4 viewMatrix() const;
    // Right-handed, depth in [0, 1], Y up in clip space.
    [[nodiscard]] Mat4 projectionMatrix() const;

    [[nodiscard]] Ray rayThrough(const Vec2& pixel) const;
    // Point under `pixel` on the plane through the target that faces the camera.
    [[nodiscard]] Vec3 focalPlanePoint(const Vec2& pixel) const;
    // Point under `pixel` on the work plane Z = 0, when the ray meets it in
    // front of the camera and within the depth range; nullopt otherwise.
    [[nodiscard]] std::optional<Vec3> workPlanePoint(const Vec2& pixel) const;
    // Point that zoom and pan keep under the cursor: workPlanePoint() when it
    // exists, focalPlanePoint() otherwise.
    [[nodiscard]] Vec3 anchorPoint(const Vec2& pixel) const;

    static constexpr double kMinDistance = 1e-6;
    static constexpr double kMaxDistance = 1e12;

private:
    [[nodiscard]] double aspectRatio() const noexcept;
    [[nodiscard]] double tanHalfFov() const noexcept;

    Vec3 m_target = Vec3::Zero();
    double m_yaw = 0.0;   // radians; azimuth of the camera around Z
    double m_pitch = 0.0; // radians; elevation, in [-pi/2, pi/2]
    double m_distance = 1.0;
    Projection m_projection = Projection::Perspective;
    int m_width = 1;
    int m_height = 1;
};

} // namespace fem::render
