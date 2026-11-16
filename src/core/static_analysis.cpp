#include <fem/core/assembler.hpp>
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

constexpr int kNoAssignment = -1;
constexpr double kPlaneTolerance = 1e-9; // of the bounding box diagonal
constexpr std::size_t kListedElements = 10;
constexpr double kNoStiffness = 1e-12; // of the largest diagonal entry

std::string quoted(std::string_view name)
{
    return "\"" + std::string(name) + "\"";
}

std::string itemLabel(std::string_view kind, std::size_t position)
{
    return std::string(kind) + " " + std::to_string(position + 1);
}

DofMask dofsOf(ElementKind kind)
{
    const DofMask plane = mask(DofType::U) | mask(DofType::V);
    return kind == ElementKind::Truss2D ? plane : static_cast<DofMask>(plane | mask(DofType::W));
}

// What validation learns about the model, reused by the analysis.
struct Tables
{
    std::vector<int> owner;        // per element: assignment, or kNoAssignment
    std::vector<DofMask> nodeDofs; // per node, from the valid bars
};

class Validator
{
public:
    explicit Validator(const Model& model)
        : m_model(model)
        , m_mesh(*model.mesh)
    {}

    Tables run()
    {
        m_tables.owner.assign(static_cast<std::size_t>(m_mesh.elementCount()), kNoAssignment);
        m_tables.nodeDofs.assign(static_cast<std::size_t>(m_mesh.nodeCount()), 0);
        checkAssignments();
        checkUnassigned();
        checkBars();
        checkThermal();
        checkInitialStrains();
        checkRestraints();
        checkForces();
        return std::move(m_tables);
    }

    std::vector<ModelIssue> issues() { return std::move(m_issues); }

private:
    void add(std::string message) { m_issues.push_back({std::move(message)}); }

    [[nodiscard]] std::string elementName(ElementIndex element) const
    {
        return "element " + std::to_string(m_mesh.elementId(element));
    }

    [[nodiscard]] std::string nodeName(NodeIndex node) const
    {
        return "node " + std::to_string(m_mesh.nodeId(node));
    }

    void checkMaterial(const Material& material)
    {
        if (!m_checkedMaterials.insert(material.name).second) {
            return;
        }
        if (!(material.youngsModulus.value_or(0.0) > 0.0)) {
            add("Material " + quoted(material.name) +
                ": the Young's modulus must be given and positive.");
        }
        if (m_model.gravity && !(material.density.value_or(0.0) > 0.0)) {
            add("Material " + quoted(material.name) +
                ": the density must be given and positive for the self-weight.");
        }
    }

    void checkSection(const Section& section)
    {
        if (!m_checkedSections.insert(section.name).second) {
            return;
        }
        if (!(section.area.value_or(0.0) > 0.0)) {
            add("Section " + quoted(section.name) + ": the area must be given and positive.");
        }
    }

    void checkAssignments()
    {
        const auto& assignments = m_model.assignments;
        for (std::size_t i = 0; i < assignments.size(); ++i) {
            const Assignment& assignment = assignments[i];
            const std::string label = itemLabel("Assignment", i);
            if (const Material* material = m_model.findMaterial(assignment.material)) {
                checkMaterial(*material);
            } else {
                add(label + ": material " + quoted(assignment.material) + " does not exist.");
            }
            if (const Section* section = m_model.findSection(assignment.section)) {
                checkSection(*section);
            } else {
                add(label + ": section " + quoted(assignment.section) + " does not exist.");
            }
            const auto elements = resolve(m_mesh, assignment.elements);
            if (!elements) {
                add(label + ": " + elements.error().message);
                continue;
            }
            for (const ElementIndex element : *elements) {
                int& owner = m_tables.owner[static_cast<std::size_t>(element)];
                if (owner != kNoAssignment) {
                    add("The " + elementName(element) + " has two assignments (" +
                        std::to_string(owner + 1) + " and " + std::to_string(i + 1) + ").");
                    continue;
                }
                owner = static_cast<int>(i);
            }
        }
    }

    void checkUnassigned()
    {
        std::vector<ElementIndex> missing;
        bool anyAssigned = false;
        for (ElementIndex element = 0; element < m_mesh.elementCount(); ++element) {
            if (m_tables.owner[static_cast<std::size_t>(element)] != kNoAssignment) {
                anyAssigned = true;
            } else if (elementTypeInfo(m_mesh.elementType(element)).dimension > 0) {
                missing.push_back(element);
            }
        }
        if (!anyAssigned) {
            add("No element has an assignment: the model is empty.");
            return;
        }
        if (missing.empty()) {
            return;
        }
        std::string list;
        for (std::size_t k = 0; k < std::min(missing.size(), kListedElements); ++k) {
            list += (k == 0 ? "" : ", ") + std::to_string(m_mesh.elementId(missing[k]));
        }
        if (missing.size() > kListedElements) {
            list += " and " + std::to_string(missing.size() - kListedElements) + " more";
        }
        add("Elements without an assignment: " + list + ".");
    }

