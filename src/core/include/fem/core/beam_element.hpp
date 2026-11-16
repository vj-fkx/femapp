#pragma once

#include <fem/core/element_formulation.hpp>
#include <fem/core/model.hpp>

#include <Eigen/Core>
#include <array>
#include <limits>
#include <optional>
#include <span>

namespace fem::core {

// A rigid connection in BeamData::springs.
inline constexpr double kRigid = std::numeric_limits<double>::infinity();

// Local dofs of one end, in this order (design 008 §5.3).
enum class BeamDof : std::uint8_t { U, V, W, RX, RY, RZ };
inline constexpr int kBeamEndDofs = 6;
inline constexpr int kBeamDofs = 12;

using BeamVector = Eigen::Matrix<double, kBeamDofs, 1>;
using BeamMatrix = Eigen::Matrix<double, kBeamDofs, kBeamDofs>;

// What a beam needs, resolved from the model (design 008 §5.3, 5.4). All in
// the principal local axes of the flexible length.
struct BeamData
{
    double axial = 0.0;     // EA
    double torsional = 0.0; // GJ
    double bendingY = 0.0;  // EIy: bending by My, in the x-z plane
    double bendingZ = 0.0;  // EIz: bending by Mz, in the x-y plane
    double shearY = 0.0;    // G Asy; 0: Euler-Bernoulli (no shear strain)
    double shearZ = 0.0;
    Eigen::Matrix3d axes = Eigen::Matrix3d::Identity();    // columns: x, y, z in global
    Eigen::Vector2d shearCentre = Eigen::Vector2d::Zero(); // (y, z) from the centroid
    // Global vectors from each node to the end of the flexible length.
    std::array<Eigen::Vector3d, 2> offsets{Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero()};
    // Per end and local force: kRigid, a spring, or 0 (released).
    std::array<std::array<double, kBeamEndDofs>, 2> springs{
        {{kRigid, kRigid, kRigid, kRigid, kRigid, kRigid},
         {kRigid, kRigid, kRigid, kRigid, kRigid, kRigid}}};
    Eigen::Vector4d foundation = Eigen::Vector4d::Zero(); // kx, ky, kz, k_rx per length
    // Loads of the flexible length held at both ends, local dofs at the
    // centroid: the equivalent loads of design 008 §5.5.
    BeamVector fixedEndLoads = BeamVector::Zero();
    // Feature 008b: E Iw (0: no warping) and, for an arc, the turn rate of
    // the local frame along it, in the principal local axes (design 008b
    // §5.2, 5.3).
    double warpingStiffness = 0.0;
    Eigen::Vector3d curvature = Eigen::Vector3d::Zero();
};

// REQ-008-09, 10: local axes of a bar from x1 to x2, as columns (x, y, z).
// 3D: y is the part of the reference normal to the bar (default global Z,
// or global X for a bar along Z), then y and z turn by `roll` about x. 2D:
// y = Z x x and z = Z. Fails for a zero length or a reference parallel to
// the bar.
[[nodiscard]] Expected<Eigen::Matrix3d> beamAxes(const Eigen::Vector3d& x1,
                                                 const Eigen::Vector3d& x2,
                                                 const std::optional<Eigen::Vector3d>& reference,
                                                 double roll, ElementKind kind);

// Transverse interpolation of the theory in one plane at xi in [0, 1], for
// the dofs (v1, t1, v2, t2) with t = dv/dx in Euler-Bernoulli: the exact
// homogeneous solutions, which depend on phi = 12 EI / (G As L^2)
// (Friedman and Kosmatka, 1993). phi = 0 gives the cubic Hermite functions.
struct PlaneShape
{
    Eigen::Vector4d displacement = Eigen::Vector4d::Zero(); // v(xi)
    Eigen::Vector4d rotation = Eigen::Vector4d::Zero();     // t(xi)
};
[[nodiscard]] PlaneShape planeShape(double xi, double length, double phi);

// phi of the plane x-y (`aboutZ`) or x-z, for a flexible length.
[[nodiscard]] double shearParameter(const BeamData& beam, double length, bool aboutZ);

// The flexible length between the ends of the offsets.
[[nodiscard]] double flexibleLength(const BeamData& beam, const Eigen::Vector3d& x1,
                                    const Eigen::Vector3d& x2);

// REQ-008-13: false when the releases and springs let the flexible length
// move without strain (its internal dofs are singular).
[[nodiscard]] bool hasStableConnections(const BeamData& beam, double length, ElementKind kind);

// The flexible length on its own: local dofs at the centroid of both ends,
// stiffness of the theory (with the shear centre and the foundation).
[[nodiscard]] BeamMatrix flexibleStiffness(const BeamData& beam, double length);

// The beam after the analysis: local dofs and forces at both ends of the
// flexible length (forces that the ends apply on the bar, local axes).
struct BeamEnds
{
    BeamVector displacements = BeamVector::Zero();
    BeamVector forces = BeamVector::Zero();
};

// From the 12 global nodal values (u, v, w, rx, ry, rz of each node).
[[nodiscard]] BeamEnds beamEnds(const BeamData& beam, ElementKind kind, const Eigen::Vector3d& x1,
                                const Eigen::Vector3d& x2, const BeamVector& nodeValues);

// REQ-008-05 to 15: the 2-node beam (Line2), in 3D with u, v, w, rx, ry, rz
// per node or in the XY plane with u, v, rz.
class BeamElement final : public ElementFormulation
{
public:
    // `beams` is indexed by ElementIndex and must outlive the formulation.
    BeamElement(ElementKind kind, std::span<const BeamData> beams);

    [[nodiscard]] DofMask dofsAt(ElementType type, int localNode) const override;
    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override;

private:
    ElementKind m_kind;
    std::span<const BeamData> m_beams;
};

} // namespace fem::core
