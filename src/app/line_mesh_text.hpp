#pragma once

// Feature 009 (REQ-009-12 to 16): the words of the MESH command, apart from
// the controller so that they can be tested without widgets.

#include <fem/mesher/line_mesher.hpp>

#include <QString>
#include <QStringList>

#include <optional>

namespace fem::app {

// REQ-009-12: MESH or ME, in any case.
[[nodiscard]] bool isMeshCommand(const QString& text);
// Tab: "MESH" when `prefix` (not empty) starts it, in any case.
[[nodiscard]] std::optional<QString> completeMeshCommand(const QString& prefix);

// REQ-009-12: what was typed at the size prompt.
struct MeshSize
{
    enum class Kind : std::uint8_t { Default, Length, Invalid };
    Kind kind = Kind::Default;
    double length = 0.0;
};
// Enter: the default; a number > 0: the target length; anything else is
// invalid. The decimal separator is the point, as in the 005 commands.
[[nodiscard]] MeshSize parseMeshSize(const QString& text);

[[nodiscard]] QString meshCommandName();
[[nodiscard]] QString meshPrompt();
[[nodiscard]] QString meshInvalidSize();
[[nodiscard]] QString meshAlreadyRunning();
[[nodiscard]] QString meshStarted();
[[nodiscard]] QString meshOutOfDate();

// REQ-009-11, 13: the report, its first line the counts, then one line per
// split up to `maxSplits` and a line with how many more.
[[nodiscard]] QStringList meshReportLines(const mesher::LineMeshReport& report, int maxSplits = 20);
// REQ-009-14: why the drawing could not be meshed, one line per overlap.
[[nodiscard]] QStringList meshErrorLines(const mesher::LineMeshError& error);

} // namespace fem::app
