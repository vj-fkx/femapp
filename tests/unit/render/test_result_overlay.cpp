// Feature 012 (design §5.6): where the texts of a result go.

#include <fem/render/result_display.hpp>

#include <QPointF>
#include <QSizeF>
#include <QString>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

using fem::render::ResultLabel;
using Vec = Eigen::Vector3d;

namespace {

// One world unit, 100 pixels; y down the screen as x.
std::optional<QPointF> toPixel(const Vec& p)
{
    if (p.z() < 0.0) {
        return std::nullopt; // behind the camera
    }
    return QPointF(100.0 * p.x(), 100.0 * p.y());
}

QSizeF fixedSize(const QString& /*text*/)
{
    return {50.0, 20.0};
}

ResultLabel label(ResultLabel::Kind kind, const Vec& at, const QString& text,
                  const Vec& displacement = Vec::Zero())
{
    return ResultLabel{.kind = kind, .at = at, .displacement = displacement, .text = text};
}

std::vector<std::size_t> indices(const std::vector<fem::render::PlacedLabel>& placed)
{
    std::vector<std::size_t> out;
    out.reserve(placed.size());
    for (const auto& p : placed) {
        out.push_back(p.index);
    }
    return out;
}

} // namespace

// REQ-012-11: the ticks of the legend.
TEST_CASE("the ticks of a legend", "[render][result][overlay]")
{
    CHECK(fem::render::legendTicks(0.0, 10.0, 0) == std::vector<double>{0, 2, 4, 6, 8, 10});
    CHECK(fem::render::legendTicks(0.0, 10.0, 5) == std::vector<double>{0, 2, 4, 6, 8, 10});
    CHECK(fem::render::legendTicks(-1.0, 1.0, 2) == std::vector<double>{-1, 0, 1});
    CHECK(fem::render::legendTicks(0.0, 10.0, 10).size() == 11);
    // More bands than that: even ticks.
    CHECK(fem::render::legendTicks(0.0, 10.0, 12) == std::vector<double>{0, 2, 4, 6, 8, 10});
    // The last one is exactly the maximum.
    CHECK(fem::render::legendTicks(0.1, 0.7, 3).back() == 0.7);
}

// REQ-012-16, 17, 18: the order of priority, the labels that would cover
// another and those off the view.
TEST_CASE("where the labels of a result go", "[render][result][overlay]")
{
    using Kind = ResultLabel::Kind;
    const QSizeF view(800.0, 600.0);
    const std::vector<ResultLabel> labels = {
        label(Kind::Reaction, Vec(1.0, 2.0, 0.0), "reaction"),
        label(Kind::Extreme, Vec(1.2, 2.0, 0.0), "max"),
        label(Kind::Query, Vec(5.0, 5.0, 0.0), "query"),
        label(Kind::Reaction, Vec(50.0, 2.0, 0.0), "off the view"),
        label(Kind::Reaction, Vec(3.0, 3.0, -1.0), "behind"),
        label(Kind::Reaction, Vec(3.0, 1.0, 0.0), "moved", Vec(1.0, 0.0, 0.0)),
    };
    const auto placed = fem::render::placeLabels(labels, 2.0, toPixel, fixedSize, view);
    // The query first, then the extreme; the reaction next to the extreme
    // would cover it.
    CHECK(indices(placed) == std::vector<std::size_t>{2, 1, 5});
    // Above and to the right of the point.
    const fem::render::PlacedLabel& query = placed.front();
    CHECK(query.anchor == QPointF(500.0, 500.0));
    CHECK(query.box.left() == 500.0 + fem::render::kLabelOffsetPx);
    CHECK(query.box.bottom() == 500.0 - fem::render::kLabelOffsetPx);
    CHECK(query.box.size() == QSizeF(50.0, 20.0));
    // Moved with the shape: (3, 1) + 2 x (1, 0).
    CHECK(placed.back().anchor == QPointF(500.0, 100.0));
}
