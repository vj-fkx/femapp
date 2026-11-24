#include "model_tree.hpp"

#include "model_controller.hpp"

#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <QFont>
#include <QStringList>
#include <QUndoStack>

#include <type_traits>
#include <variant>

namespace fem::app {

namespace {

using Group = ModelTree::Group;

constexpr std::array<Group, 9> kGroups{Group::Units,     Group::Materials,    Group::Sections,
                                       Group::Bars,      Group::Details,      Group::Supports,
                                       Group::LoadCases, Group::Combinations, Group::Mesh};

// The group of the tree an item goes in.
Group groupOf(const setup::Item& item)
{
    return std::visit(
        [](const auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, setup::MaterialItem>) {
                return Group::Materials;
            } else if constexpr (std::is_same_v<T, setup::SectionItem>) {
                return Group::Sections;
            } else if constexpr (std::is_same_v<T, setup::BarAssignment>) {
                return Group::Bars;
            } else if constexpr (std::is_same_v<T, setup::SupportItem>) {
                return Group::Supports;
            } else if constexpr (std::is_same_v<T, setup::LoadCase> ||
                                 std::is_same_v<T, setup::LoadItem>) {
                return Group::LoadCases;
            } else if constexpr (std::is_same_v<T, setup::Combination>) {
                return Group::Combinations;
            } else {
                return Group::Details;
            }
        },
        item);
}

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

int targetCount(const setup::Targets& t)
{
    return static_cast<int>(t.entities.size() + t.groups.size() + t.elements.size() +
                            t.nodes.size());
}

} // namespace

ModelTree::ModelTree(ModelController* controller, QObject* parent)
    : QAbstractItemModel(parent)
    , m_controller(controller)
{
    connect(controller, &ModelController::documentChanged, this, &ModelTree::refresh);
    connect(controller, &ModelController::meshChanged, this, &ModelTree::refresh);
    // Drafting edits may erase or bring back entities (REQ-010-14).
    connect(controller->undoStack(), &QUndoStack::indexChanged, this, &ModelTree::refresh);
    refresh();
}

void ModelTree::setWarningIcon(QIcon icon)
{
    m_warning = std::move(icon);
}

int ModelTree::addNode(int parent, Group group, std::optional<setup::ItemId> item)
{
    const auto index = static_cast<int>(m_nodes.size());
    m_nodes.push_back({.parent = parent, .group = group, .item = item, .children = {}});
    if (parent >= 0) {
        m_nodes[static_cast<std::size_t>(parent)].children.push_back(index);
    }
    return index;
}

void ModelTree::refresh()
{
    beginResetModel();
    m_nodes.clear();
    m_top.clear();
    for (const Group group : kGroups) {
        m_top.push_back(addNode(-1, group, std::nullopt));
    }
    const auto top = [this](Group g) { return m_top[static_cast<std::size_t>(g)]; };
    // The units and the mesh have one row of text each.
    addNode(top(Group::Units), Group::Units, std::nullopt);
    addNode(top(Group::Mesh), Group::Mesh, std::nullopt);
    const setup::Document& document = m_controller->document();
    // Load cases first, so that their loads go under them.
    std::vector<std::pair<setup::ItemId, int>> cases;
    for (const setup::Item& item : document.items()) {
        if (const auto* c = std::get_if<setup::LoadCase>(&item)) {
            cases.emplace_back(c->id, addNode(top(Group::LoadCases), Group::LoadCases, c->id));
        }
    }
    for (const setup::Item& item : document.items()) {
        if (std::holds_alternative<setup::LoadCase>(item)) {
            continue;
        }
        if (const auto* load = std::get_if<setup::LoadItem>(&item)) {
            for (const auto& [id, node] : cases) {
                if (id == load->loadCase) {
                    addNode(node, Group::LoadCases, load->id);
                }
            }
            continue;
        }
        addNode(top(groupOf(item)), groupOf(item), setup::idOf(item));
    }
    endResetModel();
}

QModelIndex ModelTree::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0) {
        return {};
    }
    const std::vector<int>& rows = parent.isValid() ? m_nodes[parent.internalId()].children : m_top;
    if (row >= static_cast<int>(rows.size())) {
        return {};
    }
    return createIndex(row, 0, static_cast<quintptr>(rows[static_cast<std::size_t>(row)]));
}

