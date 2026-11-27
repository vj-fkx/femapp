// Feature 012 (design §5.7): the options of the display of a result.

#include "frame_document.hpp"
#include "result_display_settings.hpp"

#include <fem/setup/editing.hpp>
#include <fem/setup/units.hpp>

#include <QLocale>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

namespace s = fem::setup;
using fem::core::ResultQuantity;

s::AnalysisResult resultOf(s::AnalysisType type)
{
    s::AnalysisResult result;
    result.settings.type = type;
    return result;
}

fem::core::FieldSamples field(double min, double max)
{
    fem::core::FieldSamples f;
    f.min = {.value = min, .sample = 0};
    f.max = {.value = max, .sample = 1};
    return f;
}

} // namespace

// REQ-012-09: a buckling mode offers no forces; asked for one, |u|.
TEST_CASE("the quantities of a result", "[app][result][display]")
{
    const s::AnalysisResult linear = resultOf(s::AnalysisType::Linear);
    const s::AnalysisResult second = resultOf(s::AnalysisType::SecondOrder);
    const s::AnalysisResult buckling = resultOf(s::AnalysisType::Buckling);
    CHECK(fem::app::quantitiesOf(linear).size() == 13);
    CHECK(fem::app::quantitiesOf(second).size() == 13);
    const auto modes = fem::app::quantitiesOf(buckling);
    CHECK(modes.size() == 7);
    CHECK(std::ranges::none_of(modes, [](ResultQuantity q) { return fem::core::isForce(q); }));
    CHECK(fem::app::shownQuantity(buckling, ResultQuantity::Mz) == ResultQuantity::UMagnitude);
    CHECK(fem::app::shownQuantity(buckling, ResultQuantity::Uy) == ResultQuantity::Uy);
    CHECK(fem::app::shownQuantity(linear, ResultQuantity::Mz) == ResultQuantity::Mz);
    CHECK(fem::app::isBuckling(buckling));
    CHECK_FALSE(fem::app::isBuckling(second));
}

// REQ-012-11, 19: names, units and titles.
TEST_CASE("the names and units of the quantities", "[app][result][display]")
{
    CHECK(fem::app::quantityName(ResultQuantity::UMagnitude) == QStringLiteral("|u|"));
    CHECK(fem::app::quantityName(ResultQuantity::Rz) == QString::fromUtf8("θz"));
    CHECK(fem::app::quantityName(ResultQuantity::Mz) == QStringLiteral("Mz"));
    CHECK(fem::app::quantityUnit(ResultQuantity::Uy) == s::quantity::kLength);
    CHECK(fem::app::quantityUnit(ResultQuantity::Vz) == s::quantity::kForce);
    CHECK(fem::app::quantityUnit(ResultQuantity::T) == s::quantity::kMoment);
    CHECK(fem::app::quantityUnit(ResultQuantity::Rx) == s::quantity::kNone);
    const s::UnitSystem units = s::UnitSystem::KilonewtonMetre;
    CHECK(fem::app::quantityTitle(ResultQuantity::Mz, units, false) ==
          QStringLiteral("Mz [%1]").arg(
              QString::fromStdString(s::unitLabel(units, s::quantity::kMoment))));
    CHECK(fem::app::quantityTitle(ResultQuantity::Rz, units, false) ==
          QString::fromUtf8("θz [rad]"));
    CHECK(fem::app::quantityTitle(ResultQuantity::Ux, units, true) ==
          QStringLiteral("u (normalized)"));
}

// REQ-012-19: the format of the tables and of the viewport.
TEST_CASE("the format of the numbers of a result", "[app][result][display]")
{
    CHECK(fem::app::formatResult(12.5) == QLocale().toString(12.5, 'g', 6));
    CHECK(fem::app::formatResult(-0.000123456789) == QLocale().toString(-0.000123456789, 'g', 6));
    CHECK(fem::app::formatResult(1234567.0) == QLocale().toString(1234567.0, 'g', 6));
}

