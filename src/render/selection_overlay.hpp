#pragma once

#include <QRect>
#include <QWidget>

namespace fem::render {

// Transparent child widget that draws the window/crossing rectangle on top of
// the viewport (REQ-004-23): solid blue for a window, dashed green for a
// crossing. It never takes mouse events.
class SelectionOverlay : public QWidget
{
public:
    explicit SelectionOverlay(QWidget* parent);

    // `crossing` selects the dashed green style.
    void showRect(const QRect& rect, bool crossing);
    void hideRect();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect m_rect;
    bool m_crossing = false;
};

} // namespace fem::render