QModelIndex ModelTree::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return {};
    }
    const int parent = m_nodes[child.internalId()].parent;
    if (parent < 0) {
        return {};
    }
    const int grand = m_nodes[static_cast<std::size_t>(parent)].parent;
    const std::vector<int>& siblings =
        grand < 0 ? m_top : m_nodes[static_cast<std::size_t>(grand)].children;
    const auto row = std::ranges::find(siblings, parent) - siblings.begin();
    return createIndex(static_cast<int>(row), 0, static_cast<quintptr>(parent));
}

int ModelTree::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return static_cast<int>(m_top.size());
    }
    return static_cast<int>(m_nodes[parent.internalId()].children.size());
}

int ModelTree::columnCount(const QModelIndex& /*parent*/) const
{
    return 1;
}

std::optional<setup::ItemId> ModelTree::itemAt(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return std::nullopt;
    }
    return m_nodes[index.internalId()].item;
}

std::optional<ModelTree::Group> ModelTree::groupAt(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return std::nullopt;
    }
    return m_nodes[index.internalId()].group;
}

QModelIndex ModelTree::indexOf(setup::ItemId id) const
{
    for (std::size_t n = 0; n < m_nodes.size(); ++n) {
        if (m_nodes[n].item == id) {
            const int parent = m_nodes[n].parent;
            const std::vector<int>& siblings =
                parent < 0 ? m_top : m_nodes[static_cast<std::size_t>(parent)].children;
            const auto row = std::ranges::find(siblings, static_cast<int>(n)) - siblings.begin();
            return createIndex(static_cast<int>(row), 0, static_cast<quintptr>(n));
        }
    }
    return {};
}

QString ModelTree::groupName(Group group) const
{
    switch (group) {
    case Group::Units:
        return tr("Units");
    case Group::Materials:
        return tr("Materials");
    case Group::Sections:
        return tr("Sections");
    case Group::Bars:
        return tr("Bars");
    case Group::Details:
        return tr("Beam details");
    case Group::Supports:
        return tr("Supports");
    case Group::LoadCases:
        return tr("Load cases");
    case Group::Combinations:
        return tr("Combinations");
    case Group::Mesh:
        return tr("Mesh");
    }
    return {};
}

QString ModelTree::infoText(Group group) const
{
    if (group == Group::Units) {
        const setup::UnitNames n = setup::unitNames(m_controller->document().units());
        return QStringList{text(n.force), text(n.length), text(n.mass), text(n.time),
                           text(n.temperature)}
            .join(QStringLiteral(", "));
    }
    const setup::MeshContext& mesh = m_controller->mesh();
    if (!mesh.mesh) {
        return tr("No mesh (run MESH)");
    }
    QString info =
        tr("%1 nodes, %2 elements").arg(mesh.mesh->nodeCount()).arg(mesh.mesh->elementCount());
    if (!mesh.map) {
        info += tr(", imported");
    }
    if (mesh.drawingRevision && *mesh.drawingRevision != m_controller->drawing().revision()) {
        info += tr(", out of date");
    }
    return info;
}

