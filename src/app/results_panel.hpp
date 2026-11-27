#pragma once

// Feature 011 (design §5.7; REQ-011-14, 15, 16): the Results dock: the load
// cases and combinations with a result, the table and the mode shown, a
// warning while the result is out of date, and the rows selected shown in
// the viewport.

#include "check_issues.hpp"

#include <fem/setup/document.hpp>

#include <QWidget>

class QComboBox;
class QLabel;
class QTableView;

namespace fem::app {

class ModelController;
class ResultsModel;

class ResultsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ResultsPanel(ModelController* controller, QWidget* parent = nullptr);

    // Shows the result of an item, if it has one.
    void showItem(setup::ItemId item);

signals:
    // REQ-011-15: the nodes or elements of the rows selected.
    void idsRequested(const fem::app::NamedIds& ids);
    // REQ-012-04: the result shown changed: its item (a setup::ItemId; a
    // plain integer for the meta-type system on every platform) and, for
    // buckling, the mode (0 otherwise).
    void resultChosen(quint64 item, int mode);

private:
    // The list, the table and the warning again: after every change of the
    // results, the document, the drawing or the mesh. The model is given
    // the result again each time, since a change of the results may move
    // it (ResultsModel::setResult).
    void refresh();
    void showResult();
    void fillTables();
    void fillModes();
    void selectionChanged();
    void emitChosen();

    ModelController* m_controller = nullptr;
    ResultsModel* m_model = nullptr;
    QComboBox* m_cases = nullptr;
    QComboBox* m_tables = nullptr;
    QComboBox* m_modes = nullptr;
    QLabel* m_stale = nullptr;
    QLabel* m_empty = nullptr;
    QTableView* m_view = nullptr;
};

} // namespace fem::app
