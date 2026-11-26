#include "results_model.hpp"

#include <fem/core/stability_analysis.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <variant>

namespace fem::app {

namespace {

constexpr int kNodeColumns = 7; // id, three displacements, three rotations
constexpr int kBarColumns = 8;  // id, end, six forces
constexpr int kEnds = 2;
constexpr int kForce = 3; // the columns of a group of three

bool anyNonZero(const std::vector<double>& values)
{
    return std::ranges::any_of(values, [](double v) { return v != 0.0; });
}

QString unit(setup::UnitSystem units, setup::Quantity quantity)
{
    return QString::fromStdString(setup::unitLabel(units, quantity));
}

QString withUnit(const char* name, const QString& unit)
{
    return QStringLiteral("%1 [%2]").arg(QString::fromUtf8(name), unit);
}

double component(const Eigen::Vector3d& v, int k)
{
    return v[k];
}

} // namespace

ResultsModel::ResultsModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    updateColumns();
}

void ResultsModel::setResult(const setup::AnalysisResult* result, setup::UnitSystem units)
{
    beginResetModel();
    m_result = result;
    m_units = units;
    m_bars.clear();
    if (const core::StaticResult* r = staticResult()) {
        const auto elements =
            static_cast<core::ElementIndex>(std::max(r->beams.size(), r->axialForce.size()));
        for (core::ElementIndex e = 0; e < elements; ++e) {
            const auto at = static_cast<std::size_t>(e);
            const bool beam = at < r->beams.size() && r->beams[at].has_value();
            const bool truss = at < r->axialForce.size() && std::isfinite(r->axialForce[at]);
            if (beam || truss) {
                m_bars.push_back(e);
            }
        }
    }
    m_mode = std::clamp(m_mode, 0, std::max(0, modeCount() - 1));
    updateColumns();
    endResetModel();
}

std::vector<ResultsModel::Table> ResultsModel::tables() const
{
    if (m_result == nullptr) {
        return {};
    }
    if (std::holds_alternative<core::BucklingResult>(m_result->data)) {
        return {Table::Buckling};
    }
    return {Table::Displacements, Table::Reactions, Table::BarForces};
}

void ResultsModel::setTable(Table table)
{
    if (table != m_table) {
        beginResetModel();
        m_table = table;
        updateColumns();
        endResetModel();
    }
}

void ResultsModel::setMode(int mode)
{
    mode = std::clamp(mode, 0, std::max(0, modeCount() - 1));
    if (mode != m_mode) {
        beginResetModel();
        m_mode = mode;
        updateColumns();
        endResetModel();
    }
}

int ResultsModel::modeCount() const
{
    if (m_result == nullptr) {
        return 0;
    }
    const auto* buckling = std::get_if<core::BucklingResult>(&m_result->data);
    return buckling != nullptr ? static_cast<int>(buckling->modes.size()) : 0;
}

double ResultsModel::factor(int mode) const
{
    if (mode < 0 || mode >= modeCount()) {
        return 0.0;
    }
    return std::get<core::BucklingResult>(m_result->data)
        .modes[static_cast<std::size_t>(mode)]
        .factor;
}

const core::StaticResult* ResultsModel::staticResult() const
{
    if (m_result == nullptr) {
        return nullptr;
    }
    if (const auto* r = std::get_if<core::StaticResult>(&m_result->data)) {
        return r;
    }
    if (const auto* r = std::get_if<core::SecondOrderResult>(&m_result->data)) {
        return &r->result;
    }
    return nullptr;
}

const core::BucklingMode* ResultsModel::bucklingMode() const
{
    if (m_mode >= modeCount()) {
        return nullptr;
    }
    return &std::get<core::BucklingResult>(m_result->data).modes[static_cast<std::size_t>(m_mode)];
}

const core::Mesh* ResultsModel::mesh() const
{
    if (m_result == nullptr) {
        return nullptr;
    }
    if (const core::StaticResult* r = staticResult(); r != nullptr && r->mesh) {
        return r->mesh.get();
    }
    return m_result->mesh.get();
}

bool ResultsModel::hasWarping() const
{
    if (m_table == Table::Buckling) {
        const core::BucklingMode* mode = bucklingMode();
        return mode != nullptr && anyNonZero(mode->warping);
    }
    const core::StaticResult* r = staticResult();
    return r != nullptr && anyNonZero(r->warping);
}

bool ResultsModel::hasBimoments() const
{
    const core::StaticResult* r = staticResult();
    return r != nullptr && anyNonZero(r->reactionBimoments);
}

int ResultsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || mesh() == nullptr) {
        return 0;
    }
    switch (m_table) {
    case Table::Displacements:
        return staticResult() != nullptr ? static_cast<int>(staticResult()->displacements.size())
                                         : 0;
    case Table::Reactions:
        return staticResult() != nullptr ? static_cast<int>(m_result->supported.size()) : 0;
    case Table::BarForces:
        return kEnds * static_cast<int>(m_bars.size());
    case Table::Buckling:
        return bucklingMode() != nullptr ? static_cast<int>(bucklingMode()->displacements.size())
                                         : 0;
    }
    return 0;
}

int ResultsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_columns;
}

void ResultsModel::updateColumns()
{
    m_columns = 0;
    switch (m_table) {
    case Table::Displacements:
    case Table::Buckling:
        m_columns = kNodeColumns + (hasWarping() ? 1 : 0);
        break;
    case Table::Reactions:
        m_columns = kNodeColumns + (hasBimoments() ? 1 : 0);
        break;
    case Table::BarForces:
        m_columns = kBarColumns;
        break;
    }
}

