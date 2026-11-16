#include "detail/model_validation.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/element_type.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/core/truss_element.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// The beams of the model, per element (design 008 §5.6): data, span loads
// and flexible length; default values for elements that are not beams.
struct Beams
{
    std::vector<BeamData> data;
    std::vector<SpanLoads> loads;
    std::vector<double> length;
};

Beams beamData(const Model& model, const detail::ValidationTables& tables)
{
    const Mesh& mesh = *model.mesh;
    const auto count = static_cast<std::size_t>(mesh.elementCount());
    Beams beams;
    beams.data.resize(count);
    beams.loads.resize(count);
    beams.length.assign(count, 0.0);
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
        beams.length[e] = setup->length;
        frames[e] = setup->frame;
        materials[e] = model.findMaterial(assignment.material);
        if (model.gravity) {
            const double area = detail::sectionArea(*model.findSection(assignment.section));
            const Eigen::Vector3d weight =
                materials[e]->density.value_or(0.0) * area * *model.gravity;
            addDistributed(beams.loads[e], frames[e], Axes::Global, weight, weight, 0.0,
                           setup->length);
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
            beams.loads[e].imposedStrain +=
                materials[e]->thermalExpansion.value_or(0.0) * change.deltaT;
        });
    }
    for (const InitialStrain& initial : model.initialStrains) {
        forBeams(initial.elements,
                 [&](std::size_t e) { beams.loads[e].imposedStrain += initial.strain; });
    }
    for (const TemperatureGradient& gradient : model.temperatureGradients) {
        forBeams(gradient.elements, [&](std::size_t e) {
            addGradient(beams.loads[e], frames[e], materials[e]->thermalExpansion.value_or(0.0),
                        gradient.alongY, gradient.alongZ);
        });
    }
    for (const DistributedLoad& load : model.distributedLoads) {
        forBeams(load.elements, [&](std::size_t e) {
            addDistributed(beams.loads[e], frames[e], load.axes, load.startValue, load.endValue,
                           positionOn(load.from, beams.length[e]),
                           positionOn(load.to, beams.length[e]));
        });
    }
    for (const ConcentratedLoad& load : model.concentratedLoads) {
        forBeams(load.elements, [&](std::size_t e) {
            addConcentrated(beams.loads[e], frames[e], load.axes, load.force, load.moment,
                            positionOn(load.at, beams.length[e]));
        });
    }
    for (std::size_t e = 0; e < count; ++e) {
        if (isBeam[e]) {
            beams.data[e].fixedEndLoads =
                equivalentLoads(beams.data[e], beams.length[e], beams.loads[e]);
        }
    }
    return beams;
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

// REQ-007-15: free dofs without stiffness, all of them.
Expected<void> checkDiagonal(const Mesh& mesh, const DofMap& dofs,
                             const std::vector<DofIndex>& dofOfEquation, const SparseMatrix& matrix)
{
    const Eigen::VectorXd diagonal = matrix.diagonal();
    if (diagonal.size() == 0) {
        return {};
    }
    const double largest = diagonal.cwiseAbs().maxCoeff();
    std::string list;
    for (Eigen::Index eq = 0; eq < diagonal.size(); ++eq) {
        if (!(diagonal[eq] > kNoStiffness * largest)) {
            list += "\n- " + dofLabel(mesh, dofs, dofOfEquation[static_cast<std::size_t>(eq)]);
        }
    }
    if (list.empty()) {
        return {};
    }
    if (list.find(" in r") != std::string::npos) {
        // REQ-008-25: often every beam at the node releases that moment.
        list += "\n(A rotation without stiffness: restrain it, or keep one beam at the node "
                "connected.)";
    }
    return makeError("The structure is unstable: no bar gives stiffness to" + list);
}

} // namespace

