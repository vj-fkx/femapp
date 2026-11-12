#include "rhi_util.hpp"

#include <QFile>

#include <cstring>

namespace fem::render::detail {

QMatrix4x4 toQt(const Mat4& matrix)
{
    QMatrix4x4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result(row, column) = static_cast<float>(matrix(row, column));
        }
    }
    return result;
}

Mat4 toOpenGlDepth(const Mat4& projection)
{
    Mat4 remap = Mat4::Identity();
    remap(2, 2) = 2.0;
    remap(2, 3) = -1.0;
    return remap * projection;
}

void copyMatrix(const QMatrix4x4& matrix, float* destination)
{
    std::memcpy(destination, matrix.constData(), kMatrixFloats * sizeof(float));
}

QShader loadShader(const QString& name, QString* error)
{
    QFile file(QStringLiteral(":/fem/render/shaders/") + name + QStringLiteral(".qsb"));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("shader '%1' is missing from the resources").arg(name);
        return {};
    }
    QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid()) {
        *error = QStringLiteral("shader '%1' is not valid").arg(name);
    }
    return shader;
}

QRhiGraphicsPipeline::TargetBlend alphaBlend()
{
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    // Keeps the destination alpha at 1, so the widget stays opaque.
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    return blend;
}

} // namespace fem::render::detail
