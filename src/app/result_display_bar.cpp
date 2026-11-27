#include "result_display_bar.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace fem::app {

namespace {

constexpr int kNoQuantity = -1;
constexpr int kMaxBands = 24;
constexpr double kLargest = 1e15;
constexpr int kDecimals = 6;

} // namespace

ResultDisplayBar::ResultDisplayBar(QWidget* parent)
    : QWidget(parent)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by this widget.
    auto* rows = new QVBoxLayout(this);
    auto* first = new QHBoxLayout;
    auto* second = new QHBoxLayout;
    m_shown = new QCheckBox(tr("Show in viewport"), this);
    m_quantity = new QComboBox(this);
    m_scaleMode = new QComboBox(this);
    m_userScale = new QDoubleSpinBox(this);
    m_deformed = new QCheckBox(tr("Deformed"), this);
    m_original = new QCheckBox(tr("Original"), this);
    m_bands = new QSpinBox(this);
    m_fixed = new QCheckBox(tr("Fixed range"), this);
    m_min = new QDoubleSpinBox(this);
    m_max = new QDoubleSpinBox(this);
    m_reactions = new QCheckBox(tr("Reactions"), this);
    m_extremes = new QCheckBox(tr("Max/min"), this);
    m_play = new QToolButton(this);
    m_period = new QDoubleSpinBox(this);
    auto* scaleLabel = new QLabel(tr("Scale"), this);
    auto* bandsLabel = new QLabel(tr("Bands"), this);
    auto* periodLabel = new QLabel(tr("Period"), this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    rows->setContentsMargins(0, 0, 0, 0);

    m_shown->setObjectName(QStringLiteral("shown"));
    m_quantity->setObjectName(QStringLiteral("quantity"));
    m_scaleMode->setObjectName(QStringLiteral("scaleMode"));
    m_userScale->setObjectName(QStringLiteral("userScale"));
    m_deformed->setObjectName(QStringLiteral("deformed"));
    m_original->setObjectName(QStringLiteral("original"));
    m_bands->setObjectName(QStringLiteral("bands"));
    m_fixed->setObjectName(QStringLiteral("fixedRange"));
    m_min->setObjectName(QStringLiteral("rangeMin"));
    m_max->setObjectName(QStringLiteral("rangeMax"));
    m_reactions->setObjectName(QStringLiteral("reactions"));
    m_extremes->setObjectName(QStringLiteral("extremes"));
    m_play->setObjectName(QStringLiteral("play"));
    m_period->setObjectName(QStringLiteral("period"));

    m_scaleMode->addItem(tr("Auto"), static_cast<int>(ScaleMode::Automatic));
    m_scaleMode->addItem(tr("User"), static_cast<int>(ScaleMode::User));
    m_scaleMode->addItem(tr("Real"), static_cast<int>(ScaleMode::Real));
    m_userScale->setRange(0.0, kLargest);
    m_userScale->setDecimals(3);
    m_bands->setRange(0, kMaxBands);
    // REQ-012-12: 0 is continuous.
    m_bands->setSpecialValueText(tr("Continuous"));
    for (QDoubleSpinBox* box : {m_min, m_max}) {
        box->setRange(-kLargest, kLargest);
        box->setDecimals(kDecimals);
    }
    m_play->setText(tr("Play"));
    m_play->setCheckable(true);
    m_period->setRange(0.2, 60.0);
    m_period->setSuffix(QStringLiteral(" s"));
    m_period->setDecimals(1);

    first->addWidget(m_shown);
    first->addWidget(m_quantity, 1);
    first->addWidget(scaleLabel);
    first->addWidget(m_scaleMode);
    first->addWidget(m_userScale);
    first->addWidget(m_deformed);
    first->addWidget(m_original);
    second->addWidget(bandsLabel);
    second->addWidget(m_bands);
    second->addWidget(m_fixed);
    second->addWidget(m_min);
    second->addWidget(m_max);
    second->addWidget(m_reactions);
    second->addWidget(m_extremes);
    second->addWidget(m_play);
    second->addWidget(periodLabel);
    second->addWidget(m_period);
    second->addStretch(1);
    rows->addLayout(first);
    rows->addLayout(second);

    std::vector<core::ResultQuantity> all;
    all.reserve(core::kResultQuantityCount);
    for (int q = 0; q < core::kResultQuantityCount; ++q) {
        all.push_back(static_cast<core::ResultQuantity>(q));
    }
    setQuantities(all);
    showSettings();
    setAvailable(false);

    connect(m_shown, &QCheckBox::toggled, this, &ResultDisplayBar::shownRequested);
    connect(m_quantity, &QComboBox::currentIndexChanged, this, &ResultDisplayBar::edited);
    connect(m_scaleMode, &QComboBox::currentIndexChanged, this, &ResultDisplayBar::edited);
    connect(m_userScale, &QDoubleSpinBox::editingFinished, this, &ResultDisplayBar::edited);
    connect(m_bands, &QSpinBox::valueChanged, this, &ResultDisplayBar::edited);
    connect(m_min, &QDoubleSpinBox::editingFinished, this, &ResultDisplayBar::edited);
    connect(m_max, &QDoubleSpinBox::editingFinished, this, &ResultDisplayBar::edited);
    connect(m_period, &QDoubleSpinBox::valueChanged, this, &ResultDisplayBar::edited);
    for (QCheckBox* box : {m_deformed, m_original, m_fixed, m_reactions, m_extremes}) {
        connect(box, &QCheckBox::toggled, this, &ResultDisplayBar::edited);
    }
    connect(m_play, &QToolButton::toggled, this, &ResultDisplayBar::edited);
}

