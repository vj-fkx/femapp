#pragma once

#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace fem::render {

// Rectangle dragged on the screen, in pixels. `to.x < from.x` means crossing.
struct PickRect
{
    Vec2 from = Vec2::Zero();
    Vec2 to = Vec2::Zero();

    [[nodiscard]] bool isCrossing() const noexcept { return to.x() < from.x(); }
};

// Click and rectangle selection against a MeshScene, computed on the CPU
// (ADR 0003). Elements and nodes are returned by internal index.
class MeshPicker
{
public:
    explicit MeshPicker(std::shared_ptr<const MeshScene> scene);

    // REQ-004-20: the element (or node) under `pixel` closest to the camera.
    // Lines, points and nodes count within `tolerancePx`; hidden nodes do not.
    [[nodiscard]] std::optional<int> pick(const Camera& camera, const Vec2& pixel,
                                          SelectionMode mode, const DisplayOptions& display,
                                          double tolerancePx) const;

    // REQ-002-13: world position of the closest visible face under `pixel`,
    // used as the orbit pivot.
    [[nodiscard]] std::optional<Eigen::Vector3d>
    surfacePoint(const Camera& camera, const Vec2& pixel, const DisplayOptions& display) const;

    // REQ-004-21, 22: window (entirely inside) or crossing (inside or
    // crossing), including items hidden behind others. Sorted.
    [[nodiscard]] std::vector<int> pickRect(const Camera& camera, const PickRect& rect,
                                            SelectionMode mode,
                                            const DisplayOptions& display) const;

private:
    std::shared_ptr<const MeshScene> m_scene;
};

} // namespace fem::render
