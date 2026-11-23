#include "thin_walled_table.hpp"

#include "fields.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fem::app {

namespace {

void setCell(QTableWidget* table, int row, int column, const QString& text)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the table takes it.
    table->setItem(row, column, new QTableWidgetItem(text));
}

void markCell(QTableWidgetItem* item, bool invalid)
{
    if (item != nullptr) {
        item->setBackground(invalid ? QColor(240, 190, 190) : QBrush());
        item->setToolTip(invalid ? ThinWalledTable::tr("Invalid value") : QString());
    }
}

std::optional<double> numberIn(QTableWidget* table, int row, int column)
{
    QTableWidgetItem* item = table->item(row, column);
    const auto parsed = item != nullptr ? parseNumber(item->text()) : std::nullopt;
    const auto* d = parsed ? std::get_if<double>(&*parsed) : nullptr;
    markCell(item, d == nullptr);
    return d != nullptr ? std::optional<double>(*d) : std::nullopt;
}

} // namespace

ThinWalledTable::ThinWalledTable(const core::ThinWalled& shape, const QString& lengthUnit,
                                 QWidget* parent)
    : QWidget(parent)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const QString unit = lengthUnit.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(lengthUnit);
    m_points = new QTableWidget(static_cast<int>(shape.points.size()), 2, this);
    m_points->setHorizontalHeaderLabels({tr("y") + unit, tr("z") + unit});
    m_walls = new QTableWidget(static_cast<int>(shape.walls.size()), 3, this);
    m_walls->setHorizontalHeaderLabels({tr("From"), tr("To"), tr("t") + unit});
    const auto addTable = [&](const QString& title, QTableWidget* table) {
        layout->addWidget(new QLabel(title, this));
        // The rows numbered from 0, as the walls refer to the points.
        table->verticalHeader()->setVisible(true);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setMinimumHeight(120);
        layout->addWidget(table);
        auto* buttons = new QHBoxLayout;
        auto* add = new QPushButton(tr("Add"), this);
        auto* remove = new QPushButton(tr("Remove"), this);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch();
        layout->addLayout(buttons);
        connect(add, &QPushButton::clicked, this, [this, table] {
            m_filling = true;
            const int row = table->rowCount();
            table->insertRow(row);
            for (int c = 0; c < table->columnCount(); ++c) {
                setCell(table, row, c, QStringLiteral("0"));
            }
            m_filling = false;
            cellChanged();
        });
        connect(remove, &QPushButton::clicked, this, [this, table] {
            const int row = table->currentRow() >= 0 ? table->currentRow() : table->rowCount() - 1;
            if (row >= 0) {
                table->removeRow(row);
                cellChanged();
            }
        });
        connect(table, &QTableWidget::cellChanged, this, &ThinWalledTable::cellChanged);
    };
    // NOLINTEND(cppcoreguidelines-owning-memory)

    m_filling = true;
    for (std::size_t i = 0; i < shape.points.size(); ++i) {
        const auto row = static_cast<int>(i);
        setCell(m_points, row, 0, formatNumber(shape.points[i].x()));
        setCell(m_points, row, 1, formatNumber(shape.points[i].y()));
    }
    for (std::size_t i = 0; i < shape.walls.size(); ++i) {
        const auto row = static_cast<int>(i);
        const core::ThinWalled::Wall& wall = shape.walls[i];
        setCell(m_walls, row, 0, QString::number(wall.from));
        setCell(m_walls, row, 1, QString::number(wall.to));
        setCell(m_walls, row, 2, formatNumber(wall.t));
    }
    // The point numbers from 0 in the row headers.
    const auto numberRows = [](QTableWidget* table) {
        QStringList labels;
        for (int r = 0; r < table->rowCount(); ++r) {
            labels.append(QString::number(r));
        }
        table->setVerticalHeaderLabels(labels);
    };
    numberRows(m_points);
    numberRows(m_walls);
    connect(m_points->model(), &QAbstractItemModel::rowsInserted, this,
            [this, numberRows] { numberRows(m_points); });
    connect(m_points->model(), &QAbstractItemModel::rowsRemoved, this,
            [this, numberRows] { numberRows(m_points); });
    connect(m_walls->model(), &QAbstractItemModel::rowsInserted, this,
            [this, numberRows] { numberRows(m_walls); });
    connect(m_walls->model(), &QAbstractItemModel::rowsRemoved, this,
            [this, numberRows] { numberRows(m_walls); });
    addTable(tr("Points"), m_points);
    addTable(tr("Walls"), m_walls);
    m_filling = false;
}

void ThinWalledTable::cellChanged()
{
    if (m_filling) {
        return;
    }
    // Marking a cell changes it too: no edits while the tables are read.
    m_filling = true;
    const auto shape = read();
    m_filling = false;
    if (shape) {
        emit edited(*shape);
    }
}

std::optional<core::ThinWalled> ThinWalledTable::read()
{
    core::ThinWalled shape;
    bool ok = true;
    for (int r = 0; r < m_points->rowCount(); ++r) {
        const auto y = numberIn(m_points, r, 0);
        const auto z = numberIn(m_points, r, 1);
        ok = ok && y && z;
        shape.points.emplace_back(y.value_or(0.0), z.value_or(0.0));
    }
    const int count = m_points->rowCount();
    for (int r = 0; r < m_walls->rowCount(); ++r) {
        // The ends: whole numbers of points in the table.
        const auto end = [&](int column) -> std::optional<int> {
            QTableWidgetItem* item = m_walls->item(r, column);
            bool isInt = false;
            const int i = item != nullptr ? item->text().trimmed().toInt(&isInt) : -1;
            const bool valid = isInt && i >= 0 && i < count;
            markCell(item, !valid);
            return valid ? std::optional<int>(i) : std::nullopt;
        };
        const auto from = end(0);
        const auto to = end(1);
        auto t = numberIn(m_walls, r, 2);
        if (t && *t <= 0.0) {
            markCell(m_walls->item(r, 2), true);
            t.reset();
        }
        ok = ok && from && to && t;
        shape.walls.push_back(
            {.from = from.value_or(0), .to = to.value_or(0), .t = t.value_or(0.0)});
    }
    return ok ? std::optional<core::ThinWalled>(std::move(shape)) : std::nullopt;
}

} // namespace fem::app
