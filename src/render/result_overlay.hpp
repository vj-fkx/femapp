#pragma once

// Feature 012 (design §5.6; REQ-012-02, 11, 14, 16, 17, 18): the texts of a
// result over the viewport: the legend, the headline and the labels. A
// transparent child widget that never takes mouse events, as the
// SelectionOverlay; repainted after every frame, with the camera and the
// factor of that frame. Private to the render module.

#include <fem/render/camera.hpp>
#include <fem/render/result_display.hpp>

#include <QWidget>

class QPainter;

namespace fem::render {

class ResultOverlay : public QWidget
{
public:
    explicit ResultOverlay(QWidget* parent);

    void setAnnotations(ResultAnnotations annotations);
    // After a frame: what it showed (nothing when `shown` is false).
    void setFrame(const Camera& camera, double scale, bool shown);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintLegend(QPainter& painter, const ResultLegend& legend) const;
    void paintHeadline(QPainter& painter) const;
    void paintLabels(QPainter& painter) const;

    ResultAnnotations m_annotations;
    Camera m_camera;
    double m_scale = 1.0;
    bool m_shown = false;
};

} // namespace fem::render
