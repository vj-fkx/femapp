#pragma once

// Feature 010 (design §5.5, REQ-010-05): the editor of a thin-walled
// section: a table of its points (y, z) and one of its walls (from, to, t),
// numbered from 0 as in the core.

#include <fem/core/section.hpp>

#include <QWidget>

#include <optional>

class QTableWidget;

namespace fem::app {

class ThinWalledTable : public QWidget
{
    Q_OBJECT

public:
    // `lengthUnit` is shown in the headers.
    ThinWalledTable(const core::ThinWalled& shape, const QString& lengthUnit,
                    QWidget* parent = nullptr);

signals:
    // A complete, readable edit of the tables; invalid cells are marked and
    // nothing is emitted.
    void edited(const fem::core::ThinWalled& shape);

private:
    void cellChanged();
    [[nodiscard]] std::optional<core::ThinWalled> read();

    QTableWidget* m_points = nullptr;
    QTableWidget* m_walls = nullptr;
    bool m_filling = false;
};

} // namespace fem::app
