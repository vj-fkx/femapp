#include "detail/model_validation.hpp"

#include <fem/core/element_type.hpp>
#include <fem/core/mesh.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fem::core::detail {

namespace {

constexpr double kPlaneTolerance = 1e-9;     // of the bounding box diagonal
constexpr double kFlexibleTolerance = 1e-9;  // of the length between the nodes
constexpr double kPositionTolerance = 1e-9;  // of the flexible length
constexpr double kSymmetryTolerance = 1e-12; // shear centre off y, of sqrt(A)
constexpr std::size_t kListedElements = 10;

std::string itemLabel(std::string_view kind, std::size_t position)
{
    return std::string(kind) + " " + std::to_string(position + 1);
}

bool needsShearModulus(const Assignment& assignment)
{
    return assignment.kind == ElementKind::Beam3D ||
           (isBeam(assignment.kind) && assignment.theory == BeamTheory::Timoshenko);
}

bool hasGenericProperties(const Section& section)
{
    return section.area || section.inertiaY || section.inertiaZ || section.productOfInertia ||
           section.torsionConstant || section.shearCentre || section.shearAreaY ||
           section.shearAreaZ;
}

class Validator
{
public:
    explicit Validator(const Model& model)
        : m_model(model)
        , m_mesh(*model.mesh)
    {}

    ValidationTables run()
    {
        const auto elements = static_cast<std::size_t>(m_mesh.elementCount());
        m_tables.owner.assign(elements, kNoAssignment);
        m_tables.nodeDofs.assign(static_cast<std::size_t>(m_mesh.nodeCount()), 0);
        m_tables.offsetOf.assign(elements, kNoItem);
        m_tables.foundationOf.assign(elements, kNoItem);
        m_tables.connectionOf.assign(elements, {kNoItem, kNoItem});
        m_flexible.assign(elements, -1.0);
        m_valid.assign(m_model.assignments.size(), false);
        for (const Assignment& assignment : m_model.assignments) {
            m_tables.hasBeams = m_tables.hasBeams || isBeam(assignment.kind);
        }
        checkAssignments();
        checkUnassigned();
        checkConnections();
        checkOffsets();
        checkFoundations();
        checkBars();
        checkThermal();
        checkInitialStrains();
        checkGradients();
        checkDistributedLoads();
        checkConcentratedLoads();
        checkRestraints();
        checkForces();
        return std::move(m_tables);
    }

    std::vector<ModelIssue> issues() { return std::move(m_issues); }

private:
    void add(std::string message) { m_issues.push_back({std::move(message)}); }

    // For the problems of shared items (materials, sections), said once.
    void addOnce(std::string message)
    {
        if (m_said.insert(message).second) {
            add(std::move(message));
        }
    }

    [[nodiscard]] std::string elementName(ElementIndex element) const
    {
        return "element " + std::to_string(m_mesh.elementId(element));
    }

    [[nodiscard]] std::string nodeName(NodeIndex node) const
    {
        return "node " + std::to_string(m_mesh.nodeId(node));
    }

    [[nodiscard]] int ownerOf(ElementIndex element) const
    {
        return m_tables.owner[static_cast<std::size_t>(element)];
    }

    [[nodiscard]] const Assignment* assignmentOf(ElementIndex element) const
    {
        const int owner = ownerOf(element);
        return owner == kNoAssignment ? nullptr
                                      : &m_model.assignments[static_cast<std::size_t>(owner)];
    }

    // Material problems; true when it can be used by the assignment.
    bool checkMaterial(const Material& material, const Assignment& assignment)
    {
        const std::string name = "Material " + quoted(material.name);
        bool valid = true;
        if (!(material.youngsModulus.value_or(0.0) > 0.0)) {
            addOnce(name + ": the Young's modulus must be given and positive.");
            valid = false;
        }
        if (m_model.gravity && !(material.density.value_or(0.0) > 0.0)) {
            addOnce(name + ": the density must be given and positive for the self-weight.");
        }
        if (needsShearModulus(assignment)) {
            const double nu = material.poissonsRatio.value_or(1.0);
            if (!(nu > -1.0 && nu < 0.5)) {
                addOnce(name +
                        ": the Poisson's ratio must be given and between -1 and 0.5 for beams.");
                valid = false;
            }
        }
        return valid;
    }

