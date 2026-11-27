#pragma once

// Feature 012 (design §4, §5.5): how a result is drawn in one frame. Every
// field here is a uniform or a choice of draw, so changing it re-sends
// nothing to the GPU (REQ-012-08, 13).

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fem::render {

// REQ-012-15: the scale over time: 0 to f (a static result) or -f to f (a
// buckling mode).
enum class Animation : std::uint8_t { Off, Static, Mode };

struct ResultDisplay
{
    bool deformed = true;  // REQ-012-07: the shape moved by scale x displacements
    bool original = false; // REQ-012-07: the original shape, thin and neutral
    double scale = 1.0;    // REQ-012-06: the factor f
    bool colored = true;   // REQ-012-09: false, the 1D color of the mesh
    double min = 0.0;      // REQ-012-11: the range of the colors
    double max = 1.0;
    int bands = 0; // REQ-012-12: 0, continuous
    Animation animation = Animation::Off;
    double period = 2.0; // seconds per cycle
    bool playing = false;
};

// REQ-012-15: the factor drawn `seconds` after the animation started: the
// factor itself when it does not play; 0 to f, f (1 - cos(2 pi t / T)) / 2,
// for Static; -f to f, f sin(2 pi t / T), for Mode.
[[nodiscard]] double animatedScale(const ResultDisplay& display, double seconds) noexcept;

// Feature 012 (design §5.6): the texts drawn over the viewport, made by the
// app (units and number formats are its business).
struct ResultLabel
{
    // In the order of priority: a label never covers one placed before it.
    enum class Kind : std::uint8_t { Query, Extreme, Reaction };
    Kind kind = Kind::Reaction;
    Eigen::Vector3d at = Eigen::Vector3d::Zero();           // original, global
    Eigen::Vector3d displacement = Eigen::Vector3d::Zero(); // moved with the shape
    QString text;                                           // lines apart by '\n'
};

struct ResultLegend
{
    QString title; // the quantity and its unit, e.g. "Mz [kN·m]"
    double min = 0.0;
    double max = 1.0;
    int bands = 0;
    std::vector<std::pair<double, QString>> ticks; // values and their texts
};

struct ResultAnnotations
{
    std::optional<ResultLegend> legend; // REQ-012-11
    QStringList headline;               // the case, the scale, the mode and its factor
    bool warning = false;               // REQ-012-02: the last headline line warns
    std::vector<ResultLabel> labels;    // REQ-012-16, 17, 18
};

// REQ-012-11: the values of the ticks of a legend: the limits of the bands
// up to 10 bands (11 ticks), else 6 even ones from min to max.
[[nodiscard]] std::vector<double> legendTicks(double min, double max, int bands);

// REQ-012-16, 17, 18: where the labels go: each at its point moved by scale
// x displacement (`project` gives its pixel, nothing behind the camera), its
// box above and to the right of it. By kind (Query, Extreme, Reaction),
// then in order; a label whose point is off the view, or whose box would
// cover one placed before, is left out.
struct PlacedLabel
{
    std::size_t index = 0; // in the labels given
    QPointF anchor;        // the pixel of its point
    QRectF box;            // where its text goes
};
inline constexpr double kLabelOffsetPx = 6.0;
[[nodiscard]] std::vector<PlacedLabel>
placeLabels(std::span<const ResultLabel> labels, double scale,
            const std::function<std::optional<QPointF>(const Eigen::Vector3d&)>& project,
            const std::function<QSizeF(const QString&)>& sizeOf, const QSizeF& view);

} // namespace fem::render
