#include "detail/model_validation.hpp"
#include "detail/static_stages.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/beam_element.hpp>
#include <fem/core/beam_iso.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>
#include <fem/core/beam_state.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/element_type.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/core/truss_element.hpp>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fem::core {

namespace {

constexpr double kNoStiffness = 1e-12; // of the largest diagonal entry

using detail::kNoAssignment;
using detail::quoted;

// Per element: EA, imposed strain and weight; zero for elements that are not
// bars (design 007 §5.1).
std::vector<BarData> barData(const Model& model, const detail::ValidationTables& tables)
{
    const Mesh& mesh = *model.mesh;
    std::vector<BarData> bars(static_cast<std::size_t>(mesh.elementCount()));
    for (std::size_t e = 0; e < bars.size(); ++e) {
        const int owner = tables.owner[e];
        if (owner == kNoAssignment) {
            continue;
        }
        const Assignment& assignment = model.assignments[static_cast<std::size_t>(owner)];
        const Material& material = *model.findMaterial(assignment.material);
        const double area = detail::sectionArea(*model.findSection(assignment.section));
        bars[e].axialStiffness = material.youngsModulus.value_or(0.0) * area;
        if (model.gravity) {
            bars[e].weight = material.density.value_or(0.0) * area * *model.gravity;
        }
    }
    for (const TemperatureChange& change : model.temperatureChanges) {
        for (const ElementIndex element :
             resolve(mesh, change.elements).value_or(std::vector<ElementIndex>{})) {
            const int owner = tables.owner[static_cast<std::size_t>(element)];
            if (owner != kNoAssignment) {
                const auto& name = model.assignments[static_cast<std::size_t>(owner)].material;
                const double alpha = model.findMaterial(name)->thermalExpansion.value_or(0.0);
                bars[static_cast<std::size_t>(element)].imposedStrain += alpha * change.deltaT;
            }
        }
    }
    for (const InitialStrain& initial : model.initialStrains) {
        for (const ElementIndex element :
             resolve(mesh, initial.elements).value_or(std::vector<ElementIndex>{})) {
            if (tables.owner[static_cast<std::size_t>(element)] != kNoAssignment) {
                bars[static_cast<std::size_t>(element)].imposedStrain += initial.strain;
            }
        }
    }
    return bars;
}

using detail::Beams;

// REQ-008d-03 (design 008d §5.2): the warping of each end of the beams with
// warping. By default continuous between ends whose tangents at a node are
// collinear, free otherwise; the WarpingEnd items override it.
void resolveWarping(const Model& model, Beams& beams)
{
    constexpr double kCollinear = 1e-6;
    const Mesh& mesh = *model.mesh;
    const auto count = beams.data.size();
    const auto warps = [&beams](std::size_t e) { return beams.data[e].warpingStiffness > 0.0; };
    const auto tangent = [&](std::size_t e, int end) {
        const BeamData& beam = beams.data[e];
        if (beams.isIso[e]) {
            // dx/dxi of the quadratic curve at xi = -1 and 1 (Gmsh order:
            // the ends, then the middle).
            const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
            const Eigen::Vector3d p = mesh.nodeCoordinates(nodes[0]) + beam.offsets[0];
            const Eigen::Vector3d q = mesh.nodeCoordinates(nodes[1]) + beam.offsets[1];
            const Eigen::Vector3d m = mesh.nodeCoordinates(nodes[2]);
            const Eigen::Vector3d d = end == 0 ? Eigen::Vector3d(-1.5 * p - 0.5 * q + 2.0 * m)
                                               : Eigen::Vector3d(0.5 * p + 1.5 * q - 2.0 * m);
            return Eigen::Vector3d(d.normalized());
        }
        return Eigen::Vector3d((end == 0 ? beam.axes : beam.endAxes).col(0));
    };
    std::vector<std::vector<std::pair<std::size_t, int>>> atNode(
        static_cast<std::size_t>(mesh.nodeCount()));
    for (std::size_t e = 0; e < count; ++e) {
        if (warps(e)) {
            const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
            atNode[static_cast<std::size_t>(nodes[0])].emplace_back(e, 0);
            atNode[static_cast<std::size_t>(nodes[1])].emplace_back(e, 1);
        }
    }
    for (const auto& ends : atNode) {
        for (const auto& [e, end] : ends) {
            bool collinear = false;
            for (const auto& [other, otherEnd] : ends) {
                if ((other != e || otherEnd != end) &&
                    tangent(e, end).cross(tangent(other, otherEnd)).norm() < kCollinear) {
                    collinear = true;
                }
            }
            BeamData& beam = beams.data[e];
            beam.warpingContinuous.at(static_cast<std::size_t>(end)) = collinear;
            beam.warpingSprings.at(static_cast<std::size_t>(end)) = 0.0;
        }
    }
    for (const WarpingEnd& item : model.warpingEnds) {
        for (const ElementIndex element :
             resolve(mesh, item.elements).value_or(std::vector<ElementIndex>{})) {
            const auto e = static_cast<std::size_t>(element);
            if (!warps(e)) {
                continue;
            }
            for (std::size_t end = 0; end < 2; ++end) {
                if ((end == 0 && item.end == BarEnd::End) ||
                    (end == 1 && item.end == BarEnd::Start)) {
                    continue;
                }
                BeamData& beam = beams.data[e];
                beam.warpingContinuous.at(end) = item.condition == WarpingCondition::Continuous;
                switch (item.condition) {
                case WarpingCondition::Free:
                case WarpingCondition::Continuous:
                    beam.warpingSprings.at(end) = 0.0;
                    break;
                case WarpingCondition::Restrained:
                    beam.warpingSprings.at(end) = kRigid;
                    break;
                case WarpingCondition::Elastic:
                    beam.warpingSprings.at(end) = item.stiffness;
                    break;
                }
            }
        }
    }
    // The isoparametric beams keep a copy of their data.
    for (std::size_t e = 0; e < count; ++e) {
        if (beams.isIso[e] && warps(e)) {
            beams.iso[e].beam.warpingSprings = beams.data[e].warpingSprings;
            beams.iso[e].beam.warpingContinuous = beams.data[e].warpingContinuous;
        }
    }
}

// With `secondOrder` (feature 008c) the straight beams use the exact element.
Beams beamData(const Model& model, const detail::ValidationTables& tables, bool secondOrder)
{
    const Mesh& mesh = *model.mesh;
    const auto count = static_cast<std::size_t>(mesh.elementCount());
    Beams beams;
    beams.data.resize(count);
    beams.loads.resize(count);
    beams.length.assign(count, 0.0);
    beams.exact.assign(count, false);
    beams.iso.resize(count);
    beams.isIso.assign(count, false);
    if (!tables.hasBeams) {
        return beams;
    }
    std::vector<SpanFrame> frames(count);
    std::vector<const Material*> materials(count, nullptr);
    std::vector<bool> isBeam(count, false);
    for (std::size_t e = 0; e < count; ++e) {
        const int owner = tables.owner[e];
        if (owner == kNoAssignment) {
            continue;
        }
        const Assignment& assignment = model.assignments[static_cast<std::size_t>(owner)];
        if (!detail::isBeam(assignment.kind)) {
            continue;
        }
        const auto setup = detail::beamSetup(model, static_cast<ElementIndex>(e), tables);
        if (!setup) {
            continue; // cannot happen after a clean validation
        }
        isBeam[e] = true;
        beams.data[e] = setup->data;
        beams.exact[e] = setup->data.warpingStiffness > 0.0 || setup->data.arcLength > 0.0 ||
                         (secondOrder && !setup->isoparametric);
        if (setup->isoparametric) {
            beams.isIso[e] = true;
            beams.iso[e] = {.beam = setup->data,
                            .reference = setup->reference,
                            .roll = setup->roll,
                            .loads = {}};
        }
        beams.length[e] = setup->length;
        frames[e] = setup->frame;
        materials[e] = model.findMaterial(assignment.material);
        if (model.gravity) {
            const double area = detail::sectionArea(*model.findSection(assignment.section));
            const Eigen::Vector3d weight =
                materials[e]->density.value_or(0.0) * area * *model.gravity;
            addDistributed(beams.loads[e], frames[e], Axes::Global, weight, weight, 0.0,
                           setup->length);
            beams.iso[e].loads.distributed.push_back({.axes = Axes::Global,
                                                      .startValue = weight,
                                                      .endValue = weight,
                                                      .from = 0.0,
                                                      .to = setup->length});
        }
    }
    // Every element of a target that is a beam.
    const auto forBeams = [&](const ElementTarget& target, const auto& action) {
        for (const ElementIndex element :
             resolve(mesh, target).value_or(std::vector<ElementIndex>{})) {
            if (isBeam[static_cast<std::size_t>(element)]) {
                action(static_cast<std::size_t>(element));
            }
        }
    };
    for (const TemperatureChange& change : model.temperatureChanges) {
        forBeams(change.elements, [&](std::size_t e) {
            const double strain = materials[e]->thermalExpansion.value_or(0.0) * change.deltaT;
            beams.loads[e].imposedStrain += strain;
            beams.iso[e].loads.imposedStrain += strain;
        });
    }
    for (const InitialStrain& initial : model.initialStrains) {
        forBeams(initial.elements, [&](std::size_t e) {
            beams.loads[e].imposedStrain += initial.strain;
            beams.iso[e].loads.imposedStrain += initial.strain;
        });
    }
    for (const TemperatureGradient& gradient : model.temperatureGradients) {
        forBeams(gradient.elements, [&](std::size_t e) {
            SpanLoads curvatures;
            addGradient(curvatures, frames[e], materials[e]->thermalExpansion.value_or(0.0),
                        gradient.alongY, gradient.alongZ);
            beams.loads[e].curvatureY += curvatures.curvatureY;
            beams.loads[e].curvatureZ += curvatures.curvatureZ;
            beams.iso[e].loads.curvatureY += curvatures.curvatureY;
            beams.iso[e].loads.curvatureZ += curvatures.curvatureZ;
        });
    }
    // REQ-008d-13: the point of the section where a load acts, from the
    // local axes of the user to the principal ones.
    const auto principalPoint = [&frames](std::size_t e, const Eigen::Vector2d& point) {
        const double c = std::cos(frames[e].principalAngle);
        const double s = std::sin(frames[e].principalAngle);
        return Eigen::Vector2d(c * point.x() + s * point.y(), -s * point.x() + c * point.y());
    };
    for (const DistributedLoad& load : model.distributedLoads) {
        forBeams(load.elements, [&](std::size_t e) {
            const Eigen::Vector2d point = load.point.value_or(Eigen::Vector2d::Zero());
            addDistributed(beams.loads[e], frames[e], load.axes, load.startValue, load.endValue,
                           positionOn(load.from, beams.length[e]),
                           positionOn(load.to, beams.length[e]), load.startTorque, load.endTorque,
                           point);
            beams.iso[e].loads.distributed.push_back(
                {.axes = load.axes,
                 .startValue = load.startValue,
                 .endValue = load.endValue,
                 .from = positionOn(load.from, beams.length[e]),
                 .to = positionOn(load.to, beams.length[e]),
                 .startTorque = load.startTorque,
                 .endTorque = load.endTorque,
                 .point = principalPoint(e, point)});
        });
    }
    for (const ConcentratedLoad& load : model.concentratedLoads) {
        forBeams(load.elements, [&](std::size_t e) {
            const Eigen::Vector2d point = load.point.value_or(Eigen::Vector2d::Zero());
            addConcentrated(beams.loads[e], frames[e], load.axes, load.force, load.moment,
                            positionOn(load.at, beams.length[e]), point);
            beams.iso[e].loads.concentrated.push_back({.axes = load.axes,
                                                       .force = load.force,
                                                       .moment = load.moment,
                                                       .at = positionOn(load.at, beams.length[e]),
                                                       .point = principalPoint(e, point)});
        });
    }
    for (std::size_t e = 0; e < count; ++e) {
        if (isBeam[e] && !beams.exact[e] && !beams.isIso[e]) {
            beams.data[e].fixedEndLoads =
                equivalentLoads(beams.data[e], beams.length[e], beams.loads[e]);
        }
    }
    resolveWarping(model, beams);
    return beams;
}

// Feature 008c: whether the span loads of a straight bar have axial
// components, and their integral from 0 to s (design 008c §5.2).
bool hasAxialSpanLoad(const SpanLoads& loads)
{
    const auto axial = [](const SpanPiece& piece) {
        return piece.start(0) != 0.0 || piece.end(0) != 0.0;
    };
    const auto point = [](const SpanPoint& p) { return p.force.x() != 0.0; };
    return std::ranges::any_of(loads.pieces, axial) || std::ranges::any_of(loads.points, point);
}

double axialLoadBefore(const SpanLoads& loads, double s)
{
    double total = 0.0;
    for (const SpanPiece& piece : loads.pieces) {
        const double a = piece.from;
        const double b = std::min(piece.to, s);
        if (!(b > a)) {
            continue;
        }
        const double span = piece.to - piece.from;
        const double slope = span > 0.0 ? (piece.end(0) - piece.start(0)) / span : 0.0;
        total += piece.start(0) * (b - a) + 0.5 * slope * (b - a) * (b - a);
    }
    for (const SpanPoint& point : loads.points) {
        if (point.at < s) {
            total += point.force.x();
        }
    }
    return total;
}

std::string issueList(const std::vector<ModelIssue>& issues)
{
    std::string message = issues.size() == 1
                              ? std::string("The model has 1 problem:")
                              : "The model has " + std::to_string(issues.size()) + " problems:";
    for (const ModelIssue& issue : issues) {
        message += "\n- " + issue.message;
    }
    return message;
}

std::string dofLabel(const Mesh& mesh, const DofMap& dofs, DofIndex dof)
{
    return "the node " + std::to_string(mesh.nodeId(dofs.nodeOf(dof))) + " in " +
           std::string(dofName(dofs.typeOf(dof)));
}

} // namespace

