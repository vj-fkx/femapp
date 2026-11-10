#include "axis_labels.hpp"

#include "axis_gizmo_mesh.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRect>
#include <QString>

#include <cstddef>

namespace fem::render {

namespace {

constexpr int kGlyphCount = 3;
constexpr double kGlyphHeightFraction = 0.8;

} // namespace

QImage buildAxisLabelAtlas(int cellSizePx)
{
    QImage atlas(cellSizePx * kGlyphCount, cellSizePx, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);

    QPainter painter(&atlas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(static_cast<int>(cellSizePx * kGlyphHeightFraction));
    painter.setFont(font);
    painter.setPen(QColor(Qt::white));

    const std::array<QString, kGlyphCount> glyphs = {QStringLiteral("X"), QStringLiteral("Y"),
                                                     QStringLiteral("Z")};
    for (int i = 0; i < kGlyphCount; ++i) {
        const QRect cell(i * cellSizePx, 0, cellSizePx, cellSizePx);
        painter.drawText(cell, Qt::AlignCenter, glyphs.at(static_cast<std::size_t>(i)));
    }
    painter.end();
    return atlas;
}

std::vector<LabelVertex> buildAxisLabelQuads()
{
    struct Corner
    {
        float x;
        float y;
    };
    // Two triangles; corner (-1, 1) is the top-left of the glyph cell.
    constexpr std::array<Corner, 6> kCorners = {{
        {.x = -1.0F, .y = 1.0F},
        {.x = -1.0F, .y = -1.0F},
        {.x = 1.0F, .y = -1.0F},
        {.x = -1.0F, .y = 1.0F},
        {.x = 1.0F, .y = -1.0F},
        {.x = 1.0F, .y = 1.0F},
    }};
    const std::array<std::array<float, 3>, kGlyphCount> colors = {kAxisColorX, kAxisColorY,
                                                                  kAxisColorZ};

    std::vector<LabelVertex> quads;
    quads.reserve(kCorners.size() * kGlyphCount);
    for (int glyph = 0; glyph < kGlyphCount; ++glyph) {
        std::array<float, 3> anchor = {0.0F, 0.0F, 0.0F};
        anchor.at(static_cast<std::size_t>(glyph)) = kLabelAnchorDistance;

        for (const Corner& corner : kCorners) {
            // Texture rows start at the top of the image.
            const float u = (static_cast<float>(glyph) + (corner.x + 1.0F) * 0.5F) /
                            static_cast<float>(kGlyphCount);
            const float v = (1.0F - corner.y) * 0.5F;
            quads.push_back({.anchor = anchor,
                             .corner = {corner.x, corner.y},
                             .texCoord = {u, v},
                             .color = colors.at(static_cast<std::size_t>(glyph))});
        }
    }
    return quads;
}

} // namespace fem::render