// REQ-012-06, 07, 11, 12, 15: the options of the frames.
TEST_CASE("the frame options of a result", "[app][result][display]")
{
    using fem::app::ScaleMode;
    fem::app::ResultDisplaySettings settings;
    SECTION("the factor: automatic, the user's, real")
    {
        CHECK(fem::app::displayFor(settings, field(0, 1), 250.0, false).scale == 250.0);
        settings.scaleMode = ScaleMode::User;
        settings.userScale = 40.0;
        CHECK(fem::app::displayFor(settings, field(0, 1), 250.0, false).scale == 40.0);
        settings.scaleMode = ScaleMode::Real;
        CHECK(fem::app::displayFor(settings, field(0, 1), 250.0, false).scale == 1.0);
    }
    SECTION("the range: the extremes, the user's, an empty one widened")
    {
        auto display = fem::app::displayFor(settings, field(-3.0, 7.0), 1.0, false);
        CHECK(display.min == -3.0);
        CHECK(display.max == 7.0);
        settings.rangeFixed = true;
        settings.rangeMin = 0.0;
        settings.rangeMax = 5.0;
        display = fem::app::displayFor(settings, field(-3.0, 7.0), 1.0, false);
        CHECK(display.min == 0.0);
        CHECK(display.max == 5.0);
        settings.rangeFixed = false;
        display = fem::app::displayFor(settings, field(4.0, 4.0), 1.0, false);
        CHECK(display.min == 3.0);
        CHECK(display.max == 5.0);
        display = fem::app::displayFor(settings, field(0.0, 0.0), 1.0, false);
        CHECK(display.min == -1.0);
        CHECK(display.max == 1.0);
    }
    SECTION("the rest is passed on; the animation follows the kind of result")
    {
        settings.deformed = false;
        settings.original = true;
        settings.colored = false;
        settings.bands = 7;
        settings.playing = true;
        settings.period = 3.0;
        auto display = fem::app::displayFor(settings, field(0, 1), 1.0, false);
        CHECK_FALSE(display.deformed);
        CHECK(display.original);
        CHECK_FALSE(display.colored);
        CHECK(display.bands == 7);
        CHECK(display.playing);
        CHECK(display.period == 3.0);
        CHECK(display.animation == fem::render::Animation::Static);
        display = fem::app::displayFor(settings, field(0, 1), 1.0, true);
        CHECK(display.animation == fem::render::Animation::Mode);
    }
}

namespace {

// The frame of the 010, analysed, and what the texts are made from.
struct Shown
{
    fem::setup::test::Frame frame;
    fem::setup::test::Setup setup;
    s::MeshContext mesh;
    std::optional<s::AnalysisResult> result;
    fem::core::ResultFields fields;
    fem::core::ShapeSamples shape;
    fem::core::FieldSamples field;
};

const s::AnalysisResult& got(const Shown& a)
{
    if (!a.result) {
        throw std::logic_error("no result");
    }
    return *a.result;
}

void show(Shown& a, s::AnalysisType type, ResultQuantity quantity)
{
    fem::setup::test::drawFrame(a.frame);
    fem::setup::test::prepare(a.setup, a.frame);
    s::AnalysisSettings settings;
    settings.type = type;
    a.setup.document.apply(s::changeAnalysis(a.setup.document, settings));
    a.mesh = fem::setup::test::meshOf(a.frame.drawing);
    a.result = s::runCase(a.setup.document, a.frame.drawing, a.mesh, a.setup.loadCase, {}).result;
    REQUIRE(a.result.has_value());
    const s::AnalysisResult& result = got(a);
    if (const auto* modes = std::get_if<fem::core::BucklingResult>(&result.data)) {
        REQUIRE_FALSE(modes->modes.empty());
        a.fields = fem::core::fieldsOf(result.mesh, modes->modes.front());
    } else {
        const fem::core::StaticResult* solved = fem::app::staticOf(result);
        REQUIRE(solved != nullptr);
        a.fields = fem::core::fieldsOf(*solved);
    }
    a.shape = fem::core::sampleShape(a.fields, {.perBeam = 5, .threads = 0});
    a.field = fem::core::sampleField(a.fields, a.shape, quantity);
}

const fem::render::ResultLegend& legendOf(const fem::render::ResultAnnotations& out)
{
    if (!out.legend) {
        throw std::logic_error("no legend");
    }
    return *out.legend;
}

fem::app::AnnotationInput inputOf(const Shown& a)
{
    fem::app::AnnotationInput in;
    in.result = &got(a);
    in.fields = &a.fields;
    in.shape = &a.shape;
    in.field = &a.field;
    in.units = a.setup.document.units();
    in.display = fem::app::displayFor(in.settings, a.field, 25.0, fem::app::isBuckling(got(a)));
    in.caseName = QStringLiteral("lateral (linear)");
    return in;
}

std::vector<fem::render::ResultLabel> labelsOf(const fem::render::ResultAnnotations& out,
                                               fem::render::ResultLabel::Kind kind)
{
    std::vector<fem::render::ResultLabel> labels;
    std::ranges::copy_if(out.labels, std::back_inserter(labels),
                         [&](const auto& l) { return l.kind == kind; });
    return labels;
}

QString unitOf(const Shown& a, s::Quantity quantity)
{
    return QString::fromStdString(s::unitLabel(a.setup.document.units(), quantity));
}

} // namespace

