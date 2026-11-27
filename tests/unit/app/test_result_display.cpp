// Feature 012 (design §5.7): the options of the display of a result.

#include "result_display_settings.hpp"

#include <fem/setup/units.hpp>

#include <QLocale>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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
