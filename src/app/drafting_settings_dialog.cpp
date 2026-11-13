#include "drafting_settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLocale>

namespace fem::app {

namespace {

constexpr double kMinStep = 1e-6;
constexpr double kMaxStep = 1e9;
constexpr int kStepDecimals = 6;
constexpr double kMinAperture = 2.0;
constexpr double kMaxAperture = 50.0;

} // namespace

DraftingSettingsDialog::DraftingSettingsDialog(double gridStep, double aperturePx, QWidget* parent)
    : QDialog(parent)
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parent.
    , m_gridStep(new QDoubleSpinBox(this))
    , m_aperture(new QDoubleSpinBox(this))
// NOLINTEND(cppcoreguidelines-owning-memory)
{
    setWindowTitle(tr("Drafting Settings"));

    m_gridStep->setRange(kMinStep, kMaxStep);
    m_gridStep->setDecimals(kStepDecimals);
    m_gridStep->setValue(gridStep);
    m_aperture->setRange(kMinAperture, kMaxAperture);
    m_aperture->setDecimals(0);
    m_aperture->setSuffix(tr(" px"));
    m_aperture->setValue(aperturePx);

    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parent.
    auto* form = new QFormLayout(this);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    form->addRow(tr("Grid snap &step:"), m_gridStep);
    form->addRow(tr("Object snap &aperture:"), m_aperture);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

double DraftingSettingsDialog::gridStep() const
{
    return m_gridStep->value();
}

double DraftingSettingsDialog::aperturePx() const
{
    return m_aperture->value();
}

} // namespace fem::app
