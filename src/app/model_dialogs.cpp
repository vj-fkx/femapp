#include "model_dialogs.hpp"

#include "field_editor.hpp"
#include "fields.hpp"
#include "model_controller.hpp"
#include "property_panel.hpp"

#include <fem/setup/editing.hpp>
#include <fem/setup/library.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <set>

namespace fem::app {

namespace {

// The context of the texts of these dialogs for Qt Linguist.
struct Text
{
    Q_DECLARE_TR_FUNCTIONS(ModelDialogs)
};

// NOLINTBEGIN(cppcoreguidelines-owning-memory): the widgets are owned by
// their Qt parents.

QDialogButtonBox* okCancel(QDialog& dialog)
{
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}

// The targets at the top of an assignment dialog: what is selected, or the
// groups of the mesh to check (the list returned), or why there are none.
QListWidget* addTargets(QDialog& dialog, QVBoxLayout* layout, const setup::Targets& targets,
                        std::span<const setup::GroupTarget> groups)
{
    if (!targets.empty()) {
        QString what;
        if (!targets.entities.empty()) {
            what = Text::tr("%n drawing object(s) selected", nullptr,
                            static_cast<int>(targets.entities.size()));
        } else {
            what = Text::tr("%n mesh element(s) and %1 node(s) selected", nullptr,
                            static_cast<int>(targets.elements.size()))
                       .arg(targets.nodes.size());
        }
        layout->addWidget(new QLabel(what, &dialog));
        return nullptr;
    }
    if (groups.empty()) {
        layout->addWidget(new QLabel(
            Text::tr("Nothing selected and no mesh groups: select objects of the drawing or "
                     "of the mesh first."),
            &dialog));
        return nullptr;
    }
    layout->addWidget(
        new QLabel(Text::tr("Nothing selected: assign to the groups checked."), &dialog));
    auto* list = new QListWidget(&dialog);
    for (const setup::GroupTarget& g : groups) {
        auto* item = new QListWidgetItem(QString::fromStdString(g.name), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list);
    return list;
}

std::vector<setup::GroupTarget> checkedGroups(const QListWidget* list,
                                              std::span<const setup::GroupTarget> groups)
{
    std::vector<setup::GroupTarget> checked;
    for (int r = 0; list != nullptr && r < list->count(); ++r) {
        if (list->item(r)->checkState() == Qt::Checked) {
            checked.push_back(groups[static_cast<std::size_t>(r)]);
        }
    }
    return checked;
}

} // namespace

std::optional<setup::MaterialItem> chooseMaterial(QWidget* parent, setup::UnitSystem units)
{
    const std::vector<setup::LibraryMaterial>& library = setup::materialLibrary();
    QDialog dialog(parent);
    dialog.setWindowTitle(Text::tr("New material"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* standard = new QComboBox(&dialog);
    // The standards in the order of the table, then a material of the user's.
    QStringList standards;
    for (const setup::LibraryMaterial& m : library) {
        const QString s = QString::fromStdString(m.standard);
        if (!standards.contains(s)) {
            standards.append(s);
        }
    }
    standard->addItems(standards);
    const QString custom = Text::tr("(user material)");
    standard->addItem(custom);
    form->addRow(Text::tr("Standard"), standard);
    layout->addLayout(form);
    auto* grades = new QListWidget(&dialog);
    layout->addWidget(grades);
    auto* details = new QLabel(&dialog);
    details->setWordWrap(true);
    layout->addWidget(details);
    layout->addWidget(okCancel(dialog));

    const auto fill = [&] {
        grades->clear();
        for (std::size_t i = 0; i < library.size(); ++i) {
            if (QString::fromStdString(library[i].standard) == standard->currentText()) {
                auto* item = new QListWidgetItem(QString::fromStdString(library[i].grade), grades);
                item->setData(Qt::UserRole, static_cast<int>(i));
            }
        }
        grades->setEnabled(grades->count() > 0);
        if (grades->count() > 0) {
            grades->setCurrentRow(0);
        } else {
            details->setText(Text::tr("An empty material, to be filled in the properties panel."));
        }
    };
    QObject::connect(grades, &QListWidget::currentRowChanged, &dialog, [&] {
        const QListWidgetItem* item = grades->currentItem();
        if (item == nullptr) {
            return;
        }
        const auto& m = library.at(static_cast<std::size_t>(item->data(Qt::UserRole).toInt()));
        QStringList lines;
        const setup::MaterialItem converted = setup::fromLibrary(m, units);
        const auto value = [](const std::optional<double>& v) {
            return v ? QString::number(*v, 'g', 6) : QStringLiteral("-");
        };
        lines.append(
            QStringLiteral("E = %1 %2")
                .arg(value(converted.data.youngsModulus),
                     QString::fromStdString(setup::unitLabel(units, setup::quantity::kStress))));
        for (const setup::MaterialProperty& p : converted.properties) {
            lines.append(QStringLiteral("%1 = %2 %3")
                             .arg(QString::fromStdString(p.key), value(p.value),
                                  QString::fromStdString(setup::unitLabel(units, p.quantity))));
        }
        details->setText(lines.join(QStringLiteral(", ")));
    });
    QObject::connect(grades, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(standard, &QComboBox::currentIndexChanged, &dialog, fill);
    fill();
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const QListWidgetItem* chosen = grades->currentItem();
    if (standard->currentText() == custom || chosen == nullptr) {
        return setup::MaterialItem{.id = {}, .data = {.name = "Material"}};
    }
    return setup::fromLibrary(
        library.at(static_cast<std::size_t>(chosen->data(Qt::UserRole).toInt())), units);
}

std::optional<int> chooseSectionShape(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(Text::tr("New section"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* shapes = new QListWidget(&dialog);
    // In the order of core::SectionShape; generic first, as -1.
    const QStringList names{Text::tr("Generic (by its properties)"),
                            Text::tr("Rectangle"),
                            Text::tr("Circle"),
                            Text::tr("Circular tube"),
                            Text::tr("Rectangular tube"),
                            Text::tr("I section"),
                            Text::tr("Angle"),
                            Text::tr("Channel"),
                            Text::tr("Tee"),
                            Text::tr("Thin-walled (table of walls)"),
                            Text::tr("Solid polygon (from closed polylines)")};
    shapes->addItems(names);
    shapes->setCurrentRow(1);
    layout->addWidget(shapes);
    layout->addWidget(
        new QLabel(Text::tr("The dimensions are edited in the properties panel."), &dialog));
    layout->addWidget(okCancel(dialog));
    QObject::connect(shapes, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted || shapes->currentRow() < 0) {
        return std::nullopt;
    }
    return shapes->currentRow() - 1;
}

std::optional<BarChoice> chooseBars(QWidget* parent, const setup::Document& document,
                                    const setup::Targets& targets,
                                    std::span<const setup::GroupTarget> groups)
{
    QStringList materials;
    QStringList sections;
    for (const setup::Item& item : document.items()) {
        if (const auto* m = std::get_if<setup::MaterialItem>(&item)) {
            materials.append(QString::fromStdString(m->data.name));
        } else if (const auto* s = std::get_if<setup::SectionItem>(&item)) {
            sections.append(QString::fromStdString(s->data.name));
        }
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(Text::tr("Assign bars"));
    auto* layout = new QVBoxLayout(&dialog);

    // The targets: the selection, or the groups of the mesh.
    QListWidget* groupList = addTargets(dialog, layout, targets, groups);

    auto* form = new QFormLayout;
    auto* kind = new QComboBox(&dialog);
    kind->addItems(
        {Text::tr("Truss 2D"), Text::tr("Truss 3D"), Text::tr("Beam 2D"), Text::tr("Beam 3D")});
    kind->setCurrentIndex(3);
    auto* material = new QComboBox(&dialog);
    material->addItems(materials);
    auto* section = new QComboBox(&dialog);
    section->addItems(sections);
    auto* theory = new QComboBox(&dialog);
    theory->addItems({Text::tr("Euler-Bernoulli"), Text::tr("Timoshenko")});
    // The reference vector of the local y, or the default.
    auto* reference = new QWidget(&dialog);
    auto* referenceBox = new QHBoxLayout(reference);
    referenceBox->setContentsMargins(0, 0, 0, 0);
    auto* useReference = new QCheckBox(Text::tr("Given"), reference);
    std::array<QLineEdit*, 3> vector{};
    referenceBox->addWidget(useReference);
    for (std::size_t k = 0; k < 3; ++k) {
        vector.at(k) = new QLineEdit(k == 2 ? QStringLiteral("1") : QStringLiteral("0"), reference);
        vector.at(k)->setEnabled(false);
        referenceBox->addWidget(vector.at(k));
        QObject::connect(useReference, &QCheckBox::toggled, vector.at(k), &QWidget::setEnabled);
    }
    auto* roll = new QLineEdit(QStringLiteral("0"), &dialog);
    auto* warping = new QCheckBox(&dialog);
    auto* curved = new QComboBox(&dialog);
    curved->addItems({Text::tr("Arc"), Text::tr("Isoparametric")});
    auto* warning = new QLabel(&dialog);
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #b07000;"));
    form->addRow(Text::tr("Kind"), kind);
    form->addRow(Text::tr("Material"), material);
    form->addRow(Text::tr("Section"), section);
    form->addRow(Text::tr("Theory"), theory);
    form->addRow(Text::tr("Reference vector (local y)"), reference);
    form->addRow(Text::tr("Roll angle (°)"), roll);
    form->addRow(Text::tr("Warping"), warping);
    form->addRow(Text::tr("Curved beams"), curved);
    layout->addLayout(form);
    layout->addWidget(warning);
    QDialogButtonBox* buttons = okCancel(dialog);
    layout->addWidget(buttons);

    // Beam options only for beams; the truss warning (REQ-010-10).
    const auto kindChanged = [&](int index) {
        const bool beam = index >= 2;
        theory->setEnabled(beam);
        reference->setEnabled(index == 3);
        roll->setEnabled(index == 3);
        warping->setEnabled(index == 3);
        curved->setEnabled(beam);
        warning->setText(
            beam ? QString()
                 : Text::tr("A truss bar does not buckle between its nodes in the second "
                            "order or the buckling analysis: model it as a beam for "
                            "that."));
    };
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, kindChanged);
    kindChanged(kind->currentIndex());
    buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(!materials.isEmpty() && !sections.isEmpty() &&
                     (!targets.empty() || groupList != nullptr));
    if (materials.isEmpty() || sections.isEmpty()) {
        layout->insertWidget(layout->count() - 1,
                             new QLabel(Text::tr("Make a material and a section first."), &dialog));
    }

    // Typed numbers are read when OK is pressed; an unreadable one keeps
    // the dialog open.
    BarChoice choice;
    for (;;) {
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        const auto number = [](const QLineEdit* line) -> std::optional<double> {
            const auto parsed = parseNumber(line->text());
            const auto* d = parsed ? std::get_if<double>(&*parsed) : nullptr;
            return d != nullptr ? std::optional<double>(*d) : std::nullopt;
        };
        const auto angle = number(roll);
        Eigen::Vector3d v = Eigen::Vector3d::Zero();
        bool ok = angle.has_value();
        if (useReference->isChecked()) {
            for (std::size_t k = 0; k < 3; ++k) {
                const auto c = number(vector.at(k));
                ok = ok && c.has_value();
                v(static_cast<Eigen::Index>(k)) = c.value_or(0.0);
            }
            ok = ok && v.norm() > 0.0;
        }
        if (!ok) {
            warning->setText(
                Text::tr("The reference vector or the roll angle is not a valid number."));
            continue;
        }
        choice.data.kind = static_cast<core::ElementKind>(kind->currentIndex());
        choice.data.material = material->currentText().toStdString();
        choice.data.section = section->currentText().toStdString();
        choice.data.theory = static_cast<core::BeamTheory>(theory->currentIndex());
        if (useReference->isChecked()) {
            choice.data.reference = v;
        }
        choice.data.rollAngle = angle.value_or(0.0) * std::numbers::pi / 180.0;
        choice.data.warping = warping->isChecked() && choice.data.kind == core::ElementKind::Beam3D;
        choice.data.curved = static_cast<core::CurvedFormulation>(curved->currentIndex());
        if (groupList != nullptr) {
            choice.groups = checkedGroups(groupList, groups);
            if (choice.groups.empty()) {
                warning->setText(Text::tr("Check at least one group."));
                continue;
            }
        }
        return choice;
    }
}

std::optional<DetailChoice> chooseDetail(QWidget* parent, const QString& title, setup::Item item,
                                         const setup::Document& document,
                                         const setup::Targets& targets,
                                         std::span<const setup::GroupTarget> groups,
                                         bool needsTargets)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    QListWidget* groupList = needsTargets ? addTargets(dialog, layout, targets, groups) : nullptr;

    // The fields write into a copy of the item; the refused ones are
    // remembered until they are corrected.
    auto edited = std::make_shared<setup::Item>(std::move(item));
    auto refused = std::make_shared<std::set<QWidget*>>();
    const setup::UnitSystem units = document.units();
    auto* holder = new QVBoxLayout;
    layout->addLayout(holder);
    QPointer<QWidget> body;
    const auto labelsOf = [&document](const setup::Item& i) {
        QStringList labels;
        for (const Field& field : fieldsOf(i, document)) {
            labels.append(field.label);
        }
        return labels;
    };
    std::function<void()> build;
    build = [&] {
        if (body) {
            body->hide();
            body->deleteLater();
        }
        refused->clear();
        body = new QWidget(&dialog);
        auto* form = new QFormLayout(body);
        form->setContentsMargins(0, 0, 0, 0);
        const QStringList before = labelsOf(*edited);
        for (const Field& field : fieldsOf(*edited, document)) {
            addFieldEditor(form, body, field, field.get(*edited),
                           QString::fromStdString(setup::unitLabel(units, field.quantity)),
                           [&dialog, &build, &labelsOf, edited, refused,
                            before](const Field& f, const FieldValue& value, QWidget* editor) {
                               const bool ok = f.set && f.set(*edited, value);
                               markInvalid(editor, !ok);
                               if (ok) {
                                   refused->erase(editor);
                               } else {
                                   refused->insert(editor);
                               }
                               // Other fields now (a length instead of a fraction):
                               // the form again, once the editor has returned.
                               if (ok && labelsOf(*edited) != before) {
                                   QTimer::singleShot(0, &dialog, [&build] { build(); });
                               }
                           });
        }
        holder->addWidget(body);
    };
    build();
    auto* problem = new QLabel(&dialog);
    problem->setWordWrap(true);
    problem->setStyleSheet(QStringLiteral("color: #d04040;"));
    layout->addWidget(problem);
    QDialogButtonBox* buttons = okCancel(dialog);
    buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(!needsTargets || !targets.empty() || groupList != nullptr);
    layout->addWidget(buttons);

    for (;;) {
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        if (!refused->empty()) {
            problem->setText(Text::tr("Correct the values marked in red."));
            continue;
        }
        DetailChoice choice{.item = *edited, .groups = checkedGroups(groupList, groups)};
        if (groupList != nullptr && choice.groups.empty()) {
            problem->setText(Text::tr("Check at least one group."));
            continue;
        }
        return choice;
    }
}

void editLoadCases(QWidget* parent, ModelController* model)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(Text::tr("Load cases"));
    auto* layout = new QHBoxLayout(&dialog);
    auto* left = new QVBoxLayout;
    auto* list = new QListWidget(&dialog);
    left->addWidget(list);
    auto* newCase = new QPushButton(Text::tr("New"), &dialog);
    auto* activate = new QPushButton(Text::tr("Activate"), &dialog);
    auto* remove = new QPushButton(Text::tr("Delete"), &dialog);
    auto* close = new QPushButton(Text::tr("Close"), &dialog);
    for (QPushButton* button : {newCase, activate, remove}) {
        left->addWidget(button);
    }
    left->addStretch();
    left->addWidget(close);
    layout->addLayout(left);
    // REQ-010-26: the name, the standard, the category and its psi, as in
    // the properties panel, each change one step of undo.
    auto* panel = new PropertyPanel(model, &dialog);
    panel->setMinimumWidth(360);
    layout->addWidget(panel, 1);

    const auto selected = [list]() -> std::optional<setup::ItemId> {
        const QListWidgetItem* item = list->currentItem();
        if (item == nullptr) {
            return std::nullopt;
        }
        return static_cast<setup::ItemId>(item->data(Qt::UserRole).toULongLong());
    };
    // The cases, the active one marked; the one selected kept.
    const auto fill = [list, model, selected](std::optional<setup::ItemId> keep) {
        const QSignalBlocker block(list);
        if (!keep) {
            keep = selected();
        }
        list->clear();
        const setup::Document& document = model->document();
        for (const setup::Item& item : document.items()) {
            const auto* c = std::get_if<setup::LoadCase>(&item);
            if (c == nullptr) {
                continue;
            }
            QString text = QString::fromStdString(c->name);
            if (!c->category.empty()) {
                text += QStringLiteral(" [%1]").arg(QString::fromStdString(c->category));
            }
            auto* row = new QListWidgetItem(text, list);
            row->setData(Qt::UserRole, QVariant::fromValue(static_cast<qulonglong>(c->id)));
            if (document.activeCase() == c->id) {
                QFont bold = row->font();
                bold.setBold(true);
                row->setFont(bold);
                row->setText(text + Text::tr(" (active)"));
            }
            if (keep == c->id) {
                list->setCurrentItem(row);
            }
        }
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                     [panel, selected] { panel->showItem(selected()); });
    QObject::connect(model, &ModelController::documentChanged, &dialog,
                     [fill] { fill(std::nullopt); });
    QObject::connect(newCase, &QPushButton::clicked, &dialog, [model, fill, panel] {
        setup::LoadCase c{.id = {}, .name = {}, .standard = {}, .category = {}};
        c.name = setup::freeName(model->document(), c, "Load case");
        if (const auto id = model->add(std::move(c), Text::tr("new load case"))) {
            fill(*id);
            panel->showItem(*id);
        }
    });
    QObject::connect(activate, &QPushButton::clicked, &dialog, [model, selected] {
        if (const auto id = selected()) {
            model->push(setup::setActiveCase(model->document(), *id),
                        Text::tr("activate load case"));
        }
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [model, selected] {
        if (const auto id = selected()) {
            model->push(setup::removeItem(model->document(), *id), Text::tr("delete"));
        }
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [activate] { activate->click(); });
    fill(model->document().activeCase());
    if (list->currentItem() == nullptr && list->count() > 0) {
        list->setCurrentRow(0);
    }
    panel->showItem(selected());
    dialog.exec();
}

std::optional<setup::Combination>
chooseCombination(QWidget* parent, const setup::Document& document, setup::Combination combination)
{
    std::vector<const setup::LoadCase*> cases;
    for (const setup::Item& item : document.items()) {
        if (const auto* c = std::get_if<setup::LoadCase>(&item)) {
            cases.push_back(c);
        }
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(Text::tr("Combination"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(QString::fromStdString(combination.name), &dialog);
    form->addRow(Text::tr("Name"), name);
    layout->addLayout(form);
    layout->addWidget(
        new QLabel(Text::tr("The factor of each load case; empty or 0 leaves it out."), &dialog));
    auto* table = new QTableWidget(static_cast<int>(cases.size()), 2, &dialog);
    table->setHorizontalHeaderLabels({Text::tr("Load case"), Text::tr("Factor")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    for (std::size_t r = 0; r < cases.size(); ++r) {
        const auto row = static_cast<int>(r);
        auto* caseName = new QTableWidgetItem(QString::fromStdString(cases[r]->name));
        caseName->setFlags(caseName->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, caseName);
        QString factor;
        for (const auto& [id, f] : combination.terms) {
            if (id == cases[r]->id) {
                factor = formatNumber(f);
            }
        }
        table->setItem(row, 1, new QTableWidgetItem(factor));
    }
    layout->addWidget(table);
    auto* problem = new QLabel(&dialog);
    problem->setStyleSheet(QStringLiteral("color: #d04040;"));
    layout->addWidget(problem);
    QDialogButtonBox* buttons = okCancel(dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!cases.empty());
    layout->addWidget(buttons);
    if (cases.empty()) {
        problem->setText(Text::tr("Make the load cases first."));
    }

    for (;;) {
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        std::vector<std::pair<setup::ItemId, QString>> typed;
        for (std::size_t r = 0; r < cases.size(); ++r) {
            const QTableWidgetItem* cell = table->item(static_cast<int>(r), 1);
            typed.emplace_back(cases[r]->id, cell != nullptr ? cell->text() : QString());
        }
        const auto terms = termsOf(typed);
        if (!terms) {
            problem->setText(Text::tr("A factor is not a number."));
            continue;
        }
        if (terms->empty() || name->text().trimmed().isEmpty()) {
            problem->setText(Text::tr("Give a name and at least one factor."));
            continue;
        }
        combination.name = name->text().trimmed().toStdString();
        combination.terms = *terms;
        return combination;
    }
}

// NOLINTEND(cppcoreguidelines-owning-memory)

} // namespace fem::app
