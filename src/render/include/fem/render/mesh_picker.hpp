#pragma once

#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

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

// An item picked by a click (REQ-004-20); `index` as in SelectionItems.
struct PickedItem
{
    SelectionKind kind = SelectionKind::Element;
    int index = -1;
};

// Click and rectangle selection against a MeshScene, computed on the CPU
// (ADR 0003). Elements and nodes are returned by internal index.
class MeshPicker
{
public:
    explicit MeshPicker(std::shared_ptr<const MeshScene> scene);

    // REQ-004-20: one item under `pixel`, of a kind turned on in `filter`, by
    // priority: the closest visible element (lines and points within
    // `tolerancePx`), else the closest visible solid face, else a visible
    // edge within `tolerancePx`, else a visible node within `tolerancePx`.
    [[nodiscard]] std::optional<PickedItem> pick(const Camera& camera, const Vec2& pixel,
                                                 const SelectionFilter& filter,
                                                 const DisplayOptions& display,
                                                 double tolerancePx) const;

    // REQ-002-13: world position of the closest visible face under `pixel`,
    // used as the orbit pivot.
    [[nodiscard]] std::optional<Eigen::Vector3d>
    surfacePoint(const Camera& camera, const Vec2& pixel, const DisplayOptions& display) const;

    // REQ-002-13: centre of the bounding box of the visible surface nodes that
    // fall inside the screen, in front of the camera; the orbit pivot.
    [[nodiscard]] std::optional<Eigen::Vector3d> visibleCenter(const Camera& camera,
                                                               const DisplayOptions& display) const;

    // REQ-004-21, 22: window (entirely inside) or crossing (inside or
    // crossing), including items hidden behind others, for every kind turned
    // on in `filter`. Each list sorted.
    [[nodiscard]] SelectionItems pickRect(const Camera& camera, const PickRect& rect,
                                          const SelectionFilter& filter,
                                          const DisplayOptions& display) const;

private:
    std::shared_ptr<const MeshScene> m_scene;
};

} // namespace fem::render
