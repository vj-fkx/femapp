#include "result_display_settings.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <array>
#include <cmath>
#include <cstddef>

namespace fem::app {

namespace {

using core::ResultQuantity;

constexpr std::array<const char*, core::kResultQuantityCount> kNames = {
    "u", "v", "w", "|u|", "θx", "θy", "θz", "N", "Vy", "Vz", "T", "My", "Mz"};

QString tr(const char* text)
{
    return QCoreApplication::translate("fem::app::ResultDisplay", text);
}

} // namespace

bool isBuckling(const setup::AnalysisResult& result)
{
    return result.settings.type == setup::AnalysisType::Buckling;
}

std::vector<ResultQuantity> quantitiesOf(const setup::AnalysisResult& result)
{
    std::vector<ResultQuantity> out;
    for (int q = 0; q < core::kResultQuantityCount; ++q) {
        const auto quantity = static_cast<ResultQuantity>(q);
        if (!isBuckling(result) || !core::isForce(quantity)) {
            out.push_back(quantity);
        }
    }
    return out;
}

ResultQuantity shownQuantity(const setup::AnalysisResult& result, ResultQuantity wanted)
{
    return isBuckling(result) && core::isForce(wanted) ? ResultQuantity::UMagnitude : wanted;
}

QString quantityName(ResultQuantity quantity)
{
    return QString::fromUtf8(kNames.at(static_cast<std::size_t>(quantity)));
}

setup::Quantity quantityUnit(ResultQuantity quantity)
{
    switch (quantity) {
    case ResultQuantity::Ux:
    case ResultQuantity::Uy:
    case ResultQuantity::Uz:
    case ResultQuantity::UMagnitude:
        return setup::quantity::kLength;
    case ResultQuantity::N:
    case ResultQuantity::Vy:
    case ResultQuantity::Vz:
        return setup::quantity::kForce;
    case ResultQuantity::T:
    case ResultQuantity::My:
    case ResultQuantity::Mz:
        return setup::quantity::kMoment;
    default:
        return setup::quantity::kNone; // rotations, in rad
    }
}

QString quantityTitle(ResultQuantity quantity, setup::UnitSystem units, bool buckling)
{
    const QString name = quantityName(quantity);
    if (buckling) {
        return tr("%1 (normalized)").arg(name);
    }
    const setup::Quantity unit = quantityUnit(quantity);
    const QString label = unit == setup::quantity::kNone
                              ? QStringLiteral("rad")
                              : QString::fromStdString(setup::unitLabel(units, unit));
    return QStringLiteral("%1 [%2]").arg(name, label);
}

QString formatResult(double value)
{
    constexpr int kDigits = 6;
    return QLocale().toString(value, 'g', kDigits);
}

render::ResultDisplay displayFor(const ResultDisplaySettings& settings,
                                 const core::FieldSamples& field, double autoScale, bool buckling)
{
    render::ResultDisplay display;
    switch (settings.scaleMode) {
    case ScaleMode::Automatic:
        display.scale = autoScale;
        break;
    case ScaleMode::User:
        display.scale = settings.userScale;
        break;
    case ScaleMode::Real:
        display.scale = 1.0;
        break;
    }
    display.deformed = settings.deformed;
    display.original = settings.original;
    display.colored = settings.colored;
    double min = settings.rangeFixed ? settings.rangeMin : field.min.value;
    double max = settings.rangeFixed ? settings.rangeMax : field.max.value;
    if (!(max > min)) {
        // REQ-012-11 (design §7): one value: a range around it.
        const double value = min;
        min = value != 0.0 ? value - 1.0 : -1.0;
        max = value != 0.0 ? value + 1.0 : 1.0;
    }
    display.min = min;
    display.max = max;
    display.bands = settings.bands;
    display.animation = buckling ? render::Animation::Mode : render::Animation::Static;
    display.period = settings.period;
    display.playing = settings.playing;
    return display;
}

} // namespace fem::app