int ResultsModel::itemOf(int row) const
{
    switch (m_table) {
    case Table::Reactions:
        return m_result->supported[static_cast<std::size_t>(row)];
    case Table::BarForces:
        return m_bars[static_cast<std::size_t>(row / kEnds)];
    case Table::Displacements:
    case Table::Buckling:
        break;
    }
    return row;
}

QVariant ResultsModel::value(int row, int column) const
{
    const int item = itemOf(row);
    const auto at = static_cast<std::size_t>(item);
    if (column == 0) {
        const core::Mesh* m = mesh();
        return QVariant::fromValue(static_cast<qlonglong>(
            m_table == Table::BarForces ? m->elementId(item) : m->nodeId(item)));
    }
    const core::StaticResult* r = staticResult();
    switch (m_table) {
    case Table::Displacements:
        if (column <= kForce) {
            return component(r->displacements[at], column - 1);
        }
        if (column < kNodeColumns) {
            return component(r->rotations[at], column - 1 - kForce);
        }
        return at < r->warping.size() ? r->warping[at] : 0.0;
    case Table::Reactions:
        if (column <= kForce) {
            return component(r->reactions[at], column - 1);
        }
        if (column < kNodeColumns) {
            return component(r->reactionMoments[at], column - 1 - kForce);
        }
        return at < r->reactionBimoments.size() ? r->reactionBimoments[at] : 0.0;
    case Table::BarForces: {
        const bool start = row % kEnds == 0;
        if (column == 1) {
            return start ? tr("start") : tr("end");
        }
        if (const auto* beam = at < r->beams.size() ? &r->beams[at] : nullptr;
            beam != nullptr && beam->has_value()) {
            const core::BeamForces& f = start ? (*beam)->start() : (*beam)->end();
            const std::array<double, 6> forces{f.n, f.vy, f.vz, f.t, f.my, f.mz};
            return forces.at(static_cast<std::size_t>(column - 2));
        }
        // A truss: N alone, the same at both ends.
        return column == 2 ? QVariant(r->axialForce[at]) : QVariant();
    }
    case Table::Buckling: {
        const core::BucklingMode* mode = bucklingMode();
        if (column <= kForce) {
            return component(mode->displacements[at], column - 1);
        }
        if (column < kNodeColumns) {
            return at < mode->rotations.size() ? component(mode->rotations[at], column - 1 - kForce)
                                               : 0.0;
        }
        return at < mode->warping.size() ? mode->warping[at] : 0.0;
    }
    }
    return {};
}

QVariant ResultsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount()) {
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    return value(index.row(), index.column());
}

QVariant ResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 ||
        section >= columnCount()) {
        return {};
    }
    const QString length = unit(m_units, setup::quantity::kLength);
    const QString force = unit(m_units, setup::quantity::kForce);
    const QString moment = unit(m_units, setup::quantity::kMoment);
    const QString rad = QStringLiteral("rad");
    const QString perLength = unit(m_units, setup::Quantity{.length = -1});
    static constexpr std::array<const char*, kNodeColumns + 1> kMotions{"",   "u",  "v",  "w",
                                                                        "θx", "θy", "θz", "θ'"};
    static constexpr std::array<const char*, kNodeColumns + 1> kReactions{"",   "Rx", "Ry", "Rz",
                                                                          "Mx", "My", "Mz", "B"};
    static constexpr std::array<const char*, kBarColumns> kForces{"",   "",  "N",  "Vy",
                                                                  "Vz", "T", "My", "Mz"};
    const auto at = static_cast<std::size_t>(section);
    switch (m_table) {
    case Table::Displacements:
        if (section == 0) {
            return tr("Node");
        }
        return withUnit(kMotions.at(at),
                        section <= kForce ? length : (section < kNodeColumns ? rad : perLength));
    case Table::Reactions:
        if (section == 0) {
            return tr("Node");
        }
        return withUnit(
            kReactions.at(at),
            section <= kForce
                ? force
                : (section < kNodeColumns ? moment : unit(m_units, setup::quantity::kBimoment)));
    case Table::BarForces:
        if (section == 0) {
            return tr("Element");
        }
        if (section == 1) {
            return tr("End");
        }
        return withUnit(kForces.at(at), section < 2 + kForce ? force : moment);
    case Table::Buckling:
        // Normalized as in the core: no units.
        return section == 0 ? tr("Node") : QString::fromUtf8(kMotions.at(at));
    }
    return {};
}

NamedIds ResultsModel::idsOf(const QModelIndexList& rows) const
{
    NamedIds ids;
    const core::Mesh* m = mesh();
    if (m == nullptr) {
        return ids;
    }
    const int count = rowCount();
    for (const QModelIndex& index : rows) {
        if (!index.isValid() || index.row() >= count) {
            continue;
        }
        const int item = itemOf(index.row());
        if (m_table == Table::BarForces) {
            ids.elements.push_back(m->elementId(item));
        } else {
            ids.nodes.push_back(m->nodeId(item));
        }
    }
    for (std::vector<core::ExternalId>* list : {&ids.elements, &ids.nodes}) {
        std::ranges::sort(*list);
        const auto [first, last] = std::ranges::unique(*list);
        list->erase(first, last);
    }
    return ids;
}

} // namespace fem::app
