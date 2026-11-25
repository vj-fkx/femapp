#include "detail/check.hpp"

#include <fem/core/static_analysis.hpp>
#include <fem/setup/build.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace fem::setup {

namespace {

std::string idText(geom::EntityId id)
{
    return std::to_string(static_cast<std::uint64_t>(id));
}

// The mesh read through the map of 009 (design §5.4, step 2): the
// elements and the nodes of each entity, by external id, made once.
class MeshIndex
{
public:
    explicit MeshIndex(const MeshContext& context)
        : m_context(context)
    {
        const core::Mesh& mesh = *context.mesh;
        if (context.map) {
            const auto& elements = context.map->elements;
            for (std::size_t e = 0; e < elements.size(); ++e) {
                m_elements[static_cast<std::uint64_t>(elements[e].entity)].push_back(
                    mesh.elementId(static_cast<core::ElementIndex>(e)));
            }
            const auto& nodes = context.map->nodes;
            for (std::size_t n = 0; n < nodes.size(); ++n) {
                for (const geom::EntityId entity : nodes[n].entities) {
                    m_nodes[static_cast<std::uint64_t>(entity)].push_back(
                        mesh.nodeId(static_cast<core::NodeIndex>(n)));
                }
            }
        }
    }

    [[nodiscard]] const core::Mesh& mesh() const { return *m_context.mesh; }
    [[nodiscard]] bool hasMap() const { return m_context.map != nullptr; }
    [[nodiscard]] std::uint64_t generation() const { return m_context.generation; }

    [[nodiscard]] const std::vector<core::ExternalId>* elementsOf(geom::EntityId id) const
    {
        const auto it = m_elements.find(static_cast<std::uint64_t>(id));
        return it == m_elements.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const std::vector<core::ExternalId>* nodesOf(geom::EntityId id) const
    {
        const auto it = m_nodes.find(static_cast<std::uint64_t>(id));
        return it == m_nodes.end() ? nullptr : &it->second;
    }

private:
    const MeshContext& m_context;
    std::unordered_map<std::uint64_t, std::vector<core::ExternalId>> m_elements;
    std::unordered_map<std::uint64_t, std::vector<core::ExternalId>> m_nodes;
};

// The targets of an item resolved on the mesh; invalid parts give issues.
class Resolver
{
public:
    Resolver(const geom::Drawing& drawing, const MeshIndex& index, std::vector<BuildIssue>& issues)
        : m_drawing(drawing)
        , m_index(index)
        , m_issues(issues)
    {}

    // The external ids of the elements of `targets`; empty when nothing of
    // them could be found (issues given).
    [[nodiscard]] std::vector<core::ExternalId> elements(const Targets& targets, ItemId item) const
    {
        std::set<core::ExternalId> out;
        for (const geom::EntityId e : targets.entities) {
            if (const auto* found = entity(e, item, false)) {
                out.insert(found->begin(), found->end());
            }
        }
        for (const GroupTarget& g : targets.groups) {
            if (const core::Group* group = this->group(g, item)) {
                for (const core::ElementIndex e : group->elements) {
                    out.insert(m_index.mesh().elementId(e));
                }
            }
        }
        if (!targets.elements.empty() && fresh(targets, item)) {
            for (const core::ExternalId id : targets.elements) {
                if (m_index.mesh().findElement(id)) {
                    out.insert(id);
                } else {
                    issue(IssueKind::MissingElement, item, std::to_string(id));
                }
            }
        }
        return {out.begin(), out.end()};
    }

