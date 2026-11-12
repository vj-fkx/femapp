#include "selection_overlay.hpp"

#include <QColor>
#include <QPainter>
#include <QPen>

namespace fem::render {

namespace {

const QColor kWindowColor(70, 140, 255);
const QColor kCrossingColor(80, 200, 110);
constexpr int kFillAlpha = 40;

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
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    update();
}

void SelectionOverlay::hideRect()
{
    hide();
}

void SelectionOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    const QColor color = m_crossing ? kCrossingColor : kWindowColor;
    QColor fill = color;
    fill.setAlpha(kFillAlpha);
    QPen pen(color);
    pen.setWidth(1);
    pen.setStyle(m_crossing ? Qt::DashLine : Qt::SolidLine);
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRect(m_rect.adjusted(0, 0, -1, -1));
}

} // namespace fem::render
