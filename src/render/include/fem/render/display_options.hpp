#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fem::render {

// REQ-004-11
enum class DisplayMode : std::uint8_t { ShadedWithEdges, Shaded, Wireframe };

// REQ-004-14
enum class ColorMode : std::uint8_t { ByDimension, ByGroup };

// REQ-004-19, 32: what can be selected. Faces and edges are those of the outer
// surface of the solids (MeshSurface::faces3d and edges3d).
enum class SelectionKind : std::uint8_t { Element, Face, Edge, Node };
inline constexpr std::size_t kSelectionKindCount = 4;
inline constexpr std::array<SelectionKind, kSelectionKindCount> kSelectionKinds = {
    SelectionKind::Element, SelectionKind::Face, SelectionKind::Edge, SelectionKind::Node};

[[nodiscard]] constexpr std::size_t toIndex(SelectionKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

// REQ-004-19: the kinds turned on; they can be combined.
struct SelectionFilter
{
    std::array<bool, kSelectionKindCount> enabled = {true, false, false, false};

    [[nodiscard]] bool isEnabled(SelectionKind kind) const noexcept
    {
        return enabled[toIndex(kind)];
    }
    [[nodiscard]] int enabledCount() const noexcept
    {
        int count = 0;
        for (const bool on : enabled) {
            count += on ? 1 : 0;
        }
        return count;
    }
};

struct DisplayOptions
{
    DisplayMode mode = DisplayMode::ShadedWithEdges;
    ColorMode color = ColorMode::ByDimension;
    // REQ-004-16: visibility of the elements of each dimension, 0D to 3D.
    std::array<bool, 4> dimensionVisible = {true, true, true, true};
    // REQ-004-34: draw the nodes of the visible surface.
    bool showNodes = false;

    [[nodiscard]] bool isVisible(int dimension) const noexcept
    {
        return dimension >= 0 && dimension < 4 &&
               dimensionVisible[static_cast<std::size_t>(dimension)];
    }
};

} // namespace fem::render
