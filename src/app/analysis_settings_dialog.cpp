#include "analysis_settings_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>

namespace fem::app {

namespace {

constexpr double kMinTolerance = 1e-15;
constexpr double kMaxTolerance = 1e-1;
constexpr int kToleranceDecimals = 20;
constexpr int kMaxIterations = 10000;
constexpr int kMaxModes = 100;

} // namespace

AnalysisSettingsDialog::AnalysisSettingsDialog(const setup::AnalysisSettings& settings,
                                               QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parent.
    , m_type(new QComboBox(this))
    , m_tolerance(new QLineEdit(this))
    , m_iterations(new QSpinBox(this))
    , m_modes(new QSpinBox(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
// NOLINTEND(cppcoreguidelines-owning-memory)
{
    setWindowTitle(tr("Analysis Settings"));

    m_type->addItem(tr("Linear static"), static_cast<int>(setup::AnalysisType::Linear));
    m_type->addItem(tr("Second-order static"), static_cast<int>(setup::AnalysisType::SecondOrder));
    m_type->addItem(tr("Linear buckling"), static_cast<int>(setup::AnalysisType::Buckling));
    m_type->setCurrentIndex(m_type->findData(static_cast<int>(settings.type)));

    // The point as the decimal separator, as everywhere in the application.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the field.
    auto* validator =
        new QDoubleValidator(kMinTolerance, kMaxTolerance, kToleranceDecimals, m_tolerance);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());
    m_tolerance->setValidator(validator);
    m_tolerance->setText(QString::number(settings.tolerance, 'g', 6));
    m_iterations->setRange(1, kMaxIterations);
    m_iterations->setValue(settings.maxIterations);
    m_modes->setRange(1, kMaxModes);
    m_modes->setValue(settings.modes);

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by this dialog.
    auto* form = new QFormLayout(this);
    form->addRow(tr("&Analysis:"), m_type);
    form->addRow(tr("&Tolerance (second order):"), m_tolerance);
    form->addRow(tr("Maximum &iterations (second order):"), m_iterations);
    form->addRow(tr("&Modes (buckling):"), m_modes);
    form->addRow(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_type, &QComboBox::currentIndexChanged, this, &AnalysisSettingsDialog::typeChanged);
    // OK only with a tolerance in range.
    connect(m_tolerance, &QLineEdit::textChanged, this, [this] {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_tolerance->hasAcceptableInput());
    });
    typeChanged();
}

void AnalysisSettingsDialog::typeChanged()
{
    const auto type = static_cast<setup::AnalysisType>(m_type->currentData().toInt());
    const bool second = type == setup::AnalysisType::SecondOrder;
    m_tolerance->setEnabled(second);
    m_iterations->setEnabled(second);
    m_modes->setEnabled(type == setup::AnalysisType::Buckling);
}

setup::AnalysisSettings AnalysisSettingsDialog::settings() const
{
    setup::AnalysisSettings s = m_settings;
    s.type = static_cast<setup::AnalysisType>(m_type->currentData().toInt());
    bool ok = false;
    const double tolerance = QLocale::c().toDouble(m_tolerance->text(), &ok);
    if (ok && m_tolerance->hasAcceptableInput()) {
        s.tolerance = tolerance;
    }
    s.maxIterations = m_iterations->value();
    s.modes = m_modes->value();
    return s;
}

} // namespace fem::app
