#include "results_panel.hpp"

#include "analysis_text.hpp"
#include "model_controller.hpp"
#include "result_display_settings.hpp"
#include "results_model.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QUndoStack>
#include <QVBoxLayout>

#include <optional>

namespace fem::app {

namespace {

QString tableName(ResultsModel::Table table)
{
    switch (table) {
    case ResultsModel::Table::Displacements:
        return ResultsPanel::tr("Displacements");
    case ResultsModel::Table::Reactions:
        return ResultsPanel::tr("Reactions");
    case ResultsModel::Table::BarForces:
        return ResultsPanel::tr("Bar forces");
    case ResultsModel::Table::Buckling:
        return ResultsPanel::tr("Buckling modes");
    }
    return {};
}

// REQ-012-19: the numbers of the tables as in the viewport.
class ResultsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    [[nodiscard]] QString displayText(const QVariant& value,
                                      const QLocale& /*locale*/) const override
    {
        return cellText(value);
    }
};

// The item of the current entry of the list of cases.
std::optional<setup::ItemId> currentItem(const QComboBox* cases)
{
    if (cases->currentIndex() < 0) {
        return std::nullopt;
    }
    return static_cast<setup::ItemId>(cases->currentData().toULongLong());
}

} // namespace

ResultsPanel::ResultsPanel(ModelController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by this widget.
    auto* layout = new QVBoxLayout(this);
    auto* choices = new QHBoxLayout;
    m_model = new ResultsModel(this);
    m_cases = new QComboBox(this);
    m_tables = new QComboBox(this);
    m_modes = new QComboBox(this);
    m_stale =
        new QLabel(tr("Out of date: the model changed after this analysis. Run it again."), this);
    m_empty = new QLabel(tr("No results yet: run an analysis (Analysis > Run analysis)."), this);
    m_view = new QTableView(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    layout->setContentsMargins(4, 4, 4, 4);
    m_cases->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    choices->addWidget(m_cases, 1);
    choices->addWidget(m_tables);
    choices->addWidget(m_modes);
    layout->addLayout(choices);
    m_stale->setWordWrap(true);
    m_stale->setStyleSheet(QStringLiteral("QLabel { color: #b35900; }"));
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty);
    layout->addWidget(m_stale);
    // NFR-011-04: a virtual model, rows of one height so that the view
    // need not measure them.
    m_view->setModel(m_model);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the view.
    m_view->setItemDelegate(new ResultsDelegate(m_view));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    layout->addWidget(m_view, 1);

    connect(m_cases, &QComboBox::currentIndexChanged, this, &ResultsPanel::showResult);
    connect(m_tables, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            m_model->setTable(static_cast<ResultsModel::Table>(m_tables->itemData(index).toInt()));
            fillModes();
        }
    });
    connect(m_modes, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            m_model->setMode(index);
            emitChosen();
        }
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &ResultsPanel::selectionChanged);
    // REQ-011-16: the warning follows every change of what was analysed.
    connect(controller, &ModelController::resultsChanged, this, &ResultsPanel::refresh);
    connect(controller, &ModelController::documentChanged, this, &ResultsPanel::refresh);
    connect(controller, &ModelController::meshChanged, this, &ResultsPanel::refresh);
    connect(controller->undoStack(), &QUndoStack::indexChanged, this, &ResultsPanel::refresh);
    refresh();
}

void ResultsPanel::showItem(setup::ItemId item)
{
    const int at = m_cases->findData(QVariant::fromValue(static_cast<quint64>(item)));
    if (at >= 0) {
        m_cases->setCurrentIndex(at);
    }
}

void ResultsPanel::refresh()
{
    const std::optional<setup::ItemId> was = currentItem(m_cases);
    {
        const QSignalBlocker block(m_cases);
        m_cases->clear();
        for (const setup::AnalysisResult& result : m_controller->results().all()) {
            m_cases->addItem(tr("%1 (%2)").arg(caseName(m_controller->document(), result.item),
                                               analysisTypeName(result.settings.type)),
                             QVariant::fromValue(static_cast<quint64>(result.item)));
        }
        const int at =
            was ? m_cases->findData(QVariant::fromValue(static_cast<quint64>(*was))) : -1;
        m_cases->setCurrentIndex(at >= 0 ? at : (m_cases->count() > 0 ? 0 : -1));
    }
    showResult();
}

void ResultsPanel::showResult()
{
    const std::optional<setup::ItemId> item = currentItem(m_cases);
    const setup::AnalysisResult* result = item ? m_controller->results().find(*item) : nullptr;
    m_model->setResult(result, m_controller->document().units());
    m_empty->setVisible(result == nullptr);
    m_cases->setEnabled(m_cases->count() > 0);
    m_stale->setVisible(result != nullptr &&
                        !setup::ResultSet::isCurrent(*result, m_controller->state()));
    fillTables();
    emitChosen();
}

void ResultsPanel::emitChosen()
{
    const std::optional<setup::ItemId> item = currentItem(m_cases);
    if (item && m_controller->results().find(*item) != nullptr) {
        emit resultChosen(static_cast<quint64>(*item),
                          m_model->modeCount() > 0 ? m_model->mode() : 0);
    }
}

void ResultsPanel::fillTables()
{
    const std::vector<ResultsModel::Table> tables = m_model->tables();
    {
        const QSignalBlocker block(m_tables);
        m_tables->clear();
        for (const ResultsModel::Table table : tables) {
            m_tables->addItem(tableName(table), static_cast<int>(table));
        }
        // The table shown before, when this result has it.
        int at = m_tables->findData(static_cast<int>(m_model->table()));
        if (at < 0 && !tables.empty()) {
            at = 0;
            m_model->setTable(tables.front());
        }
        m_tables->setCurrentIndex(at);
    }
    m_tables->setEnabled(!tables.empty());
    fillModes();
}

void ResultsPanel::fillModes()
{
    const QSignalBlocker block(m_modes);
    m_modes->clear();
    // REQ-011-14: the factor of each mode in the list of the modes.
    for (int mode = 0; mode < m_model->modeCount(); ++mode) {
        m_modes->addItem(tr("Mode %1: factor %2")
                             .arg(mode + 1)
                             .arg(QString::number(m_model->factor(mode), 'g', 4)));
    }
    m_modes->setCurrentIndex(m_model->modeCount() > 0 ? m_model->mode() : -1);
    m_modes->setVisible(m_model->table() == ResultsModel::Table::Buckling &&
                        m_model->modeCount() > 0);
}

void ResultsPanel::selectionChanged()
{
    // REQ-011-15: shown in the viewport; the window says when the mesh shown
    // is not the model's.
    const NamedIds ids = m_model->idsOf(m_view->selectionModel()->selectedRows());
    if (!ids.elements.empty() || !ids.nodes.empty()) {
        emit idsRequested(ids);
    }
}

} // namespace fem::app