    // Section problems for the use of the assignment; true when usable.
    bool checkSection(const Section& section, const Assignment& assignment)
    {
        const std::string name = "Section " + quoted(section.name);
        if (section.shape && hasGenericProperties(section)) {
            addOnce(name + ": give either a shape or its properties, not both.");
            return false;
        }
        const auto properties = sectionProperties(section);
        if (!properties) {
            addOnce(properties.error().message);
            return false;
        }
        if (!section.shape && !(section.area.value_or(0.0) > 0.0)) {
            addOnce(name + ": the area must be given and positive.");
            return false;
        }
        if (!isBeam(assignment.kind)) {
            return true;
        }
        bool valid = true;
        if (!section.shape) {
            const bool space = assignment.kind == ElementKind::Beam3D;
            if (!(section.inertiaZ.value_or(0.0) > 0.0)) {
                addOnce(name + ": the inertia Iz must be given and positive for beams.");
                valid = false;
            }
            if (space && !(section.inertiaY.value_or(0.0) > 0.0)) {
                addOnce(name + ": the inertia Iy must be given and positive for 3D beams.");
                valid = false;
            }
            if (space && !(section.torsionConstant.value_or(0.0) > 0.0)) {
                addOnce(name + ": the torsion constant J must be given and positive for 3D beams.");
                valid = false;
            }
            const double iy = section.inertiaY.value_or(0.0);
            const double iz = section.inertiaZ.value_or(0.0);
            const double iyz = section.productOfInertia.value_or(0.0);
            if (space && iy > 0.0 && iz > 0.0 && !(iy * iz - iyz * iyz > 0.0)) {
                addOnce(name + ": Iy Iz - Iyz^2 must be positive.");
                valid = false;
            }
        }
        if (assignment.theory == BeamTheory::Timoshenko &&
            !(properties->shearAreaY.value_or(0.0) > 0.0 &&
              properties->shearAreaZ.value_or(0.0) > 0.0)) {
            addOnce(name + ": the shear areas Asy and Asz must be given and positive for "
                           "Timoshenko beams.");
            valid = false;
        }
        if (assignment.kind == ElementKind::Beam2D &&
            (properties->principalAngle != 0.0 ||
             std::abs(properties->shearCentre.y()) >
                 kSymmetryTolerance * std::sqrt(properties->area))) {
            addOnce(name + ": a 2D beam needs a section symmetric about its local y axis.");
            valid = false;
        }
        return valid;
    }

