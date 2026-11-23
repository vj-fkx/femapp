#include "property_panel.hpp"

#include "field_editor.hpp"
#include "model_controller.hpp"
#include "section_view.hpp"
#include "thin_walled_table.hpp"

#include <fem/setup/editing.hpp>
#include <fem/setup/units.hpp>

#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <numbers>
#include <utility>

namespace fem::app {

PropertyPanel::PropertyPanel(ModelController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by this widget.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    // The document changed (an edit, an undo): show the item as it is now.
    connect(controller, &ModelController::documentChanged, this, &PropertyPanel::rebuild);
    rebuild();
}

void PropertyPanel::showItem(std::optional<setup::ItemId> id)
{
    m_item = id;
    rebuild();
}

void PropertyPanel::rebuild()
{
    // The editor that finished the edit may be the sender: delete later.
    if (m_body) {
        m_body->deleteLater();
    }
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_body = new QWidget(this);
    auto* form = new QFormLayout(m_body);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    form->setContentsMargins(0, 0, 0, 0);
    layout()->addWidget(m_body);
    const setup::Item* item = m_item ? m_controller->document().find(*m_item) : nullptr;
    if (item == nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the form.
        form->addRow(new QLabel(tr("Select an item of the model tree."), m_body));
        return;
    }
    for (const Field& field : fieldsOf(*item, m_controller->document())) {
        addField(form, field, *item);
    }
    if (const auto* section = std::get_if<setup::SectionItem>(item)) {
        addSection(form, *section);
    }
}

void PropertyPanel::addSection(QFormLayout* form, const setup::SectionItem& section)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): the widgets are owned by
    // the body of the panel.
    const setup::UnitSystem units = m_controller->document().units();
    const setup::ItemId id = section.id;
    const auto* shape = section.data.shape ? &*section.data.shape : nullptr;

    // The editors of the shapes that have no fields.
    if (const auto* thin = shape != nullptr ? std::get_if<core::ThinWalled>(shape) : nullptr) {
        auto* table = new ThinWalledTable(
            *thin, QString::fromStdString(setup::unitLabel(units, setup::quantity::kLength)),
            m_body);
        connect(table, &ThinWalledTable::edited, this, [this, id](const core::ThinWalled& edited) {
            const setup::Item* current = m_controller->document().find(id);
            const auto* s = current != nullptr ? std::get_if<setup::SectionItem>(current) : nullptr;
            if (s == nullptr) {
                return;
            }
            setup::SectionItem changed = *s;
            changed.data.shape = edited;
            m_controller->push(setup::changeItem(m_controller->document(), changed),
                               tr("edit walls"));
        });
        form->addRow(table);
    } else if (shape != nullptr && std::holds_alternative<core::SolidPolygon>(*shape)) {
        auto* from = new QPushButton(tr("Outline from selected polylines"), m_body);
        from->setToolTip(tr("The largest closed polyline selected in the drawing is the "
                            "outline, the others are holes. The drawing x and y are the y and z "
                            "of the section."));
        connect(from, &QPushButton::clicked, this,
                [this, id] { emit solidFromSelectionRequested(id); });
        form->addRow(from);
    }

    // REQ-010-06: the properties of the core, or its message.
    auto* heading = new QLabel(tr("Computed properties"), m_body);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    form->addRow(heading);
    const auto solved = core::solveSection(section.data);
    if (!solved) {
        auto* problem = new QLabel(QString::fromStdString(solved.error().message), m_body);
        problem->setWordWrap(true);
        problem->setStyleSheet(QStringLiteral("color: #d04040;"));
        form->addRow(problem);
        if (shape != nullptr) {
            auto* view = new SectionView(m_body);
            view->setSection(*shape, nullptr);
            form->addRow(view);
        }
        return;
    }
    const core::SectionProperties& p = (*solved)->properties();
    const auto row = [&](const QString& label, const QString& value, setup::Quantity quantity) {
        const QString unit = QString::fromStdString(setup::unitLabel(units, quantity));
        auto* text = new QLabel(unit.isEmpty() ? value : value + QLatin1Char(' ') + unit, m_body);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, text);
    };
    const auto number = [](double v) { return QString::number(v, 'g', 6); };
    const auto pair = [&](const Eigen::Vector2d& v) {
        return QStringLiteral("(%1, %2)").arg(number(v.x()), number(v.y()));
    };
    namespace q = setup::quantity;
    row(tr("A"), number(p.area), q::kArea);
    row(tr("Centroid (y, z)"), pair(p.centroid), q::kLength);
    row(tr("Principal angle"),
        QStringLiteral("%1°").arg(number(p.principalAngle * 180.0 / std::numbers::pi)), {});
    row(tr("Iy'"), number(p.inertiaY), q::kInertia);
    row(tr("Iz'"), number(p.inertiaZ), q::kInertia);
    row(tr("J"), number(p.torsionConstant), q::kInertia);
    row(tr("Iw"), number(p.warpingConstant), q::kWarping);
    row(tr("Shear centre (y', z')"), pair(p.shearCentre), q::kLength);
    const auto optionalArea = [&](const QString& label, const std::optional<double>& v) {
        if (v) {
            row(label, number(*v), q::kArea);
        } else {
            form->addRow(label, new QLabel(tr("(not computed)"), m_body));
        }
    };
    optionalArea(tr("Asy'"), p.shearAreaY);
    optionalArea(tr("Asz'"), p.shearAreaZ);
    row(tr("βy'"), number(p.monosymmetryY), q::kLength);
    row(tr("βz'"), number(p.monosymmetryZ), q::kLength);
    row(tr("r0²"), number(p.polarRadius2), q::kArea);
    if (shape != nullptr) {
        auto* view = new SectionView(m_body);
        view->setSection(*shape, solved->get());
        form->addRow(view);
    }
    // NOLINTEND(cppcoreguidelines-owning-memory)
}

void PropertyPanel::addField(QFormLayout* form, const Field& field, const setup::Item& item)
{
    const QString unit =
        QString::fromStdString(setup::unitLabel(m_controller->document().units(), field.quantity));
    addFieldEditor(form, m_body, field, field.get(item), unit,
                   [this](const Field& f, const FieldValue& value, QWidget* editor) {
                       edit(f, value, editor);
                   });
}

void PropertyPanel::edit(const Field& field, const FieldValue& value, QWidget* editor)
{
    if (!m_item || !field.set) {
        return;
    }
    const setup::Item* current = m_controller->document().find(*m_item);
    if (current == nullptr) {
        return;
    }
    // The same value again (the focus left a field): no edit to undo.
    if (field.get(*current) == value) {
        markInvalid(editor, false);
        return;
    }
    setup::Item changed = *current;
    // REQ-010-19: an invalid value stays in its field, marked, and nothing
    // goes to the document.
    if (!field.set(changed, value)) {
        markInvalid(editor, true);
        return;
    }
    m_controller->push(setup::changeItem(m_controller->document(), changed),
                       tr("edit %1").arg(field.label));
}

} // namespace fem::app
