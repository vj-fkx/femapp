#include "symbol_builder.hpp"

#include "result_display_settings.hpp"

#include <fem/core/beam_element.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/element_type.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <unordered_map>

namespace fem::app {

namespace {

using Eigen::Vector3d;

// A straight bar: its ends and how its assignment orients it.
struct Bar
{
    Vector3d from;
    Vector3d to;
    const core::Assignment* assignment = nullptr;
};

bool isThreeD(core::ElementKind kind)
{
    return kind == core::ElementKind::Truss3D || kind == core::ElementKind::Beam3D;
}

bool isBeam(core::ElementKind kind)
{
    return kind == core::ElementKind::Beam2D || kind == core::ElementKind::Beam3D;
}

// The local axes of a bar; none for a truss without its assignment or a
// degenerate bar.
std::optional<Eigen::Matrix3d> axesOf(const Bar& bar)
{
    const core::ElementKind kind =
        bar.assignment != nullptr ? bar.assignment->kind : core::ElementKind::Beam3D;
    const auto axes = core::beamAxes(
        bar.from, bar.to, bar.assignment != nullptr ? bar.assignment->reference : std::nullopt,
        bar.assignment != nullptr ? bar.assignment->rollAngle : 0.0, kind);
    return axes ? std::optional<Eigen::Matrix3d>(*axes) : std::nullopt;
}

double along(const core::BarPosition& at, double length)
{
    const double s = at.fraction ? at.value : (length > 0.0 ? at.value / length : 0.0);
    return std::clamp(s, 0.0, 1.0);
}

// The symbols of the actions on bars and nodes, wherever they come from.
struct Collector
{
    render::SymbolSet set;
    // Per node position (as a key), the sum of the forces and moments, so
    // that a combination or several loads draw one arrow.
    struct NodeLoad
    {
        Vector3d at;
        Vector3d force = Vector3d::Zero();
        Vector3d moment = Vector3d::Zero();
    };
    std::map<std::tuple<double, double, double>, NodeLoad> nodeLoads;
    std::map<std::tuple<double, double, double>, render::SupportSymbol> supports;

    static std::tuple<double, double, double> key(const Vector3d& p)
    {
        return {p.x(), p.y(), p.z()};
    }

    void support(const Vector3d& at, std::uint8_t held, bool planar)
    {
        auto [it, added] = supports.try_emplace(
            key(at), render::SupportSymbol{.at = at, .held = 0, .planar = planar});
        it->second.held = static_cast<std::uint8_t>(it->second.held | held);
    }

    void nodal(const Vector3d& at, const Vector3d& force, const Vector3d& moment)
    {
        auto [it, added] = nodeLoads.try_emplace(key(at), NodeLoad{.at = at});
        it->second.force += force;
        it->second.moment += moment;
    }

    void distributed(const Bar& bar, const core::DistributedLoad& load, double factor)
    {
        Vector3d start = factor * load.startValue;
        Vector3d end = factor * load.endValue;
        if (load.axes == core::Axes::Local) {
            const auto axes = axesOf(bar);
            if (!axes) {
                return;
            }
            start = *axes * start;
            end = *axes * end;
        }
        const double length = (bar.to - bar.from).norm();
        const double s0 = along(load.from, length);
        const double s1 = along(load.to, length);
        if (s1 <= s0) {
            return;
        }
        set.distributed.push_back({.from = bar.from + s0 * (bar.to - bar.from),
                                   .to = bar.from + s1 * (bar.to - bar.from),
                                   .startValue = start,
                                   .endValue = end});
    }

    void concentrated(const Bar& bar, const core::ConcentratedLoad& load, double factor)
    {
        Vector3d force = factor * load.force;
        Vector3d moment = factor * load.moment;
        if (load.axes == core::Axes::Local) {
            const auto axes = axesOf(bar);
            if (!axes) {
                return;
            }
            force = *axes * force;
            moment = *axes * moment;
        }
        const Vector3d at =
            bar.from + along(load.at, (bar.to - bar.from).norm()) * (bar.to - bar.from);
        if (force.norm() > 0.0) {
            set.forces.push_back({.at = at, .vector = force});
        }
        if (moment.norm() > 0.0) {
            set.moments.push_back({.at = at, .axis = moment});
        }
    }

