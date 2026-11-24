#pragma once

// Feature 010 (design §5.5, REQ-010-21): the dock of the check of the
// model: the problems of the build, those of core::validate and the
// warnings, for the active load case or combination.

#include "check_issues.hpp"

#include <fem/setup/document.hpp>

#include <QWidget>

#include <vector>

class QLabel;
class QTreeWidget;

namespace fem::app {

class ModelController;
class ModelTree;

class CheckPanel : public QWidget
{
    Q_OBJECT

public:
    CheckPanel(ModelController* controller, const ModelTree* tree, QWidget* parent = nullptr);

public slots:
    // Checks the model now and lists what was found.
    void check();

signals:
    // A problem clicked: its item (a setup::ItemId, as quint64: the moc
    // needs a type QDataStream knows on every platform), to select in the
    // tree...
    void itemRequested(quint64 id);
    // ...or the elements and nodes a problem of the core names.
    void idsRequested(const fem::app::NamedIds& ids);

private:
    ModelController* m_controller = nullptr;
    const ModelTree* m_tree = nullptr;
    QLabel* m_summary = nullptr;
    QTreeWidget* m_list = nullptr;
    std::vector<setup::BuildIssue> m_issues;
};

} // namespace fem::app
