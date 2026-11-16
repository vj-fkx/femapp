#pragma once

#include <fem/core/dof_map.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/material.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fem::core {

// A group of the mesh, by name.
struct GroupRef
{
    std::string name;
};

// External ids: the numbers the user sees.
struct ElementIds
{
    std::vector<ExternalId> ids;
};

struct NodeIds
{
    std::vector<ExternalId> ids;
};

// REQ-007-04: elements, as a group of dimension 1 or a list of ids.
using ElementTarget = std::variant<GroupRef, ElementIds>;
// REQ-007-05: nodes, as the nodes of the groups with that name (of any
// dimension) or a list of ids.
using NodeTarget = std::variant<GroupRef, NodeIds>;

enum class ElementKind : std::uint8_t { Truss2D, Truss3D };

// REQ-007-04: what the elements of a target are made of.
struct Assignment
{
    ElementTarget elements;
    ElementKind kind = ElementKind::Truss3D;
    std::string material;
    std::string section;
};

// REQ-007-05: a support (value 0) or a settlement in one direction.
struct Restraint
{
    NodeTarget nodes;
    DofType dof = DofType::U;
    double value = 0.0;
};

// REQ-007-06: actions of the single load case.
struct NodalForce
{
    NodeTarget nodes;
    Eigen::Vector3d force = Eigen::Vector3d::Zero(); // on each node
};

struct TemperatureChange
{
    ElementTarget elements;
    double deltaT = 0.0;
};

struct InitialStrain
{
    ElementTarget elements;
    double strain = 0.0; // eps0: assembly defect or prestress
};

// REQ-007-03: the document of an analysis. A value type: copies share the
// immutable mesh. Materials and sections are referred to by name; nothing is
// checked on edit (see validate() in static_analysis.hpp).
struct Model
{
    std::shared_ptr<const Mesh> mesh;
    std::vector<Material> materials;
    std::vector<Section> sections;
    std::vector<Assignment> assignments;
    std::vector<Restraint> restraints;
    std::vector<NodalForce> nodalForces;
    std::optional<Eigen::Vector3d> gravity; // acceleration on the whole model
    std::vector<TemperatureChange> temperatureChanges;
    std::vector<InitialStrain> initialStrains;

    // The first with that name, or null.
    [[nodiscard]] const Material* findMaterial(std::string_view name) const;
    [[nodiscard]] const Section* findSection(std::string_view name) const;
};

// Internal indices of a target, sorted and unique, or why it does not
// resolve (missing group, unknown id).
[[nodiscard]] Expected<std::vector<ElementIndex>> resolve(const Mesh& mesh,
                                                          const ElementTarget& target);
[[nodiscard]] Expected<std::vector<NodeIndex>> resolve(const Mesh& mesh, const NodeTarget& target);

} // namespace fem::core
