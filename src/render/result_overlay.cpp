#include "result_overlay.hpp"

#include "pick_util.hpp"

#include <fem/render/colormap.hpp>

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace fem::render {

namespace {

const QColor kTextColor(235, 237, 240);
const QColor kBoxColor(20, 22, 26, 200);
const QColor kWarningColor(255, 160, 60);
constexpr double kMarginPx = 12.0;
constexpr double kPaddingPx = 4.0;
constexpr double kBarWidthPx = 18.0;
constexpr double kBarHeightPx = 200.0;
constexpr double kAnchorRadiusPx = 3.0;
constexpr int kBoxAlpha = 255;

QColor toQColor(Rgba c)
{
    return {static_cast<int>(c & 0xFFU), static_cast<int>((c >> 8U) & 0xFFU),
            static_cast<int>((c >> 16U) & 0xFFU), kBoxAlpha};
}

} // namespace

ResultOverlay::ResultOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void ResultOverlay::setAnnotations(ResultAnnotations annotations)
{
    m_annotations = std::move(annotations);
    update();
}

void ResultOverlay::setFrame(const Camera& camera, double scale, bool shown)
{
    m_camera = camera;
    m_scale = scale;
    m_shown = shown;
    if (!shown) {
        hide();
        return;
    }
    setGeometry(parentWidget()->rect());
    show();
    raise();
    update();
}

void ResultOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (!m_shown) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    paintLabels(painter);
    paintHeadline(painter);
    if (m_annotations.legend) {
        paintLegend(painter, *m_annotations.legend);
    }
}

void ResultOverlay::paintLegend(QPainter& painter, const ResultLegend& legend) const
{
    const QFontMetricsF metrics(font());
    double textWidth = metrics.horizontalAdvance(legend.title);
    for (const auto& tick : legend.ticks) {
        textWidth =
            std::max(textWidth, metrics.horizontalAdvance(tick.second) + kBarWidthPx + kPaddingPx);
    }
    const double boxWidth = textWidth + 2 * kPaddingPx;
    const double boxHeight = kBarHeightPx + metrics.height() * 2 + 3 * kPaddingPx;
    const QRectF box(width() - kMarginPx - boxWidth, height() - kMarginPx - boxHeight, boxWidth,
                     boxHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBoxColor);
    painter.drawRoundedRect(box, kPaddingPx, kPaddingPx);

    painter.setPen(kTextColor);
    painter.drawText(QPointF(box.left() + kPaddingPx, box.top() + kPaddingPx + metrics.ascent()),
                     legend.title);
    // The bar, from the minimum (bottom) to the maximum (top), pixel by
    // pixel in the colors of the shader (valueColor).
    const double top = box.top() + 2 * kPaddingPx + metrics.height() + metrics.height() / 2;
    const double left = box.left() + kPaddingPx;
    const int rows = static_cast<int>(kBarHeightPx);
    for (int row = 0; row < rows; ++row) {
        const double t = 1.0 - (row + 0.5) / rows;
        const double value = legend.min + t * (legend.max - legend.min);
        painter.fillRect(QRectF(left, top + row, kBarWidthPx, 1.0),
                         toQColor(valueColor(value, legend.min, legend.max, legend.bands)));
    }
    const double span = legend.max - legend.min;
    for (const auto& [value, text] : legend.ticks) {
        const double t = span > 0.0 ? (value - legend.min) / span : 0.0;
        const double y = top + (1.0 - t) * kBarHeightPx;
        painter.drawLine(QPointF(left + kBarWidthPx, y), QPointF(left + kBarWidthPx + 3, y));
        painter.drawText(QPointF(left + kBarWidthPx + kPaddingPx + 2,
                                 y + (metrics.ascent() - metrics.descent()) / 2),
                         text);
    }
}

void ResultOverlay::paintHeadline(QPainter& painter) const
{
    const QStringList& lines = m_annotations.headline;
    if (lines.isEmpty()) {
        return;
    }
    const QFontMetricsF metrics(font());
    double y = kMarginPx + metrics.ascent();
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const bool warning = m_annotations.warning && i == lines.size() - 1;
        painter.setPen(warning ? kWarningColor : kTextColor);
        painter.drawText(QPointF(kMarginPx, y), lines.at(i));
        y += metrics.lineSpacing();
    }
}

void ResultOverlay::paintLabels(QPainter& painter) const
{
    const std::vector<ResultLabel>& labels = m_annotations.labels;
    if (labels.empty()) {
        return;
    }
    const detail::Projector projector(m_camera, Eigen::Vector3d::Zero());
    const QFontMetricsF metrics(font());
    const auto sizeOf = [&](const QString& text) {
        const QRectF bounds = metrics.boundingRect(QRectF(0, 0, 1e4, 1e4), Qt::AlignLeft, text);
        return QSizeF(bounds.width() + 2 * kPaddingPx, bounds.height() + 2 * kPaddingPx);
    };
    const auto project = [&](const Eigen::Vector3d& p) -> std::optional<QPointF> {
        const detail::ScreenPoint s = projector.project(p);
        if (!s.inFront) {
            return std::nullopt;
        }
        return QPointF(s.pixel.x(), s.pixel.y());
    };
    for (const PlacedLabel& placed :
         placeLabels(labels, m_scale, project, sizeOf, QSizeF(size()))) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(kTextColor);
        painter.drawEllipse(placed.anchor, kAnchorRadiusPx, kAnchorRadiusPx);
        painter.setBrush(kBoxColor);
        painter.drawRoundedRect(placed.box, kPaddingPx, kPaddingPx);
        painter.setPen(kTextColor);
        painter.drawText(placed.box.adjusted(kPaddingPx, kPaddingPx, -kPaddingPx, -kPaddingPx),
                         Qt::AlignLeft | Qt::AlignTop, labels[placed.index].text);
    }
}

} // namespace fem::render