    // The external ids of the nodes of `targets`: the nodes of entities
    // (a point's, or all of a line's), of groups, of picked elements, and the
    // picked nodes.
    [[nodiscard]] std::vector<core::ExternalId> nodes(const Targets& targets, ItemId item) const
    {
        std::set<core::ExternalId> out;
        const core::Mesh& mesh = m_index.mesh();
        for (const geom::EntityId e : targets.entities) {
            if (const auto* found = entity(e, item, true)) {
                out.insert(found->begin(), found->end());
            }
        }
        for (const GroupTarget& g : targets.groups) {
            if (const core::Group* group = this->group(g, item)) {
                for (const core::NodeIndex n : mesh.groupNodes(*group)) {
                    out.insert(mesh.nodeId(n));
                }
            }
        }
        if ((!targets.elements.empty() || !targets.nodes.empty()) && fresh(targets, item)) {
            for (const core::ExternalId id : targets.elements) {
                if (const auto e = mesh.findElement(id)) {
                    for (const core::NodeIndex n : mesh.elementNodes(*e)) {
                        out.insert(mesh.nodeId(n));
                    }
                } else {
                    issue(IssueKind::MissingElement, item, std::to_string(id));
                }
            }
            for (const core::ExternalId id : targets.nodes) {
                if (mesh.findNode(id)) {
                    out.insert(id);
                } else {
                    issue(IssueKind::MissingNode, item, std::to_string(id));
                }
            }
        }
        return {out.begin(), out.end()};
    }

    void issue(IssueKind kind, std::optional<ItemId> item, std::string detail,
               Severity severity = Severity::Error) const
    {
        m_issues.push_back(
            {.severity = severity, .kind = kind, .item = item, .detail = std::move(detail)});
    }

private:
    [[nodiscard]] const std::vector<core::ExternalId>* entity(geom::EntityId id, ItemId item,
                                                              bool nodes) const
    {
        if (m_drawing.find(id) == nullptr) {
            issue(IssueKind::MissingEntity, item, idText(id));
            return nullptr;
        }
        const auto* found = nodes ? m_index.nodesOf(id) : m_index.elementsOf(id);
        if (!m_index.hasMap() || found == nullptr || found->empty()) {
            issue(IssueKind::EntityNotMeshed, item, idText(id));
            return nullptr;
        }
        return found;
    }

    [[nodiscard]] const core::Group* group(const GroupTarget& g, ItemId item) const
    {
        const core::Group* found = m_index.mesh().findGroup(g.name, g.dimension);
        if (found == nullptr) {
            issue(IssueKind::MissingGroup, item, g.name);
        }
        return found;
    }

    [[nodiscard]] bool fresh(const Targets& targets, ItemId item) const
    {
        if (targets.meshGeneration != m_index.generation()) {
            issue(IssueKind::StaleSelection, item, {});
            return false;
        }
        return true;
    }

