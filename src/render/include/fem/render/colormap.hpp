#pragma once

// Feature 012 (design §5.5; REQ-012-11, 12): the colors of a result: a
// scale from blue (the minimum) through cyan, green and yellow to red (the
// maximum), continuous or in bands. The legend and the shader use the same
// rules.

#include <fem/render/mesh_scene.hpp>

#include <QImage>

namespace fem::render {

// The color at t in [0, 1] (clamped): blue, cyan, green, yellow and red at
// 0, 1/4, 1/2, 3/4 and 1, linear in between; alpha 255.
[[nodiscard]] Rgba colormapColor(double t) noexcept;

// The band of a value among `bands` equal bands of [min, max]:
// floor((value - min) / (max - min) * bands), clamped to [0, bands - 1]. A
// value on a limit goes to the band above it, and max to the last. 0 when
// max <= min or bands < 1.
[[nodiscard]] int bandOf(double value, double min, double max, int bands) noexcept;

// The color drawn for a value: continuous (bands < 1) or the color of the
// centre of its band. Outside [min, max], the color of the nearest end; not
// finite, noValueColor().
[[nodiscard]] Rgba valueColor(double value, double min, double max, int bands) noexcept;

// The color of a sample without a value: the 1D color of the mesh.
[[nodiscard]] Rgba noValueColor() noexcept;

// The texture of the shader: `width` x 1 RGBA8, pixel i the color at
// i / (width - 1).
[[nodiscard]] QImage colormapImage(int width = 256);

} // namespace fem::render