Expected<StaticResult> runLinearStatic(const Model& model, const StaticOptions& options)
{
    if (!model.mesh) {
        return makeError(issueList({{"The model has no mesh."}}));
    }
    const detail::Validation validation = detail::validateModel(model);
    if (!validation.issues.empty()) {
        return makeError(issueList(validation.issues));
    }
    const detail::ValidationTables& tables = validation.tables;
    const Mesh& mesh = *model.mesh;

    // Bars and beams, and their sets, one per kind.
    const std::vector<BarData> bars = barData(model, tables);
    const Beams beams = beamData(model, tables);
    constexpr std::size_t kKinds = 4; // in ElementKind order
    std::array<std::vector<ElementIndex>, kKinds> members;
    std::vector<ElementKind> kindOf(static_cast<std::size_t>(mesh.elementCount()),
                                    ElementKind::Truss3D);
    for (ElementIndex element = 0; element < mesh.elementCount(); ++element) {
        const int owner = tables.owner[static_cast<std::size_t>(element)];
        if (owner != kNoAssignment) {
            const auto kind = model.assignments[static_cast<std::size_t>(owner)].kind;
            kindOf[static_cast<std::size_t>(element)] = kind;
            members.at(static_cast<std::size_t>(kind)).push_back(element);
        }
    }
    const TrussElement planarTruss(ElementKind::Truss2D, bars);
    const TrussElement spatialTruss(ElementKind::Truss3D, bars);
    const BeamElement planarBeam(ElementKind::Beam2D, beams.data);
    const BeamElement spatialBeam(ElementKind::Beam3D, beams.data);
    const std::array<const ElementFormulation*, kKinds> formulations = {&planarTruss, &spatialTruss,
                                                                        &planarBeam, &spatialBeam};
    std::vector<ElementSet> sets;
    for (std::size_t k = 0; k < kKinds; ++k) {
        if (!members.at(k).empty()) {
            sets.push_back({.elements = members.at(k), .formulation = formulations.at(k)});
        }
    }

    // Dofs, supports and loads; validation guarantees every dof exists.
    DofMap dofs = DofMap::build(mesh, sets);
    for (const Restraint& restraint : model.restraints) {
        for (const NodeIndex node :
             resolve(mesh, restraint.nodes).value_or(std::vector<NodeIndex>{})) {
            if (auto prescribed = dofs.prescribe(node, restraint.dof, restraint.value);
                !prescribed) {
                return tl::unexpected(prescribed.error());
            }
        }
    }
    std::vector<NodalLoad> loads;
    constexpr std::array<DofType, 6> kNodeDofs = {DofType::U,  DofType::V,  DofType::W,
                                                  DofType::RX, DofType::RY, DofType::RZ};
    for (const NodalForce& force : model.nodalForces) {
        Eigen::Matrix<double, 6, 1> values;
        values << force.force, force.moment;
        for (const NodeIndex node : resolve(mesh, force.nodes).value_or(std::vector<NodeIndex>{})) {
            for (std::size_t k = 0; k < kNodeDofs.size(); ++k) {
                const double value = values(static_cast<Eigen::Index>(k));
                if (value != 0.0 && dofs.find(node, kNodeDofs.at(k))) {
                    loads.push_back({.node = node, .type = kNodeDofs.at(k), .value = value});
                }
            }
        }
    }

    auto system = assemble(mesh, dofs, sets, loads, options.threads);
    if (!system) {
        return tl::unexpected(system.error());
    }
    std::vector<DofIndex> dofOfEquation(static_cast<std::size_t>(dofs.freeCount()));
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        if (const std::int32_t eq = dofs.equation(d); eq >= 0) {
            dofOfEquation[static_cast<std::size_t>(eq)] = d;
        }
    }
    if (auto stable = checkDiagonal(mesh, dofs, dofOfEquation, system->matrix); !stable) {
        return tl::unexpected(stable.error());
    }
    SparseCholesky solver;
    if (auto factorized = solver.factorize(system->matrix); !factorized) {
        const auto eq = solver.singularEquation();
        if (!eq) {
            return tl::unexpected(factorized.error());
        }
        return makeError("The structure is a mechanism: " +
                         dofLabel(mesh, dofs, dofOfEquation[static_cast<std::size_t>(*eq)]) +
                         " can move without resistance (a support may be missing).");
    }
    auto free = solver.solve(system->rhs);
    if (!free) {
        return tl::unexpected(free.error());
    }
    Eigen::VectorXd values(dofs.size());
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        const std::int32_t eq = dofs.equation(d);
        values[d] = eq < 0 ? dofs.prescribedValue(d) : (*free)[eq];
    }
    auto forces = reactions(mesh, dofs, sets, loads, values, options.threads);
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
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        const auto node = static_cast<std::size_t>(dofs.nodeOf(d));
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
    for (std::size_t e = 0; e < elementCount; ++e) {
        const int owner = tables.owner[e];
        if (owner == kNoAssignment) {
            continue;
        }
        const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
        const auto& assignment = model.assignments[static_cast<std::size_t>(owner)];
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
            continue;
        }
        const double n =
            axialForce(bars[e], mesh.nodeCoordinates(nodes[0]), mesh.nodeCoordinates(nodes[1]),
                       result.displacements[static_cast<std::size_t>(nodes[0])],
                       result.displacements[static_cast<std::size_t>(nodes[1])]);
        result.axialForce[e] = n;
        result.strain[e] = n / bars[e].axialStiffness;
        result.stress[e] = n / detail::sectionArea(*model.findSection(assignment.section));
    }
    return result;
}

std::vector<ModelIssue> validate(const Model& model)
{
    if (!model.mesh) {
        return {{"The model has no mesh."}};
    }
    return detail::validateModel(model).issues;
}

} // namespace fem::core
