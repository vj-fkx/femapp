#pragma once

#include <array>
#include <vector>

namespace fem::render {

// Vertex of the axis indicator arrows. Plain floats: it goes straight to the GPU.
struct GizmoVertex
{
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 3> color;
};

inline constexpr int kGizmoArrowSides = 12;
// Per arrow: shaft side (2 triangles per side), shaft cap, head side, head cap.
inline constexpr int kGizmoVerticesPerArrow = kGizmoArrowSides * 5 * 3;

inline constexpr std::array<float, 3> kAxisColorX = {0.86F, 0.24F, 0.24F};
inline constexpr std::array<float, 3> kAxisColorY = {0.30F, 0.72F, 0.30F};
inline constexpr std::array<float, 3> kAxisColorZ = {0.27F, 0.47F, 0.92F};

// Three unit-length arrows from the origin along +X (red), +Y (green) and
// +Z (blue), as a non-indexed triangle list.
[[nodiscard]] std::vector<GizmoVertex> buildAxisGizmoMesh();

} // namespace fem::render
