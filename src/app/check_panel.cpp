#include "check_panel.hpp"

#include "model_controller.hpp"
#include "model_tree.hpp"

#include <fem/setup/editing.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace fem::app {

CheckPanel::CheckPanel(ModelController* controller, const ModelTree* tree, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_tree(tree)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by this widget.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout;
    m_summary = new QLabel(tr("Not checked yet."), this);
    m_summary->setWordWrap(true);
    auto* again = new QPushButton(tr("Check"), this);
    top->addWidget(m_summary, 1);
    top->addWidget(again);
    layout->addLayout(top);
    m_list = new QTreeWidget(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Severity"), tr("Problem"), tr("Item")});
    m_list->setRootIsDecorated(false);
    m_list->setWordWrap(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(false);
    layout->addWidget(m_list);
    connect(again, &QPushButton::clicked, this, &CheckPanel::check);
    // REQ-010-21: a click selects the item, or shows what the core names.
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* row) {
        const int at = m_list->indexOfTopLevelItem(row);
        if (at < 0 || static_cast<std::size_t>(at) >= m_issues.size()) {
            return;
        }
        const setup::BuildIssue& issue = m_issues[static_cast<std::size_t>(at)];
        if (issue.item) {
            emit itemRequested(static_cast<quint64>(*issue.item));
        } else if (issue.kind == setup::IssueKind::Core) {
            const NamedIds ids = idsInMessage(issue.detail);
            if (!ids.elements.empty() || !ids.nodes.empty()) {
                emit idsRequested(ids);
            }
        }
    });
}

void CheckPanel::check()
{
    const setup::Document& document = m_controller->document();
    const auto active = document.activeCase();
    m_issues = setup::checkModel(document, m_controller->drawing(), m_controller->mesh(),
                                 {.item = active.value_or(setup::ItemId{})});
    m_list->clear();
    int errors = 0;
    int warnings = 0;
    const QIcon errorIcon = style()->standardIcon(QStyle::SP_MessageBoxCritical);
    const QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (const setup::BuildIssue& issue : m_issues) {
        const bool error = issue.severity == setup::Severity::Error;
        (error ? errors : warnings) += 1;
        QString item;
        if (issue.item) {
            if (const setup::Item* found = document.find(*issue.item)) {
                item = m_tree->label(*found);
            }
        }
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the list takes it.
        auto* row = new QTreeWidgetItem(
            m_list, {error ? tr("Error") : tr("Warning"), issueText(issue), item});
        row->setIcon(0, error ? errorIcon : warningIcon);
        row->setToolTip(1, issueText(issue));
    }
    const setup::Item* load = active ? document.find(*active) : nullptr;
    const QString loadName =
        load != nullptr ? QString::fromStdString(setup::nameOf(*load)) : tr("no load case");
    if (m_issues.empty()) {
        m_summary->setText(tr("%1: no problems found.").arg(loadName));
    } else {
        m_summary->setText(
            tr("%1: %2 error(s), %3 warning(s).").arg(loadName).arg(errors).arg(warnings));
    }
}

} // namespace fem::app