    void axes(const Bar& bar)
    {
        if (bar.assignment == nullptr || !isBeam(bar.assignment->kind)) {
            return;
        }
        if (const auto a = axesOf(bar)) {
            set.axes.push_back({.at = 0.5 * (bar.from + bar.to), .axes = *a});
        }
    }

    render::SymbolSet finish()
    {
        for (const auto& [k, s] : supports) {
            set.supports.push_back(s);
        }
        for (const auto& [k, l] : nodeLoads) {
            if (l.force.norm() > 0.0) {
                set.forces.push_back({.at = l.at, .vector = l.force});
            }
            if (l.moment.norm() > 0.0) {
                set.moments.push_back({.at = l.at, .axis = l.moment});
            }
        }
        return std::move(set);
    }
};

std::uint8_t heldBits(const setup::SupportItem& support)
{
    unsigned bits = 0;
    for (std::size_t k = 0; k < support.held.size(); ++k) {
        if (support.held.at(k)) {
            bits |= 1U << k;
        }
    }
    return static_cast<std::uint8_t>(bits);
}

// The cases of the active load case or combination, with their factors.
std::vector<std::pair<setup::ItemId, double>> activeTerms(const setup::Document& document)
{
    const auto active = document.activeCase();
    if (!active) {
        return {};
    }
    const setup::ItemId id = *active;
    const setup::Item* item = document.find(id);
    if (item == nullptr) {
        return {};
    }
    if (const auto* c = std::get_if<setup::Combination>(item)) {
        return c->terms;
    }
    return {{id, 1.0}};
}

// ---- On the mesh: the model built. ----

render::SymbolSet onMesh(const setup::Document& document, const geom::Drawing& drawing,
                         const setup::MeshContext& context, const SymbolOptions& options)
{
    const core::Mesh& mesh = *context.mesh;
    const auto active = document.activeCase();
    const core::Model model =
        setup::buildModel(document, drawing, context, {.item = active.value_or(setup::ItemId{})})
            .model;
    bool threeD = !mesh.liesInXYPlane();
    std::unordered_map<core::ElementIndex, const core::Assignment*> assigned;
    for (const core::Assignment& a : model.assignments) {
        threeD = threeD || isThreeD(a.kind);
        if (const auto elements = core::resolve(mesh, a.elements)) {
            for (const core::ElementIndex e : *elements) {
                assigned[e] = &a;
            }
        }
    }
    const auto barOf = [&](core::ElementIndex e) -> std::optional<Bar> {
        const core::ElementType type = mesh.elementType(e);
        if (type != core::ElementType::Line2 && type != core::ElementType::Line3) {
            return std::nullopt;
        }
        const auto nodes = mesh.elementNodes(e);
        const auto found = assigned.find(e);
        return Bar{.from = mesh.nodeCoordinates(nodes[0]),
                   .to = mesh.nodeCoordinates(nodes[1]),
                   .assignment = found != assigned.end() ? found->second : nullptr};
    };

    Collector out;
    // Supports: the dofs held (value 0) per node; settlements are loads.
    std::unordered_map<core::NodeIndex, unsigned> held;
    for (const core::Restraint& r : model.restraints) {
        if (const auto nodes = core::resolve(mesh, r.nodes)) {
            for (const core::NodeIndex n : *nodes) {
                held[n] |= 1U << static_cast<unsigned>(r.dof);
            }
        }
    }
    for (const auto& [node, bits] : held) {
        out.support(mesh.nodeCoordinates(node), static_cast<std::uint8_t>(bits), !threeD);
    }
    for (const core::NodalForce& f : model.nodalForces) {
        if (const auto nodes = core::resolve(mesh, f.nodes)) {
            for (const core::NodeIndex n : *nodes) {
                out.nodal(mesh.nodeCoordinates(n), f.force, f.moment);
            }
        }
    }
    const auto onBars = [&](const core::ElementTarget& target, const auto& add) {
        if (const auto elements = core::resolve(mesh, target)) {
            for (const core::ElementIndex e : *elements) {
                if (const auto bar = barOf(e)) {
                    add(*bar);
                }
            }
        }
    };
    for (const core::DistributedLoad& l : model.distributedLoads) {
        onBars(l.elements, [&](const Bar& bar) { out.distributed(bar, l, 1.0); });
    }
    for (const core::ConcentratedLoad& l : model.concentratedLoads) {
        onBars(l.elements, [&](const Bar& bar) { out.concentrated(bar, l, 1.0); });
    }
    // REQ-010-09: the local axes.
    if (options.axes == AxesShown::All) {
        for (const auto& [e, a] : assigned) {
            if (const auto bar = barOf(e)) {
                out.axes(*bar);
            }
        }
    } else if (options.axes == AxesShown::Selected) {
        std::set<core::ElementIndex> chosen;
        for (const core::ExternalId id : options.axesOf.elements) {
            if (const auto e = mesh.findElement(id)) {
                chosen.insert(*e);
            }
        }
        // Selected entities: their elements through the map of MESH.
        if (context.map && !options.axesOf.entities.empty()) {
            const std::set<geom::EntityId> entities(options.axesOf.entities.begin(),
                                                    options.axesOf.entities.end());
            const auto& origins = context.map->elements;
            for (std::size_t e = 0; e < origins.size(); ++e) {
                if (entities.contains(origins[e].entity)) {
                    chosen.insert(static_cast<core::ElementIndex>(e));
                }
            }
        }
        for (const core::ElementIndex e : chosen) {
            if (const auto bar = barOf(e)) {
                out.axes(*bar);
            }
        }
    }
    return out.finish();
}

// ---- Without a mesh: the entities of the drawing. ----

render::SymbolSet onDrawing(const setup::Document& document, const geom::Drawing& drawing,
                            const SymbolOptions& options)
{
    // The assignment of each entity (the last one wins, as assign() keeps
    // one per target).
    std::unordered_map<std::uint64_t, const core::Assignment*> assigned;
    bool threeD = false;
    for (const setup::Item& item : document.items()) {
        if (const auto* bars = std::get_if<setup::BarAssignment>(&item)) {
            threeD = threeD || isThreeD(bars->data.kind);
            for (const geom::EntityId id : bars->targets.entities) {
                assigned[static_cast<std::uint64_t>(id)] = &bars->data;
            }
        }
    }
    for (const geom::Entity& entity : drawing.entities()) {
        for (const geom::Vec3& p : geom::verticesOf(entity)) {
            threeD = threeD || std::abs(p.z()) > 0.0;
        }
    }
    const auto barsOf = [&](geom::EntityId id) {
        std::vector<Bar> bars;
        const geom::Entity* entity = drawing.find(id);
        if (entity == nullptr) {
            return bars;
        }
        const auto found = assigned.find(static_cast<std::uint64_t>(id));
        for (const geom::Segment& s : geom::segmentsOf(*entity)) {
            bars.push_back({.from = s.a,
                            .to = s.b,
                            .assignment = found != assigned.end() ? found->second : nullptr});
        }
        return bars;
    };
    const auto verticesOf = [&](geom::EntityId id) {
        const geom::Entity* entity = drawing.find(id);
        return entity != nullptr ? geom::verticesOf(*entity) : std::vector<geom::Vec3>{};
    };

    Collector out;
    for (const setup::Item& item : document.items()) {
        if (const auto* support = std::get_if<setup::SupportItem>(&item)) {
            for (const geom::EntityId id : support->targets.entities) {
                for (const geom::Vec3& p : verticesOf(id)) {
                    out.support(p, heldBits(*support), !threeD);
                }
            }
        }
    }
    for (const auto& [loadCase, factor] : activeTerms(document)) {
        for (const setup::Item& item : document.items()) {
            const auto* load = std::get_if<setup::LoadItem>(&item);
            if (load == nullptr || load->loadCase != loadCase) {
                continue;
            }
            for (const geom::EntityId id : load->targets.entities) {
                if (const auto* f = std::get_if<core::NodalForce>(&load->action)) {
                    for (const geom::Vec3& p : verticesOf(id)) {
                        out.nodal(p, factor * f->force, factor * f->moment);
                    }
                } else if (const auto* d = std::get_if<core::DistributedLoad>(&load->action)) {
                    for (const Bar& bar : barsOf(id)) {
                        out.distributed(bar, *d, factor);
                    }
                } else if (const auto* c = std::get_if<core::ConcentratedLoad>(&load->action)) {
                    for (const Bar& bar : barsOf(id)) {
                        out.concentrated(bar, *c, factor);
                    }
                }
            }
        }
    }
    if (options.axes != AxesShown::None) {
        for (const auto& [id, a] : assigned) {
            const geom::EntityId entity{id};
            if (options.axes == AxesShown::Selected &&
                std::ranges::find(options.axesOf.entities, entity) ==
                    options.axesOf.entities.end()) {
                continue;
            }
            for (const Bar& bar : barsOf(entity)) {
                out.axes(bar);
            }
        }
    }
    return out.finish();
}

} // namespace

render::SymbolSet buildSymbols(const setup::Document& document, const geom::Drawing& drawing,
                               const setup::MeshContext& mesh, const SymbolOptions& options)
{
    const render::SymbolSet base = mesh.mesh ? onMesh(document, drawing, mesh, options)
                                             : onDrawing(document, drawing, options);
    return withResults(base, options);
}

render::SymbolSet withResults(const render::SymbolSet& base, const SymbolOptions& options)
{
    render::SymbolSet set;
    set.supports = base.supports;
    set.axes = base.axes;
    if (options.loadsShown) {
        set.forces = base.forces;
        set.moments = base.moments;
        set.distributed = base.distributed;
    }
    set.reactionForces = options.reactionForces;
    set.reactionMoments = options.reactionMoments;
    return set;
}

ReactionSymbols reactionSymbols(const setup::AnalysisResult& result)
{
    ReactionSymbols out;
    const core::StaticResult* solved = staticOf(result);
    if (solved == nullptr || !result.mesh) {
        return out;
    }
    double largestForce = 0.0;
    double largestMoment = 0.0;
    for (const core::NodeIndex node : result.supported) {
        const auto n = static_cast<std::size_t>(node);
        if (n < solved->reactions.size()) {
            largestForce = std::max(largestForce, solved->reactions[n].cwiseAbs().maxCoeff());
        }
        if (n < solved->reactionMoments.size()) {
            largestMoment =
                std::max(largestMoment, solved->reactionMoments[n].cwiseAbs().maxCoeff());
        }
    }
    constexpr double kNegligible = 1e-9;
    for (const core::NodeIndex node : result.supported) {
        const auto n = static_cast<std::size_t>(node);
        const Eigen::Vector3d at = result.mesh->nodeCoordinates(node);
        for (int k = 0; k < 3; ++k) {
            if (n < solved->reactions.size()) {
                const double v = solved->reactions[n](k);
                if (std::abs(v) > kNegligible * largestForce) {
                    out.forces.push_back({.at = at, .vector = v * Eigen::Vector3d::Unit(k)});
                }
            }
            if (n < solved->reactionMoments.size()) {
                const double v = solved->reactionMoments[n](k);
                if (std::abs(v) > kNegligible * largestMoment) {
                    out.moments.push_back({.at = at, .axis = v * Eigen::Vector3d::Unit(k)});
                }
            }
        }
    }
    return out;
}

} // namespace fem::app
