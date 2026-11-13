#include "command_line_widget.hpp"

#include <fem/geom/command_input.hpp>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace fem::app {

namespace {

constexpr int kHistoryLines = 500;
constexpr int kVisibleHistoryLines = 4;

} // namespace

CommandLineWidget::CommandLineWidget(QWidget* parent)
    : QWidget(parent)
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parent.
    , m_history(new QPlainTextEdit(this))
    , m_prompt(new QLabel(this))
    , m_input(new QLineEdit(this))
// NOLINTEND(cppcoreguidelines-owning-memory)
{
    m_history->setReadOnly(true);
    m_history->setMaximumBlockCount(kHistoryLines);
    m_history->setFocusPolicy(Qt::NoFocus);
    const int lineHeight = m_history->fontMetrics().lineSpacing();
    m_history->setFixedHeight(lineHeight * kVisibleHistoryLines + 2 * m_history->frameWidth() +
                              static_cast<int>(m_history->document()->documentMargin() * 2));

    m_input->installEventFilter(this);

    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parent.
    auto* row = new QHBoxLayout;
    row->addWidget(m_prompt);
    row->addWidget(m_input, 1);
    auto* layout = new QVBoxLayout(this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(m_history);
    layout->addLayout(row);
    setFocusProxy(m_input);
}

void CommandLineWidget::setPrompt(const QString& prompt)
{
    m_prompt->setText(prompt);
}

void CommandLineWidget::appendHistory(const QString& line)
{
    m_history->appendPlainText(line);
    m_history->verticalScrollBar()->setValue(m_history->verticalScrollBar()->maximum());
}

void CommandLineWidget::typeText(const QString& text)
{
    m_input->insert(text);
    focusInput();
}

void CommandLineWidget::focusInput()
{
    m_input->setFocus(Qt::ShortcutFocusReason);
}

bool CommandLineWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_input || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    switch (key->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space: // REQ-005-06: Space works as Enter, as in AutoCAD
        send();
        return true;
    case Qt::Key_Escape:
        m_input->clear();
        emit escapePressed();
        return true;
    case Qt::Key_Delete:
        // The input keeps the key for editing its text; empty, it erases the
        // selection, as the window shortcut cannot fire while it has focus.
        if (m_input->text().isEmpty()) {
            emit deletePressed();
            return true;
        }
        return QWidget::eventFilter(watched, event);
    case Qt::Key_Tab: {
        // REQ-005-05: completes a command name.
        const auto name = geom::completeCommand(m_input->text().toStdString());
        if (name) {
            m_input->setText(QString::fromUtf8(name->data(), static_cast<qsizetype>(name->size())));
        }
        return true;
    }
    case Qt::Key_Up:
        browse(-1);
        return true;
    case Qt::Key_Down:
        browse(1);
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void CommandLineWidget::send()
{
    const QString text = m_input->text().trimmed();
    if (!text.isEmpty() && (m_entries.isEmpty() || m_entries.last() != text)) {
        m_entries.append(text);
    }
    m_entryIndex = m_entries.size();
    m_input->clear();
    emit textEntered(text);
}

void CommandLineWidget::browse(int step)
{
    if (m_entries.isEmpty()) {
        return;
    }
    m_entryIndex = std::clamp<qsizetype>(m_entryIndex + step, 0, m_entries.size());
    m_input->setText(m_entryIndex < m_entries.size() ? m_entries.at(m_entryIndex) : QString());
}

} // namespace fem::app