    const geom::Drawing& m_drawing;
    const MeshIndex& m_index;
    std::vector<BuildIssue>& m_issues;
};

// The load cases of the selection with their factors (design §5.4, step 5).
std::optional<std::vector<std::pair<ItemId, double>>> termsOf(const Document& document,
                                                              LoadSelection load)
{
    const Item* item = document.find(load.item);
    if (item == nullptr) {
        return std::nullopt;
    }
    if (std::holds_alternative<LoadCase>(*item)) {
        return std::vector<std::pair<ItemId, double>>{{load.item, 1.0}};
    }
    if (const auto* combination = std::get_if<Combination>(item)) {
        return combination->terms;
    }
    return std::nullopt;
}

core::ElementIds elementIds(std::vector<core::ExternalId> ids)
{
    return core::ElementIds{std::move(ids)};
}

core::NodeIds nodeIds(std::vector<core::ExternalId> ids)
{
    return core::NodeIds{std::move(ids)};
}

} // namespace

BuildResult buildModel(const Document& document, const geom::Drawing& drawing,
                       const MeshContext& mesh, LoadSelection load)
{
    BuildResult result;
    auto& issues = result.issues;
    if (!mesh.mesh) {
        issues.push_back(
            {.severity = Severity::Error, .kind = IssueKind::NoMesh, .item = {}, .detail = {}});
        return result;
    }
    if (mesh.drawingRevision && *mesh.drawingRevision != drawing.revision()) {
        issues.push_back({.severity = Severity::Error,
                          .kind = IssueKind::MeshOutOfDate,
                          .item = {},
                          .detail = {}});
    }
    core::Model& model = result.model;
    model.mesh = mesh.mesh;
    const MeshIndex index(mesh);
    const Resolver resolve(drawing, index, issues);

    // Materials and sections, by name (design §5.4, step 6).
    std::set<std::string> materials;
    std::set<std::string> sections;
    for (const Item& item : document.items()) {
        if (const auto* m = std::get_if<MaterialItem>(&item)) {
            model.materials.push_back(m->data);
            materials.insert(m->data.name);
        } else if (const auto* s = std::get_if<SectionItem>(&item)) {
            model.sections.push_back(s->data);
            sections.insert(s->data.name);
        }
    }

    // Supports held, by (node, dof); settlements add to them (step 4).
    std::map<std::pair<core::ExternalId, core::DofType>, double> restrained;
    std::map<std::pair<core::ExternalId, core::DofType>, ItemId> settledBy;

    for (const Item& item : document.items()) {
        std::visit(
            [&](const auto& i) {
                using T = std::decay_t<decltype(i)>;
                if constexpr (std::is_same_v<T, BarAssignment>) {
                    bool known = true;
                    if (!materials.contains(i.data.material)) {
                        resolve.issue(IssueKind::UnknownMaterial, i.id, i.data.material);
                        known = false;
                    }
                    if (!sections.contains(i.data.section)) {
                        resolve.issue(IssueKind::UnknownSection, i.id, i.data.section);
                        known = false;
                    }
                    auto ids = resolve.elements(i.targets, i.id);
                    if (known && !ids.empty()) {
                        core::Assignment a = i.data;
                        a.elements = elementIds(std::move(ids));
                        model.assignments.push_back(std::move(a));
                    }
                    // REQ-010-10
                    if (i.data.kind == core::ElementKind::Truss2D ||
                        i.data.kind == core::ElementKind::Truss3D) {
                        resolve.issue(IssueKind::TrussBuckling, i.id, {}, Severity::Warning);
                    }
                } else if constexpr (std::is_same_v<T, ConnectionItem> ||
                                     std::is_same_v<T, OffsetItem> ||
                                     std::is_same_v<T, FoundationItem> ||
                                     std::is_same_v<T, WarpingEndItem>) {
                    auto ids = resolve.elements(i.targets, i.id);
                    if (ids.empty()) {
                        return;
                    }
                    auto data = i.data;
                    data.elements = elementIds(std::move(ids));
                    if constexpr (std::is_same_v<T, ConnectionItem>) {
                        model.endConnections.push_back(std::move(data));
                    } else if constexpr (std::is_same_v<T, OffsetItem>) {
                        model.rigidOffsets.push_back(std::move(data));
                    } else if constexpr (std::is_same_v<T, FoundationItem>) {
                        model.foundations.push_back(std::move(data));
                    } else {
                        model.warpingEnds.push_back(std::move(data));
                    }
                } else if constexpr (std::is_same_v<T, SupportItem>) {
                    for (const core::ExternalId node : resolve.nodes(i.targets, i.id)) {
                        for (int d = 0; d < core::kDofTypeCount; ++d) {
                            if (i.held.at(static_cast<std::size_t>(d))) {
                                restrained.try_emplace({node, static_cast<core::DofType>(d)}, 0.0);
                            }
                        }
                    }
                }
            },
            item);
    }

    // The actions of the load case or combination (step 5).
    const auto terms = termsOf(document, load);
    if (!terms) {
        resolve.issue(IssueKind::UnknownLoad, load.item, {});
    } else {
        Eigen::Vector3d gravity = Eigen::Vector3d::Zero();
        bool weighted = false;
        for (const auto& [loadCase, factor] : *terms) {
            for (const Item& item : document.items()) {
                const auto* l = std::get_if<LoadItem>(&item);
                if (l == nullptr || l->loadCase != loadCase) {
                    continue;
                }
                const double f = factor;
                std::visit(
                    [&](const auto& a) {
                        using A = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<A, SelfWeight>) {
                            gravity += f * a.acceleration;
                            weighted = true;
                        } else if constexpr (std::is_same_v<A, core::NodalForce>) {
                            auto ids = resolve.nodes(l->targets, l->id);
                            if (!ids.empty()) {
                                core::NodalForce n = a;
                                n.nodes = nodeIds(std::move(ids));
                                n.force *= f;
                                n.moment *= f;
                                n.bimoment *= f;
                                model.nodalForces.push_back(std::move(n));
                            }
                        } else if constexpr (std::is_same_v<A, Settlement>) {
                            for (const core::ExternalId node : resolve.nodes(l->targets, l->id)) {
                                const auto key = std::pair{node, a.dof};
                                const auto held = restrained.find(key);
                                if (held == restrained.end()) {
                                    resolve.issue(IssueKind::SettlementNotHeld, l->id,
                                                  std::to_string(node));
                                    continue;
                                }
                                held->second += f * a.value;
                                settledBy.try_emplace(key, l->id);
                            }
                        } else {
                            auto ids = resolve.elements(l->targets, l->id);
                            if (ids.empty()) {
                                return;
                            }
                            A e = a;
                            e.elements = elementIds(std::move(ids));
                            if constexpr (std::is_same_v<A, core::DistributedLoad>) {
                                e.startValue *= f;
                                e.endValue *= f;
                                e.startTorque *= f;
                                e.endTorque *= f;
                                model.distributedLoads.push_back(std::move(e));
                            } else if constexpr (std::is_same_v<A, core::ConcentratedLoad>) {
                                e.force *= f;
                                e.moment *= f;
                                model.concentratedLoads.push_back(std::move(e));
                            } else if constexpr (std::is_same_v<A, core::TemperatureChange>) {
                                e.deltaT *= f;
                                model.temperatureChanges.push_back(std::move(e));
                            } else if constexpr (std::is_same_v<A, core::TemperatureGradient>) {
                                e.alongY *= f;
                                e.alongZ *= f;
                                model.temperatureGradients.push_back(std::move(e));
                            } else if constexpr (std::is_same_v<A, core::InitialStrain>) {
                                e.strain *= f;
                                model.initialStrains.push_back(std::move(e));
                            }
                        }
                    },
                    l->action);
            }
        }
        if (weighted) {
            model.gravity = gravity;
        }
    }

