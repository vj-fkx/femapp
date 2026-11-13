#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace fem::app {

// REQ-005-04 to 09: the CAD style command line: a history of messages above,
// the prompt of the running command and the input below. Enter and Space
// send the input; Tab completes command names; the arrows browse the inputs
// of the session; Esc cancels.
class CommandLineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CommandLineWidget(QWidget* parent = nullptr);

    void setPrompt(const QString& prompt);
    void appendHistory(const QString& line);
    // Text typed while another widget had the focus (e.g. the viewport).
    void typeText(const QString& text);
    void focusInput();

signals:
    void textEntered(const QString& text);
    void escapePressed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void send();
    void browse(int step);

    QPlainTextEdit* m_history = nullptr;
    QLabel* m_prompt = nullptr;
    QLineEdit* m_input = nullptr;
    QStringList m_entries; // REQ-005-09
    qsizetype m_entryIndex = 0;
};

} // namespace fem::app
