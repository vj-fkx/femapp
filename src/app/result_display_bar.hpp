#pragma once

// Feature 012 (design §3, §4; REQ-012-01, 06, 07, 09, 11, 12, 15, 16, 17):
// the options of the display of a result, at the top of the Results dock:
// shown in the viewport, the quantity (or none), the factor, the deformed
// and the original shape, the bands, a fixed range, the reactions, the
// extremes and the animation.

#include "result_display_settings.hpp"

#include <fem/core/result_sampling.hpp>

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QToolButton;

namespace fem::app {

class ResultDisplayBar : public QWidget
{
    Q_OBJECT

public:
    explicit ResultDisplayBar(QWidget* parent = nullptr);

    // The settings shown (no signal).
    void setSettings(const ResultDisplaySettings& settings);
    [[nodiscard]] const ResultDisplaySettings& settings() const noexcept { return m_settings; }
    // REQ-012-09: the quantities of the result shown; the one chosen stays
    // chosen when it is among them.
    void setQuantities(const std::vector<core::ResultQuantity>& quantities);
    // REQ-012-01: whether the display is on, and whether there is a result.
    void setShownState(bool shown);
    void setAvailable(bool available);

signals:
    void settingsChanged(const fem::app::ResultDisplaySettings& settings);
    void shownRequested(bool shown);

private:
    void edited();
    void showSettings();

    ResultDisplaySettings m_settings;
    QCheckBox* m_shown = nullptr;
    QComboBox* m_quantity = nullptr;
    QComboBox* m_scaleMode = nullptr;
    QDoubleSpinBox* m_userScale = nullptr;
    QCheckBox* m_deformed = nullptr;
    QCheckBox* m_original = nullptr;
    QSpinBox* m_bands = nullptr;
    QCheckBox* m_fixed = nullptr;
    QDoubleSpinBox* m_min = nullptr;
    QDoubleSpinBox* m_max = nullptr;
    QCheckBox* m_reactions = nullptr;
    QCheckBox* m_extremes = nullptr;
    QToolButton* m_play = nullptr;
    QDoubleSpinBox* m_period = nullptr;
};

} // namespace fem::app
