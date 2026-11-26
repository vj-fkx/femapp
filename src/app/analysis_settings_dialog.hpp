#pragma once

#include <fem/setup/analysis_settings.hpp>

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QSpinBox;

namespace fem::app {

// REQ-011-01: Analysis > Settings: the type of the analysis and the options
// of each type (the tolerance and iterations of the second order, the modes
// of the buckling).
class AnalysisSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AnalysisSettingsDialog(const setup::AnalysisSettings& settings,
                                    QWidget* parent = nullptr);

    [[nodiscard]] setup::AnalysisSettings settings() const;

private:
    void typeChanged();

    setup::AnalysisSettings m_settings;
    QComboBox* m_type = nullptr;
    QLineEdit* m_tolerance = nullptr;
    QSpinBox* m_iterations = nullptr;
    QSpinBox* m_modes = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace fem::app