    void checkAssignments()
    {
        const auto& assignments = m_model.assignments;
        for (std::size_t i = 0; i < assignments.size(); ++i) {
            const Assignment& assignment = assignments[i];
            const std::string label = itemLabel("Assignment", i);
            bool valid = true;
            if (const Material* material = m_model.findMaterial(assignment.material)) {
                valid = checkMaterial(*material, assignment) && valid;
            } else {
                add(label + ": material " + quoted(assignment.material) + " does not exist.");
                valid = false;
            }
            if (const Section* section = m_model.findSection(assignment.section)) {
                valid = checkSection(*section, assignment) && valid;
            } else {
                add(label + ": section " + quoted(assignment.section) + " does not exist.");
                valid = false;
            }
            m_valid[i] = valid;
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
            if (ownerOf(element) != kNoAssignment) {
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

    // The elements of a beam-only item that are beams; the others are a
    // problem. Unassigned elements are reported elsewhere.
    std::vector<ElementIndex> beamsOf(const std::string& label, const ElementTarget& target)
    {
        const auto elements = resolve(m_mesh, target);
        if (!elements) {
            add(label + ": " + elements.error().message);
            return {};
        }
        std::vector<ElementIndex> beams;
        for (const ElementIndex element : *elements) {
            const Assignment* assignment = assignmentOf(element);
            if (assignment == nullptr) {
                continue;
            }
            if (!isBeam(assignment->kind)) {
                add(label + ": the " + elementName(element) + " is not a beam.");
                continue;
            }
            beams.push_back(element);
        }
        return beams;
    }

    // Records `item` in `slot`, or reports the second one.
    void claim(int& slot, std::size_t item, ElementIndex element, std::string_view what)
    {
        if (slot != kNoItem) {
            add("The " + elementName(element) + " has two " + std::string(what) + " (" +
                std::to_string(slot + 1) + " and " + std::to_string(item + 1) + ").");
            return;
        }
        slot = static_cast<int>(item);
    }

    // REQ-008-11, 12
    void checkConnections()
    {
        const auto& connections = m_model.endConnections;
        for (std::size_t i = 0; i < connections.size(); ++i) {
            const std::string label = itemLabel("End connection", i);
            const EndConnection& connection = connections[i];
            if (std::any_of(connection.stiffness.begin(), connection.stiffness.end(),
                            [](const std::optional<double>& k) { return k && !(*k >= 0.0); })) {
                add(label + ": the stiffness must not be negative.");
            }
            for (const ElementIndex element : beamsOf(label, connection.elements)) {
                auto& slots = m_tables.connectionOf[static_cast<std::size_t>(element)];
                if (connection.end != BarEnd::End) {
                    claim(slots[0], i, element, "end connections at its start");
                }
                if (connection.end != BarEnd::Start) {
                    claim(slots[1], i, element, "end connections at its end");
                }
            }
        }
    }

    // REQ-008-14
    void checkOffsets()
    {
        const auto& offsets = m_model.rigidOffsets;
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            for (const ElementIndex element :
                 beamsOf(itemLabel("Rigid offset", i), offsets[i].elements)) {
                claim(m_tables.offsetOf[static_cast<std::size_t>(element)], i, element,
                      "rigid offsets");
            }
        }
    }

    // REQ-008-15
    void checkFoundations()
    {
        const auto& foundations = m_model.foundations;
        for (std::size_t i = 0; i < foundations.size(); ++i) {
            const std::string label = itemLabel("Elastic foundation", i);
            const ElasticFoundation& foundation = foundations[i];
            if (!(foundation.translational.minCoeff() >= 0.0 && foundation.torsional >= 0.0)) {
                add(label + ": the stiffness must not be negative.");
            }
            for (const ElementIndex element : beamsOf(label, foundation.elements)) {
                claim(m_tables.foundationOf[static_cast<std::size_t>(element)], i, element,
                      "elastic foundations");
            }
        }
    }

    // REQ-007-09, 10, 008-06, 10, 13, 14: bars are 2-node lines of nonzero
    // length; 2D bars lie in the XY plane; beams have axes, a flexible
    // length and stable connections. The dofs of the nodes come from the
    // valid bars.
    void checkBars()
    {
        const BoundingBox box = m_mesh.boundingBox();
        const double tolerance = kPlaneTolerance * (box.max - box.min).norm();
        for (ElementIndex element = 0; element < m_mesh.elementCount(); ++element) {
            const Assignment* assignment = assignmentOf(element);
            if (assignment == nullptr) {
                continue;
            }
            const ElementKind kind = assignment->kind;
            const bool beam = isBeam(kind);
            const ElementType type = m_mesh.elementType(element);
            if (type != ElementType::Line2) {
                add("The " + elementName(element) + " is a " +
                    std::string(elementTypeInfo(type).name) + ", not a 2-node line, and cannot " +
                    (beam ? "be a beam." : "be a truss bar."));
                continue;
            }
            const auto nodes = m_mesh.elementNodes(element);
            const Eigen::Vector3d a = m_mesh.nodeCoordinates(nodes[0]);
            const Eigen::Vector3d b = m_mesh.nodeCoordinates(nodes[1]);
            if (!((b - a).norm() > 0.0)) {
                add("The " + elementName(element) + " has zero length.");
                continue;
            }
            if (kind == ElementKind::Truss2D || kind == ElementKind::Beam2D) {
                bool flat = std::abs(a.z()) <= tolerance && std::abs(b.z()) <= tolerance;
                const int offset = m_tables.offsetOf[static_cast<std::size_t>(element)];
                if (offset != kNoItem) {
                    const RigidOffset& o = m_model.rigidOffsets[static_cast<std::size_t>(offset)];
                    flat = flat && std::abs(o.start.z()) <= tolerance &&
                           std::abs(o.end.z()) <= tolerance;
                }
                if (!flat) {
                    add("The " + elementName(element) + " is a 2D " +
                        (beam ? "beam" : "truss bar") + " but does not lie in the XY plane.");
                    continue;
                }
            }
            if (beam) {
                // Its problems do not take the dofs from its nodes: the
                // supports there stay valid.
                checkBeam(element, (b - a).norm());
            }
            for (const NodeIndex node : nodes) {
                m_tables.nodeDofs[static_cast<std::size_t>(node)] |= dofsOf(kind);
            }
        }
    }

    // A beam with a valid assignment: axes, flexible length and connections.
    void checkBeam(ElementIndex element, double nodeDistance)
    {
        if (!m_valid[static_cast<std::size_t>(ownerOf(element))]) {
            return; // its material or section is already reported
        }
        const auto setup = beamSetup(m_model, element, m_tables);
        if (!setup) {
            add("The " + elementName(element) + ": " + setup.error().message);
            return;
        }
        if (!(setup->length > kFlexibleTolerance * nodeDistance)) {
            add("The " + elementName(element) + ": the rigid offsets leave no flexible length.");
            return;
        }
        if (!hasStableConnections(setup->data, setup->length, assignmentOf(element)->kind)) {
            add("The " + elementName(element) +
                " is unstable on its own: its end releases let it move without strain.");
            return;
        }
        m_flexible[static_cast<std::size_t>(element)] = setup->length;
    }

    // The thermal expansion of the material of an element, for `what`.
    void needExpansion(ElementIndex element, std::string_view what)
    {
        const Assignment* assignment = assignmentOf(element);
        if (assignment == nullptr) {
            return;
        }
        const Material* material = m_model.findMaterial(assignment->material);
        if (material != nullptr && !material->thermalExpansion) {
            addOnce("Material " + quoted(assignment->material) +
                    ": the thermal expansion must be given for the " + std::string(what) + ".");
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
                needExpansion(element, "temperature change");
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

    // REQ-008-16
    void checkGradients()
    {
        const auto& gradients = m_model.temperatureGradients;
        for (std::size_t i = 0; i < gradients.size(); ++i) {
            for (const ElementIndex element :
                 beamsOf(itemLabel("Temperature gradient", i), gradients[i].elements)) {
                needExpansion(element, "temperature gradient");
            }
        }
    }

    // The flexible length of a beam, when it is known (valid beam).
    [[nodiscard]] std::optional<double> flexibleOf(ElementIndex element) const
    {
        const double length = m_flexible[static_cast<std::size_t>(element)];
        return length > 0.0 ? std::optional<double>(length) : std::nullopt;
    }

    // REQ-008-16, 17
    void checkDistributedLoads()
    {
        const auto& loads = m_model.distributedLoads;
        for (std::size_t i = 0; i < loads.size(); ++i) {
            const std::string label = itemLabel("Distributed load", i);
            for (const ElementIndex element : beamsOf(label, loads[i].elements)) {
                const auto length = flexibleOf(element);
                if (!length) {
                    continue;
                }
                const double from = positionOn(loads[i].from, *length);
                const double to = positionOn(loads[i].to, *length);
                const double slack = kPositionTolerance * *length;
                if (from > to + slack) {
                    add(label + ": on the " + elementName(element) +
                        ", the start is after the end.");
                } else if (from < -slack || to > *length + slack) {
                    add(label + ": on the " + elementName(element) +
                        ", the load goes beyond the flexible length.");
                }
            }
        }
    }

    void checkConcentratedLoads()
    {
        const auto& loads = m_model.concentratedLoads;
        for (std::size_t i = 0; i < loads.size(); ++i) {
            const std::string label = itemLabel("Concentrated load", i);
            for (const ElementIndex element : beamsOf(label, loads[i].elements)) {
                const auto length = flexibleOf(element);
                if (!length) {
                    continue;
                }
                const double at = positionOn(loads[i].at, *length);
                const double slack = kPositionTolerance * *length;
                if (at < -slack || at > *length + slack) {
                    add(label + ": on the " + elementName(element) +
                        ", the position is beyond the flexible length.");
                }
            }
        }
    }

    // A node needs a bar, and the bar the direction; false with a problem.
    bool checkNode(const std::string& label, NodeIndex node, DofType dof)
    {
        const DofMask dofs = m_tables.nodeDofs[static_cast<std::size_t>(node)];
        if (dofs == 0) {
            add(label + ": the " + nodeName(node) + " is not connected to any bar.");
            return false;
        }
        if ((dofs & mask(dof)) != 0) {
            return true;
        }
        const bool rotates = (dofs & mask(DofType::RZ)) != 0;
        const bool rotation = dof == DofType::RX || dof == DofType::RY || dof == DofType::RZ;
        std::string why;
        if (rotation && !rotates) {
            why = "it belongs to no beam";
        } else {
            why = rotates ? "it only belongs to 2D bars" : "it only belongs to 2D trusses";
        }
        add(label + ": the " + nodeName(node) + " has no dof " + std::string(dofName(dof)) + " (" +
            why + ").");
        return false;
    }

    void checkRestraints()
    {
        const auto& restraints = m_model.restraints;
        for (std::size_t i = 0; i < restraints.size(); ++i) {
            const std::string label = itemLabel("Restraint", i);
            const DofType dof = restraints[i].dof;
            if (dof == DofType::T) {
                add(label + ": a structural model has no dof T.");
                continue;
            }
            if (dof != DofType::U && dof != DofType::V && dof != DofType::W && !m_tables.hasBeams) {
                add(label + ": a truss model only has the dofs u, v and w.");
                continue;
            }
            const auto nodes = resolve(m_mesh, restraints[i].nodes);
            if (!nodes) {
                add(label + ": " + nodes.error().message);
                continue;
            }
            for (const NodeIndex node : *nodes) {
                (void)checkNode(label, node, dof);
            }
        }
    }

    void checkForces()
    {
        const auto& forces = m_model.nodalForces;
        for (std::size_t i = 0; i < forces.size(); ++i) {
            const std::string label = itemLabel("Nodal force", i);
            const NodalForce& force = forces[i];
            const auto nodes = resolve(m_mesh, force.nodes);
            if (!nodes) {
                add(label + ": " + nodes.error().message);
                continue;
            }
            // A force without z only needs u and v; moments their rotation.
            std::vector<DofType> needed = {force.force.z() != 0.0 ? DofType::W : DofType::U};
            const std::array<DofType, 3> rotations = {DofType::RX, DofType::RY, DofType::RZ};
            for (std::size_t k = 0; k < rotations.size(); ++k) {
                if (force.moment(static_cast<Eigen::Index>(k)) != 0.0) {
                    needed.push_back(rotations.at(k));
                }
            }
            for (const NodeIndex node : *nodes) {
                for (const DofType dof : needed) {
                    if (!checkNode(label, node, dof)) {
                        break;
                    }
                }
            }
        }
    }

    const Model& m_model;
    const Mesh& m_mesh;
    ValidationTables m_tables;
    std::vector<ModelIssue> m_issues;
    std::set<std::string, std::less<>> m_said;
    std::vector<bool> m_valid;      // per assignment: material and section usable
    std::vector<double> m_flexible; // per element: flexible length of a valid beam
};

} // namespace

std::string quoted(std::string_view name)
{
    return "\"" + std::string(name) + "\"";
}

bool isBeam(ElementKind kind)
{
    return kind == ElementKind::Beam2D || kind == ElementKind::Beam3D;
}

DofMask dofsOf(ElementKind kind)
{
    const DofMask plane = mask(DofType::U) | mask(DofType::V);
    switch (kind) {
    case ElementKind::Truss2D:
        return plane;
    case ElementKind::Truss3D:
        return static_cast<DofMask>(plane | mask(DofType::W));
    case ElementKind::Beam2D:
        return static_cast<DofMask>(plane | mask(DofType::RZ));
    case ElementKind::Beam3D:
        break;
    }
    return static_cast<DofMask>(plane | mask(DofType::W) | mask(DofType::RX) | mask(DofType::RY) |
                                mask(DofType::RZ));
}

double sectionArea(const Section& section)
{
    if (!section.shape) {
        return section.area.value_or(0.0);
    }
    return sectionProperties(section).value_or(SectionProperties{}).area;
}

Expected<BeamSetup> beamSetup(const Model& model, ElementIndex element,
                              const ValidationTables& tables)
{
    const Mesh& mesh = *model.mesh;
    const auto e = static_cast<std::size_t>(element);
    const Assignment& assignment = model.assignments[static_cast<std::size_t>(tables.owner[e])];
    const Material& material = *model.findMaterial(assignment.material);
    const SectionProperties section =
        sectionProperties(*model.findSection(assignment.section)).value_or(SectionProperties{});

    BeamSetup setup;
    BeamData& beam = setup.data;
    if (const int offset = tables.offsetOf[e]; offset != kNoItem) {
        const RigidOffset& o = model.rigidOffsets[static_cast<std::size_t>(offset)];
        beam.offsets = {o.start, o.end};
    }
    const auto nodes = mesh.elementNodes(element);
    const Eigen::Vector3d p = mesh.nodeCoordinates(nodes[0]) + beam.offsets[0];
    const Eigen::Vector3d q = mesh.nodeCoordinates(nodes[1]) + beam.offsets[1];
    setup.length = (q - p).norm();
    if (!(setup.length > 0.0)) {
        return makeError("the rigid offsets leave no flexible length.");
    }
    // The principal axes: the local ones turned by the principal angle.
    const double angle = assignment.kind == ElementKind::Beam2D ? 0.0 : section.principalAngle;
    const auto axes =
        beamAxes(p, q, assignment.reference, assignment.rollAngle + angle, assignment.kind);
    if (!axes) {
        return makeError("the reference vector is parallel to the bar.");
    }
    beam.axes = *axes;

    const double e0 = material.youngsModulus.value_or(0.0);
    const double nu = material.poissonsRatio.value_or(0.0);
    const double g = material.poissonsRatio ? e0 / (2.0 * (1.0 + nu)) : 0.0;
    beam.axial = e0 * section.area;
    beam.bendingY = e0 * section.inertiaY;
    beam.bendingZ = e0 * section.inertiaZ;
    beam.torsional = g * section.torsionConstant;
    if (assignment.theory == BeamTheory::Timoshenko) {
        beam.shearY = g * section.shearAreaY.value_or(0.0);
        beam.shearZ = g * section.shearAreaZ.value_or(0.0);
    }
    beam.shearCentre = section.shearCentre;
    for (int end = 0; end < 2; ++end) {
        const int item = tables.connectionOf[e][static_cast<std::size_t>(end)];
        if (item == kNoItem) {
            continue;
        }
        const auto& stiffness = model.endConnections[static_cast<std::size_t>(item)].stiffness;
        for (std::size_t k = 0; k < stiffness.size(); ++k) {
            beam.springs[static_cast<std::size_t>(end)][k] = stiffness[k].value_or(kRigid);
        }
    }
    if (const int foundation = tables.foundationOf[e]; foundation != kNoItem) {
        const ElasticFoundation& f = model.foundations[static_cast<std::size_t>(foundation)];
        beam.foundation << f.translational, f.torsional;
    }
    setup.frame.principalAxes = beam.axes;
    setup.frame.principalAngle = angle;
    setup.frame.shearCentre = section.shearCentre;
    return setup;
}

Validation validateModel(const Model& model)
{
    Validator validator(model);
    Validation validation;
    validation.tables = validator.run();
    validation.issues = validator.issues();
    return validation;
}

} // namespace fem::core::detail
