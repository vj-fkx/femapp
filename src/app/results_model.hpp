#pragma once

// Feature 011 (design §5.7; REQ-011-14, 15, NFR-011-04): the tables of the
// results of an analysis, a virtual table model: data() reads the result,
// no row is copied.

#include "check_issues.hpp"

#include <fem/core/mesh.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/units.hpp>

#include <QAbstractTableModel>
#include <QModelIndexList>

#include <cstdint>
#include <vector>

namespace fem::app {

class ResultsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class Table : std::uint8_t {
        Displacements, // per node: u, v, w, θx, θy, θz (and the warping)
        Reactions,     // per supported node: Rx, Ry, Rz, Mx, My, Mz (and B)
        BarForces,     // per bar, two rows (start, end): N, Vy, Vz, T, My, Mz
        Buckling       // per node, the displacement of the chosen mode
    };

    explicit ResultsModel(QObject* parent = nullptr);

    // The result shown, nullptr for none. It is not copied: whoever owns it
    // sets it again (or nullptr) before it changes or goes, as the panel
    // does on every resultsChanged of the controller.
    void setResult(const setup::AnalysisResult* result, setup::UnitSystem units);
    [[nodiscard]] const setup::AnalysisResult* result() const noexcept { return m_result; }
    // The tables a result has: the static ones, or the buckling one.
    [[nodiscard]] std::vector<Table> tables() const;
    void setTable(Table table);
    [[nodiscard]] Table table() const noexcept { return m_table; }
    // The mode of the buckling table, from 0.
    void setMode(int mode);
    [[nodiscard]] int mode() const noexcept { return m_mode; }
    [[nodiscard]] int modeCount() const;
    [[nodiscard]] double factor(int mode) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override;

    // REQ-011-15: the nodes or elements of some rows, unique, by their
    // external ids.
    [[nodiscard]] NamedIds idsOf(const QModelIndexList& rows) const;

private:
    [[nodiscard]] const core::StaticResult* staticResult() const;
    [[nodiscard]] const core::BucklingMode* bucklingMode() const;
    [[nodiscard]] const core::Mesh* mesh() const;
    [[nodiscard]] bool hasWarping() const;
    [[nodiscard]] bool hasBimoments() const;
    // The internal index of the node or element of a row.
    [[nodiscard]] int itemOf(int row) const;
    [[nodiscard]] QVariant value(int row, int column) const;
    // The columns of what is shown, kept so that data() does not look for
    // the warping in every cell (NFR-011-04); called inside each reset.
    void updateColumns();

    const setup::AnalysisResult* m_result = nullptr;
    setup::UnitSystem m_units{};
    Table m_table = Table::Displacements;
    int m_mode = 0;
    std::vector<core::ElementIndex> m_bars; // the elements with forces
    int m_columns = 0;
};

} // namespace fem::app