    // REQ-007-09, 10: bars are 2-node lines of nonzero length; 2D trusses lie
    // in the XY plane. The dofs of the nodes come from the valid bars.
    void checkBars()
    {
        const BoundingBox box = m_mesh.boundingBox();
        const double tolerance = kPlaneTolerance * (box.max - box.min).norm();
        for (ElementIndex element = 0; element < m_mesh.elementCount(); ++element) {
            const int owner = m_tables.owner[static_cast<std::size_t>(element)];
            if (owner == kNoAssignment) {
                continue;
            }
            const ElementType type = m_mesh.elementType(element);
            if (type != ElementType::Line2) {
                add("The " + elementName(element) + " is a " +
                    std::string(elementTypeInfo(type).name) +
                    ", not a 2-node line, and cannot be a truss bar.");
                continue;
            }
            const auto nodes = m_mesh.elementNodes(element);
            const Eigen::Vector3d a = m_mesh.nodeCoordinates(nodes[0]);
            const Eigen::Vector3d b = m_mesh.nodeCoordinates(nodes[1]);
            if (!((b - a).norm() > 0.0)) {
                add("The " + elementName(element) + " has zero length.");
                continue;
            }
            const ElementKind kind = m_model.assignments[static_cast<std::size_t>(owner)].kind;
            if (kind == ElementKind::Truss2D &&
                (std::abs(a.z()) > tolerance || std::abs(b.z()) > tolerance)) {
                add("The " + elementName(element) +
                    " is a 2D truss bar but does not lie in the XY plane.");
                continue;
            }
            for (const NodeIndex node : nodes) {
                m_tables.nodeDofs[static_cast<std::size_t>(node)] |= dofsOf(kind);
            }
        }
    }

    void checkThermal()
    {
        const auto& changes = m_model.temperatureChanges;
        for (std::size_t i = 0; i < changes.size(); ++i) {
            const auto elements = resolve(m_mesh, changes[i].elements);
            if (!elements) {
                add(itemLabel("Temperature change", i) + ": " + elements.error().message);
                continue;
            }
            for (const ElementIndex element : *elements) {
                const int owner = m_tables.owner[static_cast<std::size_t>(element)];
                if (owner == kNoAssignment) {
                    continue;
                }
                const auto& name = m_model.assignments[static_cast<std::size_t>(owner)].material;
                const Material* material = m_model.findMaterial(name);
                if (material != nullptr && !material->thermalExpansion &&
                    m_checkedThermal.insert(name).second) {
                    add("Material " + quoted(name) +
                        ": the thermal expansion must be given for the temperature change.");
                }
            }
        }
    }

    void checkInitialStrains()
    {
        const auto& strains = m_model.initialStrains;
        for (std::size_t i = 0; i < strains.size(); ++i) {
            if (const auto elements = resolve(m_mesh, strains[i].elements); !elements) {
                add(itemLabel("Initial strain", i) + ": " + elements.error().message);
            }
        }
    }

    // A node needs a bar, and the bar the direction.
    void checkNode(const std::string& label, NodeIndex node, DofType dof)
    {
        const DofMask dofs = m_tables.nodeDofs[static_cast<std::size_t>(node)];
        if (dofs == 0) {
            add(label + ": the " + nodeName(node) + " is not connected to any bar.");
        } else if ((dofs & mask(dof)) == 0) {
            add(label + ": the " + nodeName(node) + " has no dof " + std::string(dofName(dof)) +
                " (it only belongs to 2D trusses).");
        }
    }

    void checkRestraints()
    {
        const auto& restraints = m_model.restraints;
        for (std::size_t i = 0; i < restraints.size(); ++i) {
            const std::string label = itemLabel("Restraint", i);
            const DofType dof = restraints[i].dof;
            if (dof != DofType::U && dof != DofType::V && dof != DofType::W) {
                add(label + ": a truss model only has the dofs u, v and w.");
                continue;
            }
            const auto nodes = resolve(m_mesh, restraints[i].nodes);
            if (!nodes) {
                add(label + ": " + nodes.error().message);
                continue;
            }
            for (const NodeIndex node : *nodes) {
                checkNode(label, node, dof);
            }
        }
    }

