#pragma once

#include <QDialog>

class QDoubleSpinBox;

namespace fem::app {

// REQ-005-19, 22: Tools > Drafting Settings: the grid snap step and the
// object snap aperture.
class DraftingSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    DraftingSettingsDialog(double gridStep, double aperturePx, QWidget* parent = nullptr);

    [[nodiscard]] double gridStep() const;
    [[nodiscard]] double aperturePx() const;

private:
    QDoubleSpinBox* m_gridStep = nullptr;
    QDoubleSpinBox* m_aperture = nullptr;
};

} // namespace fem::app