namespace detail {

StaticProblem::StaticProblem(const Model& model, const StaticOptions& options)
    : m_model(model)
    , m_options(options)
{}

StaticProblem::~StaticProblem() = default;

Expected<std::unique_ptr<StaticProblem>>
StaticProblem::prepare(const Model& model, const StaticOptions& options, bool secondOrder)
{
    if (!model.mesh) {
        return makeError(issueList({{"The model has no mesh."}}));
    }
    // The constructor is private: no make_unique.
    std::unique_ptr<StaticProblem> problem(new StaticProblem(model, options));
    StaticProblem& p = *problem;
    p.m_validation = validateModel(model);
    if (!p.m_validation.issues.empty()) {
        return makeError(issueList(p.m_validation.issues));
    }
    const ValidationTables& tables = p.m_validation.tables;
    const Mesh& mesh = *model.mesh;
    const auto elementCount = static_cast<std::size_t>(mesh.elementCount());

    // Bars and beams, and their sets, one per kind.
    p.m_bars = barData(model, tables);
    p.m_beams = beamData(model, tables, secondOrder);
    p.m_negative.assign(elementCount, 0);
    p.m_kindOf.assign(elementCount, ElementKind::Truss3D);
    // The exact beams (design 008b §5.3, 008d §5.2): a set per kind and pair
    // of continuous warping ends, since the ends decide the dofs. The same
    // for the isoparametric ones, with or without warping (design 008d §5.3).
    std::map<int, std::vector<ElementIndex>> exactMembers;
    std::map<int, std::vector<ElementIndex>> isoMembers;
    const auto isoKey = [&p](ElementKind kind, std::size_t e) {
        const BeamData& beam = p.m_beams.data[e];
        const auto& continuous = beam.warpingContinuous;
        return 8 * static_cast<int>(kind) + 4 * (beam.warpingStiffness > 0.0 ? 1 : 0) +
               2 * (continuous[0] ? 1 : 0) + (continuous[1] ? 1 : 0);
    };
    const auto exactKey = [&p](ElementKind kind, std::size_t e) {
        const auto& continuous = p.m_beams.data[e].warpingContinuous;
        return 4 * static_cast<int>(kind) + 2 * (continuous[0] ? 1 : 0) + (continuous[1] ? 1 : 0);
    };
    for (ElementIndex element = 0; element < mesh.elementCount(); ++element) {
        const auto e = static_cast<std::size_t>(element);
        const int owner = tables.owner[e];
        if (owner != kNoAssignment) {
            const auto kind = model.assignments[static_cast<std::size_t>(owner)].kind;
            p.m_kindOf[e] = kind;
            if (p.m_beams.isIso[e]) {
                isoMembers[isoKey(kind, e)].push_back(element);
            } else if (p.m_beams.exact[e]) {
                exactMembers[exactKey(kind, e)].push_back(element);
            } else {
                p.m_members.at(static_cast<std::size_t>(kind)).push_back(element);
            }
        }
    }
    p.m_formulations.push_back(std::make_unique<TrussElement>(ElementKind::Truss2D, p.m_bars));
    p.m_formulations.push_back(std::make_unique<TrussElement>(ElementKind::Truss3D, p.m_bars));
    p.m_formulations.push_back(std::make_unique<BeamElement>(ElementKind::Beam2D, p.m_beams.data));
    p.m_formulations.push_back(std::make_unique<BeamElement>(ElementKind::Beam3D, p.m_beams.data));
    for (std::size_t k = 0; k < p.m_members.size(); ++k) {
        if (!p.m_members.at(k).empty()) {
            p.m_sets.push_back(
                {.elements = p.m_members.at(k), .formulation = p.m_formulations.at(k).get()});
        }
    }
    p.m_isoMembers.reserve(isoMembers.size());
    for (auto& [key, elements] : isoMembers) {
        p.m_isoMembers.push_back(std::move(elements));
        const std::array<bool, 2> continuous = {(key / 2) % 2 == 1, key % 2 == 1};
        p.m_formulations.push_back(std::make_unique<IsoBeamElement>(
            static_cast<ElementKind>(key / 8), p.m_beams.iso, (key / 4) % 2 == 1, continuous));
        p.m_sets.push_back(
            {.elements = p.m_isoMembers.back(), .formulation = p.m_formulations.back().get()});
    }
    p.m_exactMembers.reserve(exactMembers.size());
    for (auto& [key, elements] : exactMembers) {
        p.m_exactMembers.push_back(std::move(elements));
        const std::array<bool, 2> continuous = {(key / 2) % 2 == 1, key % 2 == 1};
        p.m_formulations.push_back(
            std::make_unique<ExactBeamElement>(static_cast<ElementKind>(key / 4), p.m_beams.data,
                                               p.m_beams.loads, p.m_negative, continuous));
        p.m_sets.push_back(
            {.elements = p.m_exactMembers.back(), .formulation = p.m_formulations.back().get()});
    }

    // Dofs, supports and loads; validation guarantees every dof exists.
    p.m_dofs = DofMap::build(mesh, p.m_sets);
    for (const Restraint& restraint : model.restraints) {
        for (const NodeIndex node :
             resolve(mesh, restraint.nodes).value_or(std::vector<NodeIndex>{})) {
            if (auto prescribed = p.m_dofs.prescribe(node, restraint.dof, restraint.value);
                !prescribed) {
                return tl::unexpected(prescribed.error());
            }
        }
    }
    constexpr std::array<DofType, 6> kNodeDofs = {DofType::U,  DofType::V,  DofType::W,
                                                  DofType::RX, DofType::RY, DofType::RZ};
    for (const NodalForce& force : model.nodalForces) {
        Eigen::Matrix<double, 6, 1> values;
        values << force.force, force.moment;
        for (const NodeIndex node : resolve(mesh, force.nodes).value_or(std::vector<NodeIndex>{})) {
            for (std::size_t k = 0; k < kNodeDofs.size(); ++k) {
                const double value = values(static_cast<Eigen::Index>(k));
                if (value != 0.0 && p.m_dofs.find(node, kNodeDofs.at(k))) {
                    p.m_loads.push_back({.node = node, .type = kNodeDofs.at(k), .value = value});
                }
            }
            // REQ-008d-04: the bimoment, on the warping dof of the node.
            if (force.bimoment != 0.0 && p.m_dofs.find(node, DofType::Warp)) {
                p.m_loads.push_back({.node = node, .type = DofType::Warp, .value = force.bimoment});
            }
        }
    }
    p.m_dofOfEquation.assign(static_cast<std::size_t>(p.m_dofs.freeCount()), 0);
    for (DofIndex d = 0; d < p.m_dofs.size(); ++d) {
        if (const std::int32_t eq = p.m_dofs.equation(d); eq >= 0) {
            p.m_dofOfEquation[static_cast<std::size_t>(eq)] = d;
        }
    }
    return problem;
}

Expected<AssembledSystem> StaticProblem::assemble() const
{
    std::ranges::fill(m_negative, 0);
    return core::assemble(mesh(), m_dofs, m_sets, m_loads, m_options.threads);
}

Expected<void> StaticProblem::checkDiagonal(const SparseMatrix& matrix) const
{
    const Eigen::VectorXd diagonal = matrix.diagonal();
    if (diagonal.size() == 0) {
        return {};
    }
    const double largest = diagonal.cwiseAbs().maxCoeff();
    std::string list;
    for (Eigen::Index eq = 0; eq < diagonal.size(); ++eq) {
        if (!(diagonal[eq] > kNoStiffness * largest)) {
            list += "\n- " + equationLabel(static_cast<std::int32_t>(eq));
        }
    }
    if (list.empty()) {
        return {};
    }
    if (const std::string hint = warpingHint(diagonal, largest); !hint.empty()) {
        list += hint;
    } else if (list.find(" in r") != std::string::npos) {
        // REQ-008-25: often every beam at the node releases that moment.
        list += "\n(A rotation without stiffness: restrain it, or keep one beam at the node "
                "connected.)";
    }
    return makeError("The structure is unstable: no bar gives stiffness to" + list);
}

std::string StaticProblem::warpingHint(const Eigen::VectorXd& diagonal, double largest) const
{
    // REQ-008d-18: the nodes where beams with warping end and every such end
    // is free (no continuity, no spring, not restrained).
    const Mesh& mesh = this->mesh();
    std::vector<int> state(static_cast<std::size_t>(mesh.nodeCount()), 0); // 1 free, 2 held
    for (std::size_t e = 0; e < m_beams.data.size(); ++e) {
        const BeamData& beam = m_beams.data[e];
        if (!(beam.warpingStiffness > 0.0) || !m_beams.exact[e]) {
            continue;
        }
        const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
        for (std::size_t end = 0; end < 2; ++end) {
            int& s = state[static_cast<std::size_t>(nodes[end])];
            const bool free = !beam.warpingContinuous.at(end) && beam.warpingSprings.at(end) == 0.0;
            s = std::max(s, free ? 1 : 2);
        }
    }
    for (Eigen::Index eq = 0; eq < diagonal.size(); ++eq) {
        if (diagonal[eq] > kNoStiffness * largest) {
            continue;
        }
        const DofIndex dof = m_dofOfEquation[static_cast<std::size_t>(eq)];
        if (m_dofs.typeOf(dof) == DofType::RX &&
            state[static_cast<std::size_t>(m_dofs.nodeOf(dof))] == 1) {
            return "\n(A twist without stiffness where the warping of every beam is free: "
                   "restrain the warping at an end, as an end plate does, or make it "
                   "continuous.)";
        }
    }
    return {};
}

std::string StaticProblem::equationLabel(std::int32_t equation) const
{
    return dofLabel(mesh(), m_dofs, m_dofOfEquation[static_cast<std::size_t>(equation)]);
}

Eigen::VectorXd StaticProblem::valuesOf(const Eigen::VectorXd& free, bool prescribed) const
{
    Eigen::VectorXd values(m_dofs.size());
    for (DofIndex d = 0; d < m_dofs.size(); ++d) {
        const std::int32_t eq = m_dofs.equation(d);
        if (eq >= 0) {
            values[d] = free[eq];
        } else {
            values[d] = prescribed ? m_dofs.prescribedValue(d) : 0.0;
        }
    }
    return values;
}

std::int64_t StaticProblem::negativePivots() const
{
    std::int64_t total = 0;
    for (const int n : m_negative) {
        total += n;
    }
    return total;
}

ElementIndex StaticProblem::unstableElement() const
{
    const auto most = std::ranges::max_element(m_negative);
    if (most == m_negative.end() || *most <= 0) {
        return -1;
    }
    return static_cast<ElementIndex>(most - m_negative.begin());
}

std::vector<double> StaticProblem::nodeValues(ElementIndex element, int nodes,
                                              const Eigen::VectorXd& values) const
{
    const auto e = static_cast<std::size_t>(element);
    const auto ids = mesh().elementNodes(element);
    std::vector<double> out;
    for (int k = 0; k < nodes; ++k) {
        // The dofs of the beam kind, and the warping at a continuous end of
        // an exact beam (design 008d §5.2).
        DofMask used = dofsOf(m_kindOf[e]);
        const BeamData& beam = m_beams.data[e];
        const bool continuous = k < 2 && beam.warpingContinuous.at(static_cast<std::size_t>(k));
        const bool middle = k == 2 && m_beams.isIso[e]; // the isoparametric middle node
        if (beam.warpingStiffness > 0.0 && (continuous || middle)) {
            used = static_cast<DofMask>(used | mask(DofType::Warp));
        }
        for (int t = 0; t < kDofTypeCount; ++t) {
            const auto type = static_cast<DofType>(t);
            if ((used & mask(type)) != 0) {
                const auto dof = m_dofs.find(ids[static_cast<std::size_t>(k)], type);
                out.push_back(dof ? values[*dof] : 0.0);
            }
        }
    }
    return out;
}

std::vector<AxialProfile> StaticProblem::profilesOf(const Eigen::VectorXd& values) const
{
    const Mesh& mesh = this->mesh();
    const auto count = static_cast<std::size_t>(mesh.elementCount());
    std::vector<AxialProfile> profiles(count);
    const auto translation = [&](NodeIndex node) {
        Eigen::Vector3d u = Eigen::Vector3d::Zero();
        for (int k = 0; k < 3; ++k) {
            if (const auto dof = m_dofs.find(node, static_cast<DofType>(k))) {
                u(k) = values[*dof];
            }
        }
        return u;
    };
    const auto profileOf = [&](std::size_t e) {
        if (m_validation.tables.owner[e] == kNoAssignment) {
            return;
        }
        const auto element = static_cast<ElementIndex>(e);
        const auto ids = mesh.elementNodes(element);
        const Eigen::Vector3d x1 = mesh.nodeCoordinates(ids[0]);
        const Eigen::Vector3d x2 = mesh.nodeCoordinates(ids[1]);
        const ElementKind kind = m_kindOf[e];
        if (!isBeam(kind)) {
            profiles[e] = AxialProfile::constant(
                axialForce(m_bars[e], x1, x2, translation(ids[0]), translation(ids[1])));
            return;
        }
        const BeamData& beam = m_beams.data[e];
        if (m_beams.isIso[e]) {
            ElementNodes nodes;
            nodes.type = ElementType::Line3;
            nodes.count = 3;
            for (std::size_t k = 0; k < 3; ++k) {
                nodes.x.at(k) = mesh.nodeCoordinates(ids[k]);
            }
            const std::vector<double> local = nodeValues(element, 3, values);
            const double length = m_beams.length[e];
            const auto points = AxialProfile::gaussPoints(0, 1, length);
            AxialProfile profile;
            profile.count = 2;
            for (std::size_t k = 0; k < 2; ++k) {
                profile.values.at(k) =
                    isoBeamAt(m_beams.iso[e], kind, nodes, local, points.at(k)).forces.n;
            }
            profiles[e] = profile;
            return;
        }
        // The exact beam: its ends, then N along it.
        const std::vector<double> local = nodeValues(element, 2, values);
        const SpanLoads& loads = m_beams.loads[e];
        const bool planar = kind == ElementKind::Beam2D;
        StateBarVector endLoads;
        const StateBarVector ends = exactBeamEnds(beam, kind, loads, x1, x2, local, &endLoads);
        BeamForces start;
        BeamForces end;
        exactEndForces(beam, planar, ends, endLoads, start, end);
        const double length = exactLength(beam, x1, x2);
        if (beam.arcLength > 0.0) {
            // An arc: N turns with the frame (design 008c §5.2).
            const double angle = beam.arcLength * beam.curvature.norm();
            const int parts = std::clamp(
                static_cast<int>(std::ceil(angle / (std::numbers::pi / 16.0))), 1, kMaxAxialParts);
            AxialProfile profile;
            profile.count = 2 * parts;
            for (int part = 0; part < parts; ++part) {
                const auto points = AxialProfile::gaussPoints(part, parts, length);
                for (std::size_t k = 0; k < 2; ++k) {
                    profile.values.at(2 * static_cast<std::size_t>(part) + k) =
                        stateAt(beam, length, loads, planar, ends, points.at(k)).force.x();
                }
            }
            profiles[e] = profile;
            return;
        }
        if (!hasAxialSpanLoad(loads)) {
            profiles[e] = AxialProfile::constant(start.n);
            return;
        }
        // A straight bar with axial span loads: N(s) = N(0) - their integral.
        constexpr int kParts = 4;
        AxialProfile profile;
        profile.count = 2 * kParts;
        for (int part = 0; part < kParts; ++part) {
            const auto points = AxialProfile::gaussPoints(part, kParts, length);
            for (std::size_t k = 0; k < 2; ++k) {
                profile.values.at(2 * static_cast<std::size_t>(part) + k) =
                    start.n - axialLoadBefore(loads, points.at(k));
            }
        }
        profiles[e] = profile;
    };
    tbb::task_arena arena(m_options.threads > 0 ? m_options.threads : tbb::task_arena::automatic);
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, count),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              for (std::size_t e = range.begin(); e != range.end(); ++e) {
                                  profileOf(e);
                              }
                          });
    });
    return profiles;
}

