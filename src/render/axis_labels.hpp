#pragma once

#include <QImage>

#include <array>
#include <vector>

namespace fem::render {

// Vertex of an axis label: a camera-facing quad anchored at an arrow tip.
struct LabelVertex
{
    std::array<float, 3> anchor;
    std::array<float, 2> corner; // -1..1, offset from the anchor
    std::array<float, 2> texCoord;
    std::array<float, 3> color;
};

// Distance of the label anchors from the origin, in arrow lengths.
inline constexpr float kLabelAnchorDistance = 1.22F;

// Atlas with the glyphs "X", "Y" and "Z" side by side: white on transparent.
// `cellSizePx` is the side of each square cell, in pixels.
[[nodiscard]] QImage buildAxisLabelAtlas(int cellSizePx);

// Three quads (two triangles each) that map the atlas cells to the arrow tips.
[[nodiscard]] std::vector<LabelVertex> buildAxisLabelQuads();

} // namespace fem::render
