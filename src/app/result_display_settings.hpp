#pragma once

// Feature 012 (design §4, §5.7): what the user chose for the display of a
// result, the quantities a result offers, their names and units, and the
// frame options that follow from them. No widgets: tested without a
// window.

#include <fem/core/result_sampling.hpp>
#include <fem/render/result_display.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/units.hpp>

#include <QString>

#include <cstdint>
#include <vector>

namespace fem::app {

// REQ-012-06: the factor of the deformed shape.
enum class ScaleMode : std::uint8_t { Automatic, User, Real };

struct ResultDisplaySettings
{
    core::ResultQuantity quantity = core::ResultQuantity::UMagnitude;
    bool colored = true; // REQ-012-09: false, "None"
    ScaleMode scaleMode = ScaleMode::Automatic;
    double userScale = 1.0;
    bool deformed = true;    // REQ-012-07
    bool original = false;   // REQ-012-07
    bool rangeFixed = false; // REQ-012-11
    double rangeMin = 0.0;
    double rangeMax = 1.0;
    int bands = 0;         // REQ-012-12: 0, continuous
    bool reactions = true; // REQ-012-16
    bool extremes = true;  // REQ-012-17
    bool playing = false;  // REQ-012-15
    double period = 2.0;   // seconds per cycle
};

// REQ-012-09: the quantities a result offers (a buckling mode: the
// displacements and rotations only), and the one shown when `wanted` is
// not among them (|u|).
[[nodiscard]] bool isBuckling(const setup::AnalysisResult& result);
[[nodiscard]] std::vector<core::ResultQuantity> quantitiesOf(const setup::AnalysisResult& result);
[[nodiscard]] core::ResultQuantity shownQuantity(const setup::AnalysisResult& result,
                                                 core::ResultQuantity wanted);

// REQ-012-11, 19 (design §5.7): "u", "|u|", "θz", "Mz"...; the quantity of
// its unit (rotations: none, in rad); "Mz [kN·m]", "θz [rad]", or
// "u (normalized)" in a buckling mode.
[[nodiscard]] QString quantityName(core::ResultQuantity quantity);
[[nodiscard]] setup::Quantity quantityUnit(core::ResultQuantity quantity);
[[nodiscard]] QString quantityTitle(core::ResultQuantity quantity, setup::UnitSystem units,
                                    bool buckling);

// REQ-012-19: one number format for the tables and the viewport.
[[nodiscard]] QString formatResult(double value);

// REQ-012-06, 07, 09, 11, 12, 15: the options of the frames: the factor
// (automatic, the user's or 1), the range (the extremes of `field`, or the
// user's; an empty one widened), the bands and the animation (Mode for a
// buckling mode, Static otherwise).
[[nodiscard]] render::ResultDisplay displayFor(const ResultDisplaySettings& settings,
                                               const core::FieldSamples& field, double autoScale,
                                               bool buckling);

} // namespace fem::app
