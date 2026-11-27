#include "result_display_settings.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <variant>

namespace fem::app {

namespace {

using core::ResultQuantity;

constexpr std::array<const char*, core::kResultQuantityCount> kNames = {
    "u", "v", "w", "|u|", "θx", "θy", "θz", "N", "Vy", "Vz", "T", "My", "Mz"};

QString tr(const char* text)
{
    return QCoreApplication::translate("fem::app::ResultDisplay", text);
}

// The unit after a value: " kN·m", " rad", or nothing in a buckling mode.
QString unitText(ResultQuantity quantity, setup::UnitSystem units, bool buckling)
{
    if (buckling) {
        return {};
    }
    const setup::Quantity unit = quantityUnit(quantity);
    return QStringLiteral(" ") + (unit == setup::quantity::kNone
                                      ? QStringLiteral("rad")
                                      : QString::fromStdString(setup::unitLabel(units, unit)));
}

QString lengthText(setup::UnitSystem units)
{
    return QString::fromStdString(setup::unitLabel(units, setup::quantity::kLength));
}

// "node 12", or "bar 17, x = 2.5 m".
QString whereText(const core::Mesh& mesh, core::ElementIndex element, const core::Station& at,
                  core::NodeIndex node, setup::UnitSystem units)
{
    if (node >= 0) {
        return tr("node %1").arg(mesh.nodeId(node));
    }
    return tr("bar %1, x = %2 %3")
        .arg(mesh.elementId(element))
        .arg(formatResult(at.x), lengthText(units));
}

QString vectorText(const Eigen::Vector3d& v)
{
    return QStringLiteral("(%1, %2, %3)")
        .arg(formatResult(v.x()), formatResult(v.y()), formatResult(v.z()));
}

// The element of the bar that holds a sample.
core::ElementIndex elementOf(const core::ShapeSamples& shape, std::uint32_t sample)
{
    const auto after = std::ranges::upper_bound(shape.bars, sample, {}, &core::SampledBar::first);
    return after == shape.bars.begin() ? -1 : std::prev(after)->element;
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

QString cellText(const QVariant& value)
{
    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float) {
        return formatResult(value.toDouble());
    }
    return value.toString();
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

const core::StaticResult* staticOf(const setup::AnalysisResult& result)
{
    if (const auto* linear = std::get_if<core::StaticResult>(&result.data)) {
        return linear;
    }
    if (const auto* second = std::get_if<core::SecondOrderResult>(&result.data)) {
        return &second->result;
    }
    return nullptr;
}

render::ResultAnnotations annotationsFor(const AnnotationInput& in)
{
    render::ResultAnnotations out;
    if (in.result == nullptr || in.fields == nullptr || in.shape == nullptr ||
        in.field == nullptr || !in.fields->mesh) {
        return out;
    }
    const setup::AnalysisResult& result = *in.result;
    const core::Mesh& mesh = *in.fields->mesh;
    const bool buckling = isBuckling(result);
    const ResultQuantity quantity = in.field->quantity;
    const QString unit = unitText(quantity, in.units, buckling);
    const QString name = quantityName(quantity);

    // REQ-012-11: the legend.
    if (in.settings.colored) {
        render::ResultLegend legend;
        legend.title = quantityTitle(quantity, in.units, buckling);
        legend.min = in.display.min;
        legend.max = in.display.max;
        legend.bands = in.display.bands;
        for (const double tick : render::legendTicks(legend.min, legend.max, legend.bands)) {
            legend.ticks.emplace_back(tick, formatResult(tick));
        }
        out.legend = std::move(legend);
    }

    // The headline (REQ-012-02, 06, 14).
    if (!in.caseName.isEmpty()) {
        out.headline.append(in.caseName);
    }
    const QString how = in.settings.scaleMode == ScaleMode::Automatic ? tr("auto")
                        : in.settings.scaleMode == ScaleMode::User    ? tr("user")
                                                                      : tr("real");
    out.headline.append(tr("Scale \u00d7%1 (%2)").arg(formatResult(in.display.scale), how));
    if (const auto* modes = std::get_if<core::BucklingResult>(&result.data)) {
        const auto index = static_cast<std::size_t>(in.mode);
        if (index < modes->modes.size()) {
            out.headline.append(tr("Mode %1: factor %2")
                                    .arg(in.mode + 1)
                                    .arg(formatResult(modes->modes[index].factor)));
        }
    }
    if (!in.current) {
        out.headline.append(tr("Out of date: the model changed after this analysis."));
        out.warning = true;
    }

    // REQ-012-18: the query.
    if (in.query) {
        const render::ResultPick& pick = *in.query;
        const core::Station station = pick.station;
        QStringList lines;
        lines.append(whereText(mesh, pick.element, station, pick.node, in.units));
        if (in.settings.colored) {
            const double value = core::valueAt(*in.fields, pick.element, station, quantity);
            if (std::isfinite(value)) {
                lines.append(QStringLiteral("%1 = %2%3").arg(name, formatResult(value), unit));
            }
        }
        const Eigen::Vector3d u = core::displacementAt(*in.fields, pick.element, station);
        lines.append(QStringLiteral("u = %1%2")
                         .arg(vectorText(u),
                              buckling ? QString() : QStringLiteral(" ") + lengthText(in.units)));
        out.labels.push_back({.kind = render::ResultLabel::Kind::Query,
                              .at = core::positionAt(*in.fields, pick.element, station),
                              .displacement = u,
                              .text = lines.join(QLatin1Char('\n'))});
    }

    // REQ-012-17: the extremes of the quantity colored.
    const bool anyValue =
        std::ranges::any_of(in.field->values, [](double v) { return std::isfinite(v); });
    if (in.settings.extremes && in.settings.colored && anyValue) {
        for (const auto& [label, extreme] :
             {std::pair{tr("max"), in.field->max}, std::pair{tr("min"), in.field->min}}) {
            const std::uint32_t sample = extreme.sample;
            if (sample >= in.shape->stations.size()) {
                continue;
            }
            const core::Station& station = in.shape->stations[sample];
            const core::ElementIndex element = elementOf(*in.shape, sample);
            out.labels.push_back(
                {.kind = render::ResultLabel::Kind::Extreme,
                 .at = in.shape->positions[sample],
                 .displacement = in.shape->displacements[sample],
                 .text = QStringLiteral("%1 %2 = %3%4\n%5")
                             .arg(label, name, formatResult(extreme.value), unit,
                                  whereText(mesh, element, station, station.node, in.units))});
        }
    }

    // REQ-012-16: the reactions of the supported nodes.
    const core::StaticResult* solved = staticOf(result);
    if (in.settings.reactions && solved != nullptr) {
        double largestForce = 0.0;
        double largestMoment = 0.0;
        for (const core::NodeIndex node : result.supported) {
            const auto n = static_cast<std::size_t>(node);
            if (n < solved->reactions.size()) {
                largestForce = std::max(largestForce, solved->reactions[n].cwiseAbs().maxCoeff());
            }
            if (n < solved->reactionMoments.size()) {
                largestMoment =
                    std::max(largestMoment, solved->reactionMoments[n].cwiseAbs().maxCoeff());
            }
        }
        constexpr double kNegligible = 1e-9;
        const QString force =
            QString::fromStdString(setup::unitLabel(in.units, setup::quantity::kForce));
        const QString moment =
            QString::fromStdString(setup::unitLabel(in.units, setup::quantity::kMoment));
        static constexpr std::array<const char*, 3> kForces = {"Fx", "Fy", "Fz"};
        static constexpr std::array<const char*, 3> kMoments = {"Mx", "My", "Mz"};
        for (const core::NodeIndex node : result.supported) {
            const auto n = static_cast<std::size_t>(node);
            QStringList lines;
            const auto add = [&](const std::vector<Eigen::Vector3d>& values,
                                 const std::array<const char*, 3>& names, double largest,
                                 const QString& unitLabel) {
                if (n >= values.size()) {
                    return;
                }
                for (int k = 0; k < 3; ++k) {
                    const double v = values[n](k);
                    if (std::abs(v) > kNegligible * largest) {
                        lines.append(
                            QStringLiteral("%1 = %2 %3")
                                .arg(QString::fromUtf8(names.at(static_cast<std::size_t>(k))),
                                     formatResult(v), unitLabel));
                    }
                }
            };
            add(solved->reactions, kForces, largestForce, force);
            add(solved->reactionMoments, kMoments, largestMoment, moment);
            if (lines.isEmpty()) {
                continue;
            }
            out.labels.push_back({.kind = render::ResultLabel::Kind::Reaction,
                                  .at = mesh.nodeCoordinates(node),
                                  .displacement = n < solved->displacements.size()
                                                      ? solved->displacements[n]
                                                      : Eigen::Vector3d::Zero(),
                                  .text = lines.join(QLatin1Char('\n'))});
        }
    }
    return out;
}

} // namespace fem::app