QString ModelTree::label(const setup::Item& item) const
{
    const auto targets = [](const setup::Targets& t) {
        return tr("%n target(s)", nullptr, targetCount(t));
    };
    const auto end = [](core::BarEnd e) {
        return e == core::BarEnd::Start ? tr("start") : tr("end");
    };
    return std::visit(
        [&](const auto& i) -> QString {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, setup::MaterialItem> ||
                          std::is_same_v<T, setup::SectionItem>) {
                return text(i.data.name);
            } else if constexpr (std::is_same_v<T, setup::BarAssignment>) {
                QString kind;
                switch (i.data.kind) {
                case core::ElementKind::Truss2D:
                    kind = tr("Truss 2D");
                    break;
                case core::ElementKind::Truss3D:
                    kind = tr("Truss 3D");
                    break;
                case core::ElementKind::Beam2D:
                    kind = tr("Beam 2D");
                    break;
                case core::ElementKind::Beam3D:
                    kind = tr("Beam 3D");
                    break;
                }
                return tr("%1: %2, %3 (%4)")
                    .arg(kind, text(i.data.material), text(i.data.section), targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::ConnectionItem>) {
                return tr("End connection, %1 (%2)").arg(end(i.data.end), targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::OffsetItem>) {
                return tr("Rigid offset (%1)").arg(targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::FoundationItem>) {
                return tr("Elastic foundation (%1)").arg(targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::WarpingEndItem>) {
                return tr("Warping end, %1 (%2)").arg(end(i.data.end), targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::SupportItem>) {
                static constexpr std::array<const char*, core::kDofTypeCount> kNames{
                    "U", "V", "W", "RX", "RY", "RZ", "T", "Warp"};
                QStringList held;
                for (std::size_t d = 0; d < i.held.size(); ++d) {
                    if (i.held.at(d)) {
                        held.append(QString::fromLatin1(kNames.at(d)));
                    }
                }
                return tr("Support %1 (%2)").arg(held.join(QLatin1Char(' ')), targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::LoadCase>) {
                if (i.standard.empty()) {
                    return text(i.name);
                }
                return tr("%1 [%2 %3]").arg(text(i.name), text(i.standard), text(i.category));
            } else if constexpr (std::is_same_v<T, setup::LoadItem>) {
                QString kind = std::visit(
                    [](const auto& a) -> QString {
                        using A = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<A, core::NodalForce>) {
                            return tr("Nodal force");
                        } else if constexpr (std::is_same_v<A, core::DistributedLoad>) {
                            return tr("Distributed load");
                        } else if constexpr (std::is_same_v<A, core::ConcentratedLoad>) {
                            return tr("Concentrated load");
                        } else if constexpr (std::is_same_v<A, setup::SelfWeight>) {
                            return tr("Self-weight");
                        } else if constexpr (std::is_same_v<A, core::TemperatureChange>) {
                            return tr("Temperature change");
                        } else if constexpr (std::is_same_v<A, core::TemperatureGradient>) {
                            return tr("Temperature gradient");
                        } else if constexpr (std::is_same_v<A, core::InitialStrain>) {
                            return tr("Initial strain");
                        } else {
                            return tr("Settlement");
                        }
                    },
                    i.action);
                if (std::holds_alternative<setup::SelfWeight>(i.action)) {
                    return kind;
                }
                return tr("%1 (%2)").arg(kind, targets(i.targets));
            } else if constexpr (std::is_same_v<T, setup::Combination>) {
                QStringList terms;
                for (const auto& [id, factor] : i.terms) {
                    const setup::Item* c = m_controller->document().find(id);
                    terms.append(QStringLiteral("%1 %2").arg(QString::number(factor),
                                                             c != nullptr ? text(setup::nameOf(*c))
                                                                          : QStringLiteral("?")));
                }
                return tr("%1 = %2").arg(text(i.name), terms.join(QStringLiteral(" + ")));
            } else {
                return {};
            }
        },
        item);
}

QVariant ModelTree::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    const Node& node = m_nodes[index.internalId()];
    const setup::Item* item = node.item ? m_controller->document().find(*node.item) : nullptr;
    const bool isGroup = node.parent < 0;
    const setup::Targets* targets = item != nullptr ? setup::targetsOf(*item) : nullptr;
    const setup::TargetState state =
        targets != nullptr
            ? setup::targetState(*targets, m_controller->drawing(), m_controller->mesh())
            : setup::TargetState::Valid;
    const bool active = item != nullptr && node.item == m_controller->document().activeCase();
    switch (role) {
    case Qt::DisplayRole:
        if (isGroup) {
            return groupName(node.group);
        }
        return item != nullptr ? label(*item) : infoText(node.group);
    case Qt::ToolTipRole:
        switch (state) {
        case setup::TargetState::Valid:
            return {};
        case setup::TargetState::MissingEntity:
            return tr("An entity of its targets is not in the drawing.");
        case setup::TargetState::StaleSelection:
            return tr("It was picked in another mesh: assign it again.");
        case setup::TargetState::MissingGroup:
            return tr("A group of its targets is not in the mesh.");
        }
        return {};
    case Qt::DecorationRole:
        if (state != setup::TargetState::Valid && !m_warning.isNull()) {
            return m_warning;
        }
        return {};
    case Qt::FontRole:
        if (active) {
            QFont bold;
            bold.setBold(true);
            return bold;
        }
        return {};
    case ItemIdRole:
        return node.item ? QVariant::fromValue(static_cast<quint64>(*node.item))
                         : QVariant::fromValue(quint64{0});
    case GroupRole:
        return static_cast<int>(node.group);
    case StateRole:
        return static_cast<int>(state);
    case ActiveRole:
        return active;
    default:
        return {};
    }
}

} // namespace fem::app