// REQ-012-16, 19, AC-5: the reactions of the frame, with their values and
// the units of the document; they balance the load.
TEST_CASE("the reactions of a result as texts", "[app][result][display]")
{
    using Kind = fem::render::ResultLabel::Kind;
    Shown a;
    show(a, s::AnalysisType::Linear, ResultQuantity::Mz);
    const fem::core::StaticResult* solved = fem::app::staticOf(got(a));
    REQUIRE(solved != nullptr);
    const fem::render::ResultAnnotations out = fem::app::annotationsFor(inputOf(a));
    const auto reactions = labelsOf(out, Kind::Reaction);
    REQUIRE(reactions.size() == got(a).supported.size());
    Eigen::Vector3d sum = Eigen::Vector3d::Zero();
    for (std::size_t i = 0; i < reactions.size(); ++i) {
        const auto node = static_cast<std::size_t>(got(a).supported[i]);
        const Eigen::Vector3d r = solved->reactions[node];
        sum += r;
        CHECK(reactions[i].at == got(a).mesh->nodeCoordinates(got(a).supported[i]));
        const QString text = reactions[i].text;
        CAPTURE(text.toStdString());
        CHECK(
            text.contains(QStringLiteral("Fx = %1 %2")
                              .arg(fem::app::formatResult(r.x()), unitOf(a, s::quantity::kForce))));
        CHECK(
            text.contains(QStringLiteral("Fy = %1 %2")
                              .arg(fem::app::formatResult(r.y()), unitOf(a, s::quantity::kForce))));
        CHECK(text.contains(QStringLiteral("Mz = %1 %2")
                                .arg(fem::app::formatResult(solved->reactionMoments[node].z()),
                                     unitOf(a, s::quantity::kMoment))));
        // A 2D frame: nothing out of its plane.
        CHECK_FALSE(text.contains(QStringLiteral("Fz")));
        CHECK_FALSE(text.contains(QStringLiteral("Mx")));
    }
    CHECK((sum + solved->appliedForce).norm() <= 1e-9 * solved->appliedForce.norm());
    // Turned off: none.
    fem::app::AnnotationInput in = inputOf(a);
    in.settings.reactions = false;
    CHECK(labelsOf(fem::app::annotationsFor(in), Kind::Reaction).empty());
}

// REQ-012-11, 17: the legend and the extremes of the quantity colored.
TEST_CASE("the legend and the extremes of a result", "[app][result][display]")
{
    using Kind = fem::render::ResultLabel::Kind;
    Shown a;
    show(a, s::AnalysisType::Linear, ResultQuantity::Mz);
    const fem::app::AnnotationInput in = inputOf(a);
    const fem::render::ResultAnnotations out = fem::app::annotationsFor(in);
    REQUIRE(out.legend.has_value());
    CHECK(legendOf(out).title ==
          fem::app::quantityTitle(ResultQuantity::Mz, a.setup.document.units(), false));
    CHECK(legendOf(out).min == a.field.min.value);
    CHECK(legendOf(out).max == a.field.max.value);
    REQUIRE(legendOf(out).ticks.size() == 6);
    CHECK(legendOf(out).ticks.front().second == fem::app::formatResult(a.field.min.value));
    const auto extremes = labelsOf(out, Kind::Extreme);
    REQUIRE(extremes.size() == 2);
    const std::uint32_t top = a.field.max.sample;
    CHECK(extremes[0].at == a.shape.positions[top]);
    CHECK(extremes[0].text.startsWith(
        QStringLiteral("max Mz = %1 %2")
            .arg(fem::app::formatResult(a.field.max.value), unitOf(a, s::quantity::kMoment))));
    // Where: a node, or a bar and its x, by its external id.
    const fem::core::Station& station = a.shape.stations[top];
    CHECK((extremes[0].text.contains(QStringLiteral("node")) ||
           extremes[0].text.contains(QStringLiteral("bar"))));
    if (station.node < 0) {
        CHECK(extremes[0].text.contains(
            QStringLiteral("x = %1").arg(fem::app::formatResult(station.x))));
    }
    CHECK(extremes[1].text.startsWith(QStringLiteral("min Mz = ")));
    // Turned off, or no colors: none, and no legend without colors.
    fem::app::AnnotationInput off = in;
    off.settings.extremes = false;
    CHECK(labelsOf(fem::app::annotationsFor(off), Kind::Extreme).empty());
    off = in;
    off.settings.colored = false;
    const auto plain = fem::app::annotationsFor(off);
    CHECK_FALSE(plain.legend.has_value());
    CHECK(labelsOf(plain, Kind::Extreme).empty());
}

