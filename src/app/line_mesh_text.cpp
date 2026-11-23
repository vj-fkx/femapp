#include "line_mesh_text.hpp"

#include <fem/geom/command_input.hpp>

#include <QCoreApplication>

#include <cmath>
#include <cstdint>
#include <variant>
#include <vector>

namespace fem::app {

namespace {

// The translation context of these texts (REQ-009-16).
class LineMeshText
{
    Q_DECLARE_TR_FUNCTIONS(LineMeshText)
};

QString number(double value)
{
    return QString::number(value, 'g', 6);
}

QString point(const geom::Vec3& p)
{
    return QStringLiteral("(%1, %2, %3)").arg(number(p.x()), number(p.y()), number(p.z()));
}

QString ids(const std::vector<geom::EntityId>& entities)
{
    QStringList list;
    for (const geom::EntityId id : entities) {
        list.append(QString::number(static_cast<std::uint64_t>(id)));
    }
    return list.join(QStringLiteral(", "));
}

} // namespace

bool isMeshCommand(const QString& text)
{
    const QString word = text.trimmed().toUpper();
    return word == QStringLiteral("MESH") || word == QStringLiteral("ME");
}

std::optional<QString> completeMeshCommand(const QString& prefix)
{
    const QString word = prefix.trimmed().toUpper();
    if (!word.isEmpty() && QStringLiteral("MESH").startsWith(word)) {
        return QStringLiteral("MESH");
    }
    return std::nullopt;
}

MeshSize parseMeshSize(const QString& text)
{
    const auto input = geom::parseInput(text.trimmed().toStdString());
    if (!input) {
        return {.kind = MeshSize::Kind::Invalid, .length = 0.0};
    }
    if (std::holds_alternative<geom::Empty>(*input)) {
        return {.kind = MeshSize::Kind::Default, .length = 0.0};
    }
    if (const auto* distance = std::get_if<geom::Distance>(&*input);
        distance != nullptr && std::isfinite(distance->value) && distance->value > 0.0) {
        return {.kind = MeshSize::Kind::Length, .length = distance->value};
    }
    return {.kind = MeshSize::Kind::Invalid, .length = 0.0};
}

QString meshCommandName()
{
    return QStringLiteral("MESH");
}

QString meshPrompt()
{
    return LineMeshText::tr("Element size or <one per piece>:");
}

QString meshInvalidSize()
{
    return LineMeshText::tr("Invalid size.");
}

QString meshAlreadyRunning()
{
    return LineMeshText::tr("Meshing is already running.");
}

QString meshStarted()
{
    return LineMeshText::tr("Meshing the drawing...");
}

QString meshOutOfDate()
{
    return LineMeshText::tr("The mesh shown is out of date: run MESH again.");
}

QStringList meshReportLines(const mesher::LineMeshReport& report, int maxSplits)
{
    QStringList lines;
    lines.append(
        LineMeshText::tr("Mesh: %1 nodes, %2 elements.").arg(report.nodes).arg(report.elements));
    const auto splits = static_cast<int>(report.splits.size());
    QStringList details;
    details.append(LineMeshText::tr("%n split(s)", nullptr, splits));
    details.append(LineMeshText::tr("%n node(s) merged", nullptr, report.mergedNodes));
    details.append(
        LineMeshText::tr("%n piece(s) ignored", nullptr, static_cast<int>(report.ignored.size())));
    lines.append(details.join(QStringLiteral("; ")) + QLatin1Char('.'));
    for (int i = 0; i < splits && i < maxSplits; ++i) {
        const mesher::Split& split = report.splits[static_cast<std::size_t>(i)];
        QString kind;
        switch (split.kind) {
        case mesher::SplitKind::TJoint:
            kind = LineMeshText::tr("T joint");
            break;
        case mesher::SplitKind::Crossing:
            kind = LineMeshText::tr("Crossing");
            break;
        case mesher::SplitKind::PointOnLine:
            kind = LineMeshText::tr("Point on a line");
            break;
        }
        lines.append(LineMeshText::tr("  %1 at %2: entities %3")
                         .arg(kind, point(split.position), ids(split.entities)));
    }
    if (splits > maxSplits) {
        lines.append(LineMeshText::tr("  ... and %n more.", nullptr, splits - maxSplits));
    }
    return lines;
}

QStringList meshErrorLines(const mesher::LineMeshError& error)
{
    QStringList lines;
    switch (error.kind) {
    case mesher::LineMeshError::Kind::NoLines:
        lines.append(LineMeshText::tr("Could not mesh the drawing: the drawing has no lines."));
        break;
    case mesher::LineMeshError::Kind::Overlaps:
        lines.append(LineMeshText::tr("Could not mesh the drawing: %n pair(s) of lines overlap.",
                                      nullptr, static_cast<int>(error.overlaps.size())));
        for (const mesher::Overlap& overlap : error.overlaps) {
            lines.append(LineMeshText::tr("  Entities %1 and %2 overlap from %3 to %4.")
                             .arg(QString::number(static_cast<std::uint64_t>(overlap.first)),
                                  QString::number(static_cast<std::uint64_t>(overlap.second)),
                                  point(overlap.from), point(overlap.to)));
        }
        break;
    case mesher::LineMeshError::Kind::InvalidSizing:
        lines.append(
            LineMeshText::tr("Could not mesh the drawing: invalid element sizes (entities %1).")
                .arg(ids(error.items)));
        break;
    case mesher::LineMeshError::Kind::UnknownEntity:
        lines.append(LineMeshText::tr("Could not mesh the drawing: unknown entities %1.")
                         .arg(ids(error.items)));
        break;
    }
    return lines;
}

} // namespace fem::app
