#include "section_view.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fem::app {

SectionView::SectionView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QSize SectionView::sizeHint() const
{
    return {240, 240};
}

void SectionView::setSection(const core::SectionShape& shape, const core::SectionSolution* solution)
{
    m_rings = outlineOf(shape);
    m_properties.reset();
    m_points.clear();
    if (solution != nullptr) {
        m_properties = solution->properties();
        for (const core::StressPoint& point : solution->points()) {
            m_points.push_back({.at = toDrawing(*m_properties, point.position),
                                .name = QString::fromStdString(point.name)});
        }
    }
    update();
}

void SectionView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    // The extent of the outline in the drawing (y, z).
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();
    double zMin = yMin;
    double zMax = yMax;
    for (const Ring& ring : m_rings) {
        for (const Eigen::Vector2d& p : ring.points) {
            yMin = std::min(yMin, p.x());
            yMax = std::max(yMax, p.x());
            zMin = std::min(zMin, p.y());
            zMax = std::max(zMax, p.y());
        }
    }
    if (m_rings.empty() || yMax <= yMin || zMax <= zMin) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No drawing"));
        return;
    }
    // z to the right, y up, the same scale on both, with a margin for the
    // axes and the names of the points.
    constexpr double kMargin = 28.0;
    const double scale =
        std::min((width() - 2 * kMargin) / (zMax - zMin), (height() - 2 * kMargin) / (yMax - yMin));
    const double zMid = (zMin + zMax) / 2;
    const double yMid = (yMin + yMax) / 2;
    const auto toScreen = [&](const Eigen::Vector2d& p) {
        return QPointF(width() / 2.0 + (p.y() - zMid) * scale,
                       height() / 2.0 - (p.x() - yMid) * scale);
    };
    const auto pathOf = [&](const Ring& ring) {
        QPainterPath path;
        path.moveTo(toScreen(ring.points.front()));
        for (std::size_t i = 1; i < ring.points.size(); ++i) {
            path.lineTo(toScreen(ring.points[i]));
        }
        path.closeSubpath();
        return path;
    };

    // The material, then the holes cut out of it (walls of a thin-walled
    // section overlap at the joints: each part is filled on its own).
    const QColor fill = palette().color(QPalette::Highlight).lighter(170);
    const QColor edge = palette().color(QPalette::Text);
    painter.setPen(Qt::NoPen);
    for (const Ring& ring : m_rings) {
        if (!ring.hole && ring.points.size() >= 3) {
            painter.fillPath(pathOf(ring), fill);
        }
    }
    for (const Ring& ring : m_rings) {
        if (ring.hole && ring.points.size() >= 3) {
            painter.fillPath(pathOf(ring), palette().base());
        }
    }
    painter.setPen(QPen(edge, 1.0));
    painter.setBrush(Qt::NoBrush);
    for (const Ring& ring : m_rings) {
        if (ring.points.size() >= 3) {
            painter.drawPath(pathOf(ring));
        }
    }
    if (!m_properties) {
        return;
    }

    // The principal axes through the centroid.
    const core::SectionProperties& p = *m_properties;
    const double reach = 0.6 * std::max(yMax - yMin, zMax - zMin);
    const QColor axes(200, 60, 60);
    painter.setPen(QPen(axes, 1.0, Qt::DashDotLine));
    const auto axis = [&](const Eigen::Vector2d& direction, const QString& name) {
        const QPointF a = toScreen(toDrawing(p, -reach * direction));
        const QPointF b = toScreen(toDrawing(p, reach * direction));
        painter.drawLine(a, b);
        painter.drawText(b + QPointF(3, -3), name);
    };
    axis(Eigen::Vector2d(1, 0), QStringLiteral("y'"));
    axis(Eigen::Vector2d(0, 1), QStringLiteral("z'"));

    // The centroid, a circle with a cross; the shear centre, a square.
    painter.setPen(QPen(axes, 1.5));
    const QPointF g = toScreen(p.centroid);
    painter.drawEllipse(g, 5.0, 5.0);
    painter.drawLine(g - QPointF(5, 0), g + QPointF(5, 0));
    painter.drawLine(g - QPointF(0, 5), g + QPointF(0, 5));
    const QColor shear(40, 110, 200);
    painter.setPen(QPen(shear, 1.5));
    const QPointF sc = toScreen(toDrawing(p, p.shearCentre));
    painter.drawRect(QRectF(sc - QPointF(4, 4), QSizeF(8, 8)));

    // The stress points with their names.
    painter.setPen(edge);
    painter.setBrush(edge);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    painter.setFont(small);
    for (const Marker& marker : m_points) {
        const QPointF at = toScreen(marker.at);
        painter.drawEllipse(at, 2.5, 2.5);
        painter.drawText(at + QPointF(4, -4), marker.name);
    }
}

} // namespace fem::app