std::optional<BeamResult> StaticProblem::internalMode(ElementIndex element) const
{
    const auto e = static_cast<std::size_t>(element);
    if (!isBeam(m_kindOf[e]) || m_beams.isIso[e] || !m_beams.exact[e]) {
        return std::nullopt;
    }
    const auto ids = mesh().elementNodes(element);
    const Eigen::Vector3d x1 = mesh().nodeCoordinates(ids[0]);
    const Eigen::Vector3d x2 = mesh().nodeCoordinates(ids[1]);
    const BeamData& beam = m_beams.data[e];
    auto mode = exactInternalMode(beam, m_kindOf[e], x1, x2);
    if (!mode) {
        return std::nullopt;
    }
    BeamResult::Data data;
    data.length = exactLength(beam, x1, x2);
    data.axes = beam.axes;
    data.beam = beam;
    data.exact = true;
    data.planar = m_kindOf[e] == ElementKind::Beam2D;
    data.ends = mode->ends;
    data.junctions = mode->junctions;
    // Normalized: the largest displacement along the bar is 1.
    double largest = 0.0;
    {
        const BeamResult probe(data);
        constexpr int kSamples = 200;
        for (int i = 0; i <= kSamples; ++i) {
            largest = std::max(largest, probe.at(data.length * i / kSamples).displacement.norm());
        }
    }
    if (largest > 0.0) {
        data.ends /= largest;
        for (double& value : data.junctions) {
            value /= largest;
        }
    }
    return BeamResult(std::move(data));
}

