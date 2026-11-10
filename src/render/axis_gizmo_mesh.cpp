#include "axis_gizmo_mesh.hpp"

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fem::render {

namespace {

constexpr float kShaftRadius = 0.035F;
constexpr float kShaftLength = 0.70F;
constexpr float kHeadRadius = 0.095F;
constexpr float kArrowLength = 1.0F;

using Vec3f = Eigen::Vector3f;

// Maps arrow-local coordinates (the arrow runs along local X) to the axis.
Vec3f toAxis(const Vec3f& local, int axis)
{
    switch (axis) {
    case 0:
        return local;
    case 1:
        return {local.z(), local.x(), local.y()};
    default:
        return {local.y(), local.z(), local.x()};
    }
}

void addVertex(std::vector<GizmoVertex>& mesh, const Vec3f& position, const Vec3f& normal, int axis,
               const std::array<float, 3>& color)
{
    const Vec3f p = toAxis(position, axis);
    const Vec3f n = toAxis(normal, axis).normalized();
    mesh.push_back(
        {.position = {p.x(), p.y(), p.z()}, .normal = {n.x(), n.y(), n.z()}, .color = color});
}

void addArrow(std::vector<GizmoVertex>& mesh, int axis, const std::array<float, 3>& color)
{
    const Vec3f back(-1.0F, 0.0F, 0.0F);
    // Normal of the cone surface: tilted forward by the cone's half-angle.
    const float headLength = kArrowLength - kShaftLength;
    const float slope = kHeadRadius / headLength;

    for (int side = 0; side < kGizmoArrowSides; ++side) {
        const float a0 = 2.0F * std::numbers::pi_v<float> * static_cast<float>(side) /
                         static_cast<float>(kGizmoArrowSides);
        const float a1 = 2.0F * std::numbers::pi_v<float> * static_cast<float>(side + 1) /
                         static_cast<float>(kGizmoArrowSides);
        const Vec3f d0(0.0F, std::cos(a0), std::sin(a0));
        const Vec3f d1(0.0F, std::cos(a1), std::sin(a1));
        const Vec3f along(1.0F, 0.0F, 0.0F);

        const Vec3f shaftStart0 = d0 * kShaftRadius;
        const Vec3f shaftStart1 = d1 * kShaftRadius;
        const Vec3f shaftEnd0 = shaftStart0 + along * kShaftLength;
        const Vec3f shaftEnd1 = shaftStart1 + along * kShaftLength;

        // Shaft side.
        addVertex(mesh, shaftStart0, d0, axis, color);
        addVertex(mesh, shaftEnd0, d0, axis, color);
        addVertex(mesh, shaftEnd1, d1, axis, color);
        addVertex(mesh, shaftStart0, d0, axis, color);
        addVertex(mesh, shaftEnd1, d1, axis, color);
        addVertex(mesh, shaftStart1, d1, axis, color);

        // Shaft cap at the origin.
        addVertex(mesh, Vec3f::Zero(), back, axis, color);
        addVertex(mesh, shaftStart1, back, axis, color);
        addVertex(mesh, shaftStart0, back, axis, color);

        const Vec3f headBase0 = d0 * kHeadRadius + along * kShaftLength;
        const Vec3f headBase1 = d1 * kHeadRadius + along * kShaftLength;
        const Vec3f tip = along * kArrowLength;

        // Head side.
        const Vec3f n0 = d0 + along * slope;
        const Vec3f n1 = d1 + along * slope;
        addVertex(mesh, headBase0, n0, axis, color);
        addVertex(mesh, tip, n0 + n1, axis, color);
        addVertex(mesh, headBase1, n1, axis, color);

        // Head cap.
        addVertex(mesh, along * kShaftLength, back, axis, color);
        addVertex(mesh, headBase1, back, axis, color);
        addVertex(mesh, headBase0, back, axis, color);
    }
}

} // namespace

std::vector<GizmoVertex> buildAxisGizmoMesh()
{
    std::vector<GizmoVertex> mesh;
    mesh.reserve(static_cast<std::size_t>(kGizmoVerticesPerArrow) * 3);
    addArrow(mesh, 0, kAxisColorX);
    addArrow(mesh, 1, kAxisColorY);
    addArrow(mesh, 2, kAxisColorZ);
    return mesh;
}

} // namespace fem::render
