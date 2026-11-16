#pragma once

#include <fem/core/dof_map.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/material.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <array>
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

enum class ElementKind : std::uint8_t { Truss2D, Truss3D, Beam2D, Beam3D };

// REQ-008-07
enum class BeamTheory : std::uint8_t { EulerBernoulli, Timoshenko };

// The axes in which a load on a bar is given.
enum class Axes : std::uint8_t { Global, Local };

// REQ-007-04, 008-07, 09: what the elements of a target are made of. The
// theory and the orientation only matter for beams.
struct Assignment
{
    ElementTarget elements;
    ElementKind kind = ElementKind::Truss3D;
    std::string material;
    std::string section;
    BeamTheory theory = BeamTheory::EulerBernoulli;
    // 3D beams: the local y is the part of this vector normal to the bar
    // (default: global Z, or global X for a bar along Z; design 008 §5.2).
    std::optional<Eigen::Vector3d> reference = std::nullopt;
    double rollAngle = 0.0; // about the bar axis, radians
};

// REQ-007-05, 008-16: a support (value 0) or a settlement in one direction,
// a translation (u, v, w) or a rotation (rx, ry, rz).
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
    Eigen::Vector3d force = Eigen::Vector3d::Zero();  // on each node
    Eigen::Vector3d moment = Eigen::Vector3d::Zero(); // REQ-008-16: Mx, My, Mz
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

enum class BarEnd : std::uint8_t { Start, End, Both };

// REQ-008-11, 12: per local force of the end (N, Vy, Vz, T, My, Mz): empty is
// a rigid connection, 0 a release, a positive value a spring. 2D beams only
// use N, Vy and Mz.
struct EndConnection
{
    ElementTarget elements;
    BarEnd end = BarEnd::Both;
    std::array<std::optional<double>, 6> stiffness{};
};

// REQ-008-14: rigid links, in global axes, from each node to the start and to
// the end of the flexible length.
struct RigidOffset
{
    ElementTarget elements;
    Eigen::Vector3d start = Eigen::Vector3d::Zero();
    Eigen::Vector3d end = Eigen::Vector3d::Zero();
};

// REQ-008-15: Winkler springs per length, in local axes.
struct ElasticFoundation
{
    ElementTarget elements;
    Eigen::Vector3d translational = Eigen::Vector3d::Zero(); // kx, ky, kz
    double torsional = 0.0;                                  // about x
};

// REQ-008-17: a position along the flexible length, as a fraction of it or
// as a length from its start.
struct BarPosition
{
    double value = 0.0;
    bool fraction = true;
};

// REQ-008-16: a load per length of the bar, uniform or linear between two
// positions.
struct DistributedLoad
{
    ElementTarget elements;
    Axes axes = Axes::Global;
    Eigen::Vector3d startValue = Eigen::Vector3d::Zero();
    Eigen::Vector3d endValue = Eigen::Vector3d::Zero();
    BarPosition from{.value = 0.0, .fraction = true};
    BarPosition to{.value = 1.0, .fraction = true};
};

// REQ-008-16: a force and a moment at a point of the span.
struct ConcentratedLoad
{
    ElementTarget elements;
    Axes axes = Axes::Global;
    Eigen::Vector3d force = Eigen::Vector3d::Zero();
    Eigen::Vector3d moment = Eigen::Vector3d::Zero();
    BarPosition at{.value = 0.5, .fraction = true};
};

// REQ-008-16: temperature gradients across the section, local axes, uniform
// along the bar.
struct TemperatureGradient
{
    ElementTarget elements;
    double alongY = 0.0; // dT/dy
    double alongZ = 0.0; // dT/dz
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
    // Feature 008: beams.
    std::vector<EndConnection> endConnections;
    std::vector<RigidOffset> rigidOffsets;
    std::vector<ElasticFoundation> foundations;
    std::vector<DistributedLoad> distributedLoads;
    std::vector<ConcentratedLoad> concentratedLoads;
    std::vector<TemperatureGradient> temperatureGradients;

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
