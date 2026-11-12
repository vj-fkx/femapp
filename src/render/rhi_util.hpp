#pragma once

// Small helpers shared by the parts of the renderer that talk to QRhi.

#include <fem/render/camera.hpp>

#include <QMatrix4x4>
#include <QString>

#include <rhi/qrhi.h>

namespace fem::render::detail {

inline constexpr int kMatrixFloats = 16;
inline constexpr int kVectorFloats = 4;

[[nodiscard]] QMatrix4x4 toQt(const Mat4& matrix);

// Camera projections use a [0, 1] depth range. QRhi expects OpenGL-style clip
// space (depth in [-1, 1]) as the input of its correction matrix.
[[nodiscard]] Mat4 toOpenGlDepth(const Mat4& projection);

void copyMatrix(const QMatrix4x4& matrix, float* destination);

// Loads ":/fem/render/shaders/<name>.qsb"; on failure sets `error`.
[[nodiscard]] QShader loadShader(const QString& name, QString* error);

[[nodiscard]] QRhiGraphicsPipeline::TargetBlend alphaBlend();

} // namespace fem::render::detail