// REQ-012-02, 06, 14, AC-4: the headline: the case, the factor, the mode
// with the factor of the core, and the warning.
TEST_CASE("the headline of a result", "[app][result][display]")
{
    Shown a;
    show(a, s::AnalysisType::Buckling, ResultQuantity::UMagnitude);
    fem::app::AnnotationInput in = inputOf(a);
    const auto* modes = std::get_if<fem::core::BucklingResult>(&got(a).data);
    REQUIRE(modes != nullptr);
    fem::render::ResultAnnotations out = fem::app::annotationsFor(in);
    REQUIRE(out.headline.size() == 3);
    CHECK(out.headline[0] == QStringLiteral("lateral (linear)"));
    CHECK(out.headline[1].contains(fem::app::formatResult(25.0)));
    CHECK(out.headline[1].contains(QStringLiteral("auto")));
    CHECK(out.headline[2] ==
          QStringLiteral("Mode 1: factor %1").arg(fem::app::formatResult(modes->modes[0].factor)));
    CHECK_FALSE(out.warning);
    // A buckling mode: no units, no reactions.
    REQUIRE(out.legend.has_value());
    CHECK(legendOf(out).title == QStringLiteral("|u| (normalized)"));
    CHECK(labelsOf(out, fem::render::ResultLabel::Kind::Reaction).empty());
    in.current = false;
    out = fem::app::annotationsFor(in);
    CHECK(out.warning);
    CHECK(out.headline.back().startsWith(QStringLiteral("Out of date")));
}

// REQ-012-18, AC-6: the query: where, the value of the core there and the
// displacement.
TEST_CASE("the query of a point of a result", "[app][result][display]")
{
    Shown a;
    show(a, s::AnalysisType::Linear, ResultQuantity::Mz);
    fem::app::AnnotationInput in = inputOf(a);
    const fem::core::SampledBar& bar = a.shape.bars.front();
    const fem::core::Station station{.x = 0.37, .side = fem::core::Side::After, .node = -1};
    in.query = fem::render::ResultPick{.element = bar.element, .station = station, .node = -1};
    const auto out = fem::app::annotationsFor(in);
    const auto queries = labelsOf(out, fem::render::ResultLabel::Kind::Query);
    REQUIRE(queries.size() == 1);
    const double value = fem::core::valueAt(a.fields, bar.element, station, ResultQuantity::Mz);
    const QString text = queries.front().text;
    CAPTURE(text.toStdString());
    CHECK(text.contains(QStringLiteral("bar %1, x = %2")
                            .arg(got(a).mesh->elementId(bar.element))
                            .arg(fem::app::formatResult(0.37))));
    CHECK(text.contains(QStringLiteral("Mz = %1").arg(fem::app::formatResult(value))));
    CHECK(queries.front().at == fem::core::positionAt(a.fields, bar.element, station));
    CHECK(queries.front().displacement ==
          fem::core::displacementAt(a.fields, bar.element, station));
    // A node.
    in.query = fem::render::ResultPick{.element = bar.element,
                                       .station = a.shape.stations[bar.first],
                                       .node = got(a).mesh->elementNodes(bar.element)[0]};
    const auto node = labelsOf(fem::app::annotationsFor(in), fem::render::ResultLabel::Kind::Query);
    REQUIRE(node.size() == 1);
    CHECK(node.front().text.startsWith(QStringLiteral("node %1").arg(
        got(a).mesh->nodeId(got(a).mesh->elementNodes(bar.element)[0]))));
}
