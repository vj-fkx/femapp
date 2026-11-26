#pragma once

// Feature 011 (REQ-011-07, 10, 13; design §5.6): the words of the analysis,
// apart from the widgets so that they can be tested without them.

#include <fem/core/expected.hpp>
#include <fem/core/run_control.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/analysis_settings.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

#include <QString>
#include <QStringList>

namespace fem::app {

// The name of a load case or combination; "#id" when it is not in the
// document.
[[nodiscard]] QString caseName(const setup::Document& document, setup::ItemId item);
// "linear", "second order", "buckling".
[[nodiscard]] QString analysisTypeName(setup::AnalysisType type);

// REQ-011-07: "Analysing ULS (2 of 5): iteration 3, assembly 40%"; without
// "k of n" for a single item, without the percentage when it is unknown.
// `index` counts from 0.
[[nodiscard]] QString progressText(const QString& name, int index, int count,
                                   const core::RunProgress& progress, bool building);
[[nodiscard]] QString analysisAlreadyRunning();

// REQ-011-13: the summary of a result, in the units of the document, with
// the external ids; its first line names the case and the analysis.
[[nodiscard]] QStringList summaryLines(const QString& name, const setup::AnalysisResult& result,
                                       setup::UnitSystem units);

// REQ-011-05, 08, 10: a case refused by the check, failed in the core (its
// message) or cancelled.
[[nodiscard]] QString refusedText(const QString& name, int errors);
[[nodiscard]] QString failureText(const QString& name, const core::Error& error);
// The end of a run of several items: how many were analysed, and those not.
[[nodiscard]] QString runAllText(int analysed, const QStringList& notAnalysed, bool cancelled);

} // namespace fem::app
