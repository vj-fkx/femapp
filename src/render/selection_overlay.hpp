#pragma once

#include <fem/geom/point_resolver.hpp>

#include <QPointF>
#include <QRect>
#include <QString>
#include <QWidget>

#include <optional>

namespace fem::render {

// Transparent child widget drawn on top of the viewport, in pixels. It never
// takes mouse events. It shows:
// - the window/crossing rectangle (REQ-004-23): solid blue for a window,
//   dashed green for a crossing;
// - the object snap marker and its name (REQ-005-20): a square for an
//   endpoint, a triangle for a midpoint, an X for an intersection.
class SelectionOverlay : public QWidget
{
public:
    explicit SelectionOverlay(QWidget* parent);

    // `crossing` selects the dashed green style.
    void showRect(const QRect& rect, bool crossing);
    void hideRect();

    void showSnap(const QPointF& pixel, geom::SnapMode mode, const QString& label);
    void hideSnap();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refresh();

    std::optional<QRect> m_rect;
    bool m_crossing = false;
    struct Snap
    {
        QPointF pixel;
        geom::SnapMode mode = geom::SnapMode::Endpoint;
        QString label;
    };
    std::optional<Snap> m_snap;
};

} // namespace fem::render
