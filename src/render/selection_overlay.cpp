#include "selection_overlay.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

namespace fem::render {

namespace {

const QColor kWindowColor(70, 140, 255);
const QColor kCrossingColor(80, 200, 110);
const QColor kSnapColor(255, 220, 60);
constexpr int kFillAlpha = 40;
constexpr double kSnapHalfSizePx = 5.0;
constexpr double kLabelOffsetPx = 10.0;

} // namespace

SelectionOverlay::SelectionOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
}

void SelectionOverlay::showRect(const QRect& rect, bool crossing)
{
    m_rect = rect.normalized();
    m_crossing = crossing;
    refresh();
}

void SelectionOverlay::hideRect()
{
    m_rect.reset();
    refresh();
}

void SelectionOverlay::showSnap(const QPointF& pixel, geom::SnapMode mode, const QString& label)
{
    m_snap = Snap{.pixel = pixel, .mode = mode, .label = label};
    refresh();
}

void SelectionOverlay::hideSnap()
{
    if (m_snap) {
        m_snap.reset();
        refresh();
    }
}

void SelectionOverlay::refresh()
{
    if (!m_rect && !m_snap) {
        hide();
        return;
    }
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    update();
}

void SelectionOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    if (m_rect) {
        const QColor color = m_crossing ? kCrossingColor : kWindowColor;
        QColor fill = color;
        fill.setAlpha(kFillAlpha);
        QPen pen(color);
        pen.setWidth(1);
        pen.setStyle(m_crossing ? Qt::DashLine : Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(fill);
        painter.drawRect(m_rect->adjusted(0, 0, -1, -1));
    }
    if (m_snap) {
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(kSnapColor);
        pen.setWidthF(2.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPointF c = m_snap->pixel;
        const double h = kSnapHalfSizePx;
        switch (m_snap->mode) {
        case geom::SnapMode::Endpoint:
            painter.drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));
            break;
        case geom::SnapMode::Midpoint:
            painter.drawPolygon(QPolygonF({QPointF(c.x(), c.y() - h), QPointF(c.x() + h, c.y() + h),
                                           QPointF(c.x() - h, c.y() + h)}));
            break;
        case geom::SnapMode::Intersection:
            painter.drawLine(QPointF(c.x() - h, c.y() - h), QPointF(c.x() + h, c.y() + h));
            painter.drawLine(QPointF(c.x() - h, c.y() + h), QPointF(c.x() + h, c.y() - h));
            break;
        }
        painter.drawText(c + QPointF(kLabelOffsetPx, -kLabelOffsetPx), m_snap->label);
    }
}

} // namespace fem::render