    void checkForces()
    {
        const auto& forces = m_model.nodalForces;
        for (std::size_t i = 0; i < forces.size(); ++i) {
            const std::string label = itemLabel("Nodal force", i);
            const auto nodes = resolve(m_mesh, forces[i].nodes);
            if (!nodes) {
                add(label + ": " + nodes.error().message);
                continue;
            }
            // A force without z only needs u and v.
            const DofType needed = forces[i].force.z() != 0.0 ? DofType::W : DofType::U;
            for (const NodeIndex node : *nodes) {
                checkNode(label, node, needed);
            }
        }
    }

    const Model& m_model;
    const Mesh& m_mesh;
    Tables m_tables;
    std::vector<ModelIssue> m_issues;
    std::set<std::string, std::less<>> m_checkedMaterials;
    std::set<std::string, std::less<>> m_checkedSections;
    std::set<std::string, std::less<>> m_checkedThermal;
};

// Per element: EA, imposed strain and weight; zero for elements that are not
// bars (design 007 §5.1).
std::vector<BarData> barData(const Model& model, const Tables& tables)
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
        const double area = model.findSection(assignment.section)->area.value_or(0.0);
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
    return makeError("The structure is unstable: no bar gives stiffness to" + list);
}

} // namespace

Expected<StaticResult> runLinearStatic(const Model& model, const StaticOptions& options)
{
    if (!model.mesh) {
        return makeError(issueList({{"The model has no mesh."}}));
    }
    Validator validator(model);
    const Tables tables = validator.run();
    if (const auto issues = validator.issues(); !issues.empty()) {
        return makeError(issueList(issues));
    }
    const Mesh& mesh = *model.mesh;

    // Bars and their sets, one per kind.
    const std::vector<BarData> bars = barData(model, tables);
    std::vector<ElementIndex> planar;
    std::vector<ElementIndex> spatial;
    for (ElementIndex element = 0; element < mesh.elementCount(); ++element) {
        const int owner = tables.owner[static_cast<std::size_t>(element)];
        if (owner != kNoAssignment) {
            const auto kind = model.assignments[static_cast<std::size_t>(owner)].kind;
            (kind == ElementKind::Truss2D ? planar : spatial).push_back(element);
        }
    }
    const TrussElement planarTruss(ElementKind::Truss2D, bars);
    const TrussElement spatialTruss(ElementKind::Truss3D, bars);
    std::vector<ElementSet> sets;
    if (!planar.empty()) {
        sets.push_back({.elements = planar, .formulation = &planarTruss});
    }
    if (!spatial.empty()) {
        sets.push_back({.elements = spatial, .formulation = &spatialTruss});
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
    constexpr std::array<DofType, 3> kTranslations = {DofType::U, DofType::V, DofType::W};
    for (const NodalForce& force : model.nodalForces) {
        for (const NodeIndex node : resolve(mesh, force.nodes).value_or(std::vector<NodeIndex>{})) {
            for (std::size_t k = 0; k < kTranslations.size(); ++k) {
                const double value = force.force(static_cast<Eigen::Index>(k));
                if (value != 0.0 && dofs.find(node, kTranslations.at(k))) {
                    loads.push_back({.node = node, .type = kTranslations.at(k), .value = value});
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
    for (DofIndex d = 0; d < dofs.size(); ++d) {
        const auto node = static_cast<std::size_t>(dofs.nodeOf(d));
        const auto k = static_cast<Eigen::Index>(dofs.typeOf(d));
        result.displacements[node](k) = values[d];
        if (dofs.isPrescribed(d)) {
            result.reactions[node](k) = (*forces)[d];
        }
    }
    const auto elementCount = static_cast<std::size_t>(mesh.elementCount());
    constexpr double kNotABar = std::numeric_limits<double>::quiet_NaN();
    result.axialForce.assign(elementCount, kNotABar);
    result.strain.assign(elementCount, kNotABar);
    result.stress.assign(elementCount, kNotABar);
    for (std::size_t e = 0; e < elementCount; ++e) {
        const int owner = tables.owner[e];
        if (owner == kNoAssignment) {
            continue;
        }
        const auto nodes = mesh.elementNodes(static_cast<ElementIndex>(e));
        const double n =
            axialForce(bars[e], mesh.nodeCoordinates(nodes[0]), mesh.nodeCoordinates(nodes[1]),
                       result.displacements[static_cast<std::size_t>(nodes[0])],
                       result.displacements[static_cast<std::size_t>(nodes[1])]);
        const auto& assignment = model.assignments[static_cast<std::size_t>(owner)];
        result.axialForce[e] = n;
        result.strain[e] = n / bars[e].axialStiffness;
        result.stress[e] = n / model.findSection(assignment.section)->area.value_or(1.0);
    }
    return result;
}

std::vector<ModelIssue> validate(const Model& model)
{
    if (!model.mesh) {
        return {{"The model has no mesh."}};
    }
    Validator validator(model);
    (void)validator.run();
    return validator.issues();
}

} // namespace fem::core