void ResultDisplayBar::setSettings(const ResultDisplaySettings& settings)
{
    m_settings = settings;
    showSettings();
}

void ResultDisplayBar::setQuantities(const std::vector<core::ResultQuantity>& quantities)
{
    const QSignalBlocker block(m_quantity);
    m_quantity->clear();
    m_quantity->addItem(tr("None"), kNoQuantity);
    for (const core::ResultQuantity q : quantities) {
        m_quantity->addItem(quantityName(q), static_cast<int>(q));
    }
    showSettings();
}

void ResultDisplayBar::setShownState(bool shown)
{
    const QSignalBlocker block(m_shown);
    m_shown->setChecked(shown);
}

void ResultDisplayBar::setAvailable(bool available)
{
    m_shown->setEnabled(available);
}

void ResultDisplayBar::showSettings()
{
    // No signals while the controls take the settings.
    const std::array<QWidget*, 13> boxes = {
        m_quantity, m_scaleMode, m_userScale, m_deformed, m_original, m_bands, m_fixed,
        m_min,      m_max,       m_reactions, m_extremes, m_play,     m_period};
    std::vector<std::pair<QWidget*, bool>> blocked;
    blocked.reserve(boxes.size());
    for (QWidget* box : boxes) {
        blocked.emplace_back(box, box->blockSignals(true));
    }
    const ResultDisplaySettings& s = m_settings;
    const int quantity = s.colored ? static_cast<int>(s.quantity) : kNoQuantity;
    const int at = m_quantity->findData(quantity);
    // A quantity the result does not offer: |u|, as the display shows.
    m_quantity->setCurrentIndex(
        at >= 0 ? at : m_quantity->findData(static_cast<int>(core::ResultQuantity::UMagnitude)));
    m_scaleMode->setCurrentIndex(m_scaleMode->findData(static_cast<int>(s.scaleMode)));
    m_userScale->setValue(s.userScale);
    m_userScale->setEnabled(s.scaleMode == ScaleMode::User);
    m_deformed->setChecked(s.deformed);
    m_original->setChecked(s.original);
    m_bands->setValue(s.bands);
    m_fixed->setChecked(s.rangeFixed);
    m_min->setValue(s.rangeMin);
    m_max->setValue(s.rangeMax);
    m_min->setEnabled(s.rangeFixed);
    m_max->setEnabled(s.rangeFixed);
    m_reactions->setChecked(s.reactions);
    m_extremes->setChecked(s.extremes);
    m_play->setChecked(s.playing);
    m_play->setText(s.playing ? tr("Pause") : tr("Play"));
    m_period->setValue(s.period);
    for (const auto& [box, was] : blocked) {
        box->blockSignals(was);
    }
}

void ResultDisplayBar::edited()
{
    ResultDisplaySettings s = m_settings;
    const int quantity = m_quantity->currentData().toInt();
    s.colored = quantity != kNoQuantity;
    if (s.colored) {
        s.quantity = static_cast<core::ResultQuantity>(quantity);
    }
    s.scaleMode = static_cast<ScaleMode>(m_scaleMode->currentData().toInt());
    s.userScale = m_userScale->value();
    s.deformed = m_deformed->isChecked();
    s.original = m_original->isChecked();
    s.bands = m_bands->value();
    s.rangeFixed = m_fixed->isChecked();
    // REQ-012-11 (design §7): a range with min >= max is refused.
    if (m_min->value() < m_max->value()) {
        s.rangeMin = m_min->value();
        s.rangeMax = m_max->value();
    }
    s.reactions = m_reactions->isChecked();
    s.extremes = m_extremes->isChecked();
    s.playing = m_play->isChecked();
    s.period = m_period->value();
    m_settings = s;
    showSettings();
    emit settingsChanged(m_settings);
}

} // namespace fem::app