    // One restraint per (dof, value), with all its nodes.
    std::map<std::pair<core::DofType, double>, std::vector<core::ExternalId>> byValue;
    for (const auto& [key, value] : restrained) {
        byValue[{key.second, value}].push_back(key.first);
    }
    for (auto& [key, nodes] : byValue) {
        model.restraints.push_back(
            {.nodes = nodeIds(std::move(nodes)), .dof = key.first, .value = key.second});
    }
    return result;
}

namespace detail {

void addCoreIssues(BuildResult& built)
{
    // The core's own validation of the model built. Its issues name elements
    // and nodes, not items of the document.
    for (core::ModelIssue& issue : core::validate(built.model)) {
        built.issues.push_back({.severity = Severity::Error,
                                .kind = IssueKind::Core,
                                .item = std::nullopt,
                                .detail = std::move(issue.message)});
    }
}

} // namespace detail

std::vector<BuildIssue> checkModel(const Document& document, const geom::Drawing& drawing,
                                   const MeshContext& mesh, LoadSelection load)
{
    BuildResult built = buildModel(document, drawing, mesh, load);
    if (mesh.mesh) {
        detail::addCoreIssues(built);
    }
    return std::move(built.issues);
}

TargetState targetState(const Targets& targets, const geom::Drawing& drawing,
                        const MeshContext& mesh)
{
    for (const geom::EntityId e : targets.entities) {
        if (drawing.find(e) == nullptr) {
            return TargetState::MissingEntity;
        }
    }
    if ((!targets.elements.empty() || !targets.nodes.empty()) &&
        targets.meshGeneration != mesh.generation) {
        return TargetState::StaleSelection;
    }
    for (const GroupTarget& g : targets.groups) {
        if (!mesh.mesh || mesh.mesh->findGroup(g.name, g.dimension) == nullptr) {
            return TargetState::MissingGroup;
        }
    }
    return TargetState::Valid;
}

} // namespace fem::setup
