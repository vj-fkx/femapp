#include "analysis_text.hpp"

#include <QCoreApplication>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>

namespace fem::app {

namespace {

// The translation context of these texts (REQ-011-13).
class AnalysisText
{
    Q_DECLARE_TR_FUNCTIONS(AnalysisText)
};

// Four significant digits, as the rest of the console.
QString number(double value)
{
    return QString::number(value, 'g', 4);
}

QString unit(setup::UnitSystem units, setup::Quantity quantity)
{
    return QString::fromStdString(setup::unitLabel(units, quantity));
}

QString axis(int direction)
{
    static constexpr std::array<const char*, 3> kAxes{"x", "y", "z"};
    return QString::fromLatin1(kAxes.at(static_cast<std::size_t>(direction)));
}

QString vector(const Eigen::Vector3d& v)
{
    return QStringLiteral("(%1, %2, %3)").arg(number(v.x()), number(v.y()), number(v.z()));
}

void staticLines(QStringList& lines, const setup::Summary& s, setup::UnitSystem units)
{
    const QString force = unit(units, setup::quantity::kForce);
    const QString moment = unit(units, setup::quantity::kMoment);
    if (s.displacement) {
        lines << AnalysisText::tr("Largest displacement: %1 %2 at the node %3 in %4")
                     .arg(number(s.displacement->value), unit(units, setup::quantity::kLength))
                     .arg(s.displacement->id)
                     .arg(axis(s.displacement->direction));
    }
    if (s.rotation) {
        lines << AnalysisText::tr("Largest rotation: %1 rad at the node %2 about %3")
                     .arg(number(s.rotation->value))
                     .arg(s.rotation->id)
                     .arg(axis(s.rotation->direction));
    }
    lines << AnalysisText::tr("Sum of the reactions: %1 %2; of the loads: %3 %2")
                 .arg(vector(s.reactions), force, vector(s.applied));
    static constexpr std::array<const char*, 6> kForces{"N", "Vy", "Vz", "T", "My", "Mz"};
    for (std::size_t k = 0; k < s.barForces.size(); ++k) {
        if (const auto& f = s.barForces[k]) {
            lines << AnalysisText::tr("Largest %1: %2 %3 in the element %4")
                         .arg(QString::fromLatin1(kForces.at(k)), number(f->value),
                              k < 3 ? force : moment)
                         .arg(f->id);
        }
    }
}

} // namespace

QString caseName(const setup::Document& document, setup::ItemId item)
{
    if (const setup::Item* found = document.find(item)) {
        if (const auto* c = std::get_if<setup::LoadCase>(found)) {
            return QString::fromStdString(c->name);
        }
        if (const auto* c = std::get_if<setup::Combination>(found)) {
            return QString::fromStdString(c->name);
        }
    }
    return QStringLiteral("#%1").arg(static_cast<qulonglong>(item));
}

QString analysisTypeName(setup::AnalysisType type)
{
    switch (type) {
    case setup::AnalysisType::SecondOrder:
        return AnalysisText::tr("second order");
    case setup::AnalysisType::Buckling:
        return AnalysisText::tr("buckling");
    case setup::AnalysisType::Linear:
        break;
    }
    return AnalysisText::tr("linear");
}

QString progressText(const QString& name, int index, int count, const core::RunProgress& progress,
                     bool building)
{
    const QString text =
        count > 1 ? AnalysisText::tr("Analysing %1 (%2 of %3)").arg(name).arg(index + 1).arg(count)
                  : AnalysisText::tr("Analysing %1").arg(name);
    if (building) {
        return text + AnalysisText::tr(": building the model");
    }
    QString stage;
    switch (progress.stage) {
    case core::RunStage::Assembly:
        stage = AnalysisText::tr("assembly");
        break;
    case core::RunStage::Factorization:
        stage = AnalysisText::tr("factorization");
        break;
    case core::RunStage::Solution:
        stage = AnalysisText::tr("solution");
        break;
    case core::RunStage::Recovery:
        stage = AnalysisText::tr("bar forces");
        break;
    case core::RunStage::Iteration:
        stage = AnalysisText::tr("iterating");
        break;
    }
    if (progress.fraction >= 0.0) {
        stage += QStringLiteral(" %1%").arg(std::lround(100.0 * progress.fraction));
    }
    if (progress.iteration > 0 && progress.stage != core::RunStage::Iteration) {
        stage = AnalysisText::tr("iteration %1, %2").arg(progress.iteration).arg(stage);
    } else if (progress.iteration > 0) {
        stage = AnalysisText::tr("iteration %1").arg(progress.iteration);
    }
    return text + QStringLiteral(": ") + stage;
}

QString analysisAlreadyRunning()
{
    return AnalysisText::tr("An analysis is running: cancel it, or wait until it ends.");
}

QStringList summaryLines(const QString& name, const setup::AnalysisResult& result,
                         setup::UnitSystem units)
{
    const setup::Summary s = setup::summarize(result);
    QStringList lines;
    lines << AnalysisText::tr("%1: %2 analysis").arg(name, analysisTypeName(result.settings.type));
    if (std::holds_alternative<core::BucklingResult>(result.data)) {
        for (std::size_t m = 0; m < s.factors.size(); ++m) {
            lines
                << AnalysisText::tr("Buckling factor %1: %2").arg(m + 1).arg(number(s.factors[m]));
        }
        if (!s.note.empty()) {
            lines << QString::fromStdString(s.note);
        }
        return lines;
    }
    staticLines(lines, s, units);
    if (std::holds_alternative<core::SecondOrderResult>(result.data)) {
        lines << AnalysisText::tr("Converged in %1 iterations (last change %2)")
                     .arg(s.iterations)
                     .arg(number(s.change));
    }
    return lines;
}

QString refusedText(const QString& name, int errors)
{
    return errors == 1
               ? AnalysisText::tr("%1 was not analysed: the check found a problem.").arg(name)
               : AnalysisText::tr("%1 was not analysed: the check found %2 problems.")
                     .arg(name)
                     .arg(errors);
}

QString failureText(const QString& name, const core::Error& error)
{
    if (error.cancelled) {
        return AnalysisText::tr("The analysis of %1 was cancelled.").arg(name);
    }
    return AnalysisText::tr("%1: %2").arg(name, QString::fromStdString(error.message));
}

QString runAllText(int analysed, const QStringList& notAnalysed, bool cancelled)
{
    QString text = AnalysisText::tr("Run all: %1 analysed").arg(analysed);
    if (!notAnalysed.isEmpty()) {
        text += AnalysisText::tr(", not analysed: %1").arg(notAnalysed.join(QStringLiteral(", ")));
    }
    if (cancelled) {
        text += AnalysisText::tr(" (cancelled)");
    }
    return text + QLatin1Char('.');
}

} // namespace fem::app