void StaticProblem::setAxialForces(const std::vector<AxialProfile>& profiles)
{
    for (std::size_t e = 0; e < profiles.size(); ++e) {
        m_beams.data[e].axialForce = profiles[e];
        m_beams.iso[e].beam.axialForce = profiles[e];
        m_bars[e].axialForce = profiles[e].at(0.0, 1.0);
    }
}

Expected<StaticResult> StaticProblem::recover(const Eigen::VectorXd& values, bool loaded) const
{
    const SpanLoads none;
    const std::vector<NodalLoad> noNodalLoads;
    const Model& model = m_model;
    const Mesh& mesh = this->mesh();
    const DofMap& dofs = m_dofs;
    const ValidationTables& tables = m_validation.tables;
    const Beams& beams = m_beams;
    const std::vector<BarData>& bars = m_bars;
    const std::vector<ElementKind>& kindOf = m_kindOf;
    auto forces =
        reactions(mesh, dofs, m_sets, loaded ? m_loads : noNodalLoads, values, m_options.threads);
    if (!forces) {
        return tl::unexpected(forces.error());
    }

    // Results per node and per bar.
    StaticResult result;
    result.mesh = model.mesh;
    const auto nodeCount = static_cast<std::size_t>(mesh.nodeCount());
    result.displacements.assign(nodeCount, Eigen::Vector3d::Zero());
    result.reactions.assign(nodeCount, Eigen::Vector3d::Zero());
    result.rotations.assign(nodeCount, Eigen::Vector3d::Zero());
    result.reactionMoments.assign(nodeCount, Eigen::Vector3d::Zero());
    result.warping.assign(nodeCount, 0.0);
    result.reactionBimoments.assign(nodeCount, 0.0);
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        const auto node = static_cast<std::size_t>(dofs.nodeOf(d));
        if (dofs.typeOf(d) == DofType::Warp) {
            result.warping[node] = values[d];
            if (dofs.isPrescribed(d)) {
                result.reactionBimoments[node] = (*forces)[d];
            }
            continue;
        }
        const auto k = static_cast<Eigen::Index>(dofs.typeOf(d));
        const bool rotation = k >= 3;
        (rotation ? result.rotations : result.displacements)[node](k % 3) = values[d];
        if (dofs.isPrescribed(d)) {
            (rotation ? result.reactionMoments : result.reactions)[node](k % 3) = (*forces)[d];
        }
    }
    const auto elementCount = static_cast<std::size_t>(mesh.elementCount());
    constexpr double kNotABar = std::numeric_limits<double>::quiet_NaN();
    result.axialForce.assign(elementCount, kNotABar);
    result.strain.assign(elementCount, kNotABar);
    result.stress.assign(elementCount, kNotABar);
    result.beams.resize(elementCount);
    // Each element writes only its own entries: in parallel, as the assembly.
    const auto resultOf = [&](std::size_t e) {
        const int owner = tables.owner[e];
        if (owner == kNoAssignment) {
            return;
        }
        const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
        const auto& assignment = model.assignments[static_cast<std::size_t>(owner)];
        if (detail::isBeam(kindOf[e]) && beams.isIso[e]) {
            ElementNodes elementNodes;
            elementNodes.type = ElementType::Line3;
            elementNodes.count = 3;
            for (std::size_t k = 0; k < 3; ++k) {
                elementNodes.x.at(k) = mesh.nodeCoordinates(nodes[k]);
            }
            std::vector<double> nodeValues =
                this->nodeValues(static_cast<ElementIndex>(e), 3, values);
            BeamResult::Data data;
            data.length = beams.length[e];
            data.axes = beams.data[e].axes;
            data.beam = beams.data[e];
            data.planar = kindOf[e] == ElementKind::Beam2D;
            IsoBeamData iso = beams.iso[e];
            if (!loaded) {
                iso.loads = {};
            }
            data.iso = std::make_shared<const IsoBeamData>(std::move(iso));
            data.isoNodes = elementNodes;
            data.isoValues = std::move(nodeValues);
            const BeamResult probe(data);
            data.start = probe.at(0.0).forces;
            data.end = probe.at(data.length).forces;
            const double n = data.start.n;
            result.beams[e].emplace(std::move(data));
            result.axialForce[e] = n;
            result.strain[e] = n / beams.data[e].axial;
            result.stress[e] = n / detail::sectionArea(*model.findSection(assignment.section));
            return;
        }
        if (detail::isBeam(kindOf[e]) && beams.exact[e]) {
            // The element's nodal values, in the order of its dofs.
            const std::vector<double> nodeValues =
                this->nodeValues(static_cast<ElementIndex>(e), 2, values);
            const Eigen::Vector3d x1 = mesh.nodeCoordinates(nodes[0]);
            const Eigen::Vector3d x2 = mesh.nodeCoordinates(nodes[1]);
            BeamResult::Data data;
            data.length = exactLength(beams.data[e], x1, x2);
            data.axes = beams.data[e].axes;
            data.beam = beams.data[e];
            data.loads = loaded ? beams.loads[e] : none;
            data.exact = true;
            data.planar = kindOf[e] == ElementKind::Beam2D;
            StateBarVector endLoads;
            data.ends =
                exactBeamEnds(beams.data[e], kindOf[e], data.loads, x1, x2, nodeValues, &endLoads);
            exactEndForces(beams.data[e], data.planar, data.ends, endLoads, data.start, data.end);
            const double n = data.start.n;
            result.beams[e].emplace(std::move(data));
            result.axialForce[e] = n;
            result.strain[e] = n / beams.data[e].axial;
            result.stress[e] = n / detail::sectionArea(*model.findSection(assignment.section));
            return;
        }
        if (detail::isBeam(kindOf[e])) {
            const auto a = static_cast<std::size_t>(nodes[0]);
            const auto b = static_cast<std::size_t>(nodes[1]);
            BeamVector nodeValues;
            nodeValues << result.displacements[a], result.rotations[a], result.displacements[b],
                result.rotations[b];
            const BeamEnds ends = beamEnds(beams.data[e], kindOf[e], mesh.nodeCoordinates(nodes[0]),
                                           mesh.nodeCoordinates(nodes[1]), nodeValues);
            BeamResult::Data data;
            data.length = beams.length[e];
            data.axes = beams.data[e].axes;
            data.beam = beams.data[e];
            data.loads = beams.loads[e];
            data.displacements = ends.displacements;
            endForces(beams.data[e], ends.forces, data.start, data.end);
            const double n = data.start.n;
            result.beams[e].emplace(std::move(data));
            result.axialForce[e] = n;
            result.strain[e] = n / beams.data[e].axial;
            result.stress[e] = n / detail::sectionArea(*model.findSection(assignment.section));
            return;
        }
        const double n =
            axialForce(bars[e], mesh.nodeCoordinates(nodes[0]), mesh.nodeCoordinates(nodes[1]),
                       result.displacements[static_cast<std::size_t>(nodes[0])],
                       result.displacements[static_cast<std::size_t>(nodes[1])]);
        result.axialForce[e] = n;
        result.strain[e] = n / bars[e].axialStiffness;
        result.stress[e] = n / detail::sectionArea(*model.findSection(assignment.section));
    };
    tbb::task_arena arena(m_options.threads > 0 ? m_options.threads : tbb::task_arena::automatic);
    arena.execute([&] {
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, elementCount),
                          [&](const tbb::blocked_range<std::size_t>& range) {
                              for (std::size_t e = range.begin(); e != range.end(); ++e) {
                                  resultOf(e);
                              }
                          });
    });
    return result;
}

} // namespace detail

Expected<StaticResult> runLinearStatic(const Model& model, const StaticOptions& options)
{
    auto prepared = detail::StaticProblem::prepare(model, options, false);
    if (!prepared) {
        return tl::unexpected(prepared.error());
    }
    const detail::StaticProblem& problem = **prepared;
    auto system = problem.assemble();
    if (!system) {
        return tl::unexpected(system.error());
    }
    if (auto stable = problem.checkDiagonal(system->matrix); !stable) {
        return tl::unexpected(stable.error());
    }
    SparseCholesky solver;
    if (auto factorized = solver.factorize(system->matrix); !factorized) {
        const auto eq = solver.singularEquation();
        if (!eq) {
            return tl::unexpected(factorized.error());
        }
        return makeError("The structure is a mechanism: " + problem.equationLabel(*eq) +
                         " can move without resistance (a support may be missing).");
    }
    auto free = solver.solve(system->rhs);
    if (!free) {
        return tl::unexpected(free.error());
    }
    return problem.recover(problem.valuesOf(*free));
}

std::vector<ModelIssue> validate(const Model& model)
{
    if (!model.mesh) {
        return {{"The model has no mesh."}};
    }
    return detail::validateModel(model).issues;
}

} // namespace fem::core
