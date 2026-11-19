#pragma once

#include <fem/core/element_formulation.hpp>
#include <fem/core/model.hpp>

#include <Eigen/Core>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
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

// The local dofs of both ends of an exact bar (design 008b §5.3), at most 7
// per end: (u, v, w, rx, ry, rz, warping) with warping, (u, v, w, rx, ry, rz)
// without, (u, v, rz) in the plane.
inline constexpr int kMaxStateDofs = 14;
using StateBarMatrix =
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, kMaxStateDofs, kMaxStateDofs>;
using StateBarVector = Eigen::Matrix<double, Eigen::Dynamic, 1, 0, kMaxStateDofs, 1>;

// Feature 008d (design 008d §5.4): the internal forces of the reference
// state at a point, in the convention of BeamForces (N tension positive,
// T about the shear centre).
struct ReferencePoint
{
    double n = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double t = 0.0;
    double my = 0.0;
    double mz = 0.0;

    [[nodiscard]] ReferencePoint operator*(double factor) const
    {
        return {.n = n * factor,
                .vy = vy * factor,
                .vz = vz * factor,
                .t = t * factor,
                .my = my * factor,
                .mz = mz * factor};
    }
};

// REQ-008c-02, 04, 008d-10: the reference state along the flexible length
// (design 008c §5.2, 008d §5.4). count 0: none (first order); 1: constant;
// otherwise 2 per part of parts() equal parts, at the two Gauss points of
// each, every force linear within a part. loadFactor multiplies the span
// loads in the terms of the second order (1 in the second order, lambda in
// the buckling).
inline constexpr int kMaxReferenceParts = 32;
struct ReferenceProfile
{
    int count = 0;
    std::array<ReferencePoint, std::size_t{2} * kMaxReferenceParts> values{};
    double loadFactor = 0.0;
    // Feature 008d: a straight bar whose forces are exact by statics from
    // `start` and its span loads (times loadFactor); the values above are
    // their samples (design 008d §5.4).
    bool statics = false;
    ReferencePoint start{};

    [[nodiscard]] static ReferenceProfile constant(double n);
    [[nodiscard]] static ReferenceProfile constant(const ReferencePoint& point);
    // The two Gauss points of part `part` of `parts` equal parts of
    // [0, length].
    [[nodiscard]] static std::array<double, 2> gaussPoints(int part, int parts, double length);

    [[nodiscard]] bool active() const { return count > 0; }
    [[nodiscard]] int parts() const { return count > 1 ? count / 2 : 1; }
    // The forces at s in [0, length]; zero when inactive.
    [[nodiscard]] ReferencePoint at(double s, double length) const;
    [[nodiscard]] double axialAt(double s, double length) const { return at(s, length).n; }
    // Whether N changes along the bar (the other forces may vary with N
    // constant: a beam whose moments vary).
    [[nodiscard]] bool axialVaries() const;
    // Whether any force changes along the bar.
    [[nodiscard]] bool varies() const;
    // The largest compression, >= 0, including the ends of the parts.
    [[nodiscard]] double largestCompression() const;
    // The same profile with every force (and the load factor) times `factor`.
    [[nodiscard]] ReferenceProfile scaled(double factor) const;
};

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
    // REQ-008d-03: the warping of each end, resolved (design 008d §5.2):
    // a continuous end uses the warping dof of its node; the others have
    // a bimoment spring, kRigid (restrained), 0 (free) or a value.
    std::array<double, 2> warpingSprings{0.0, 0.0};
    std::array<bool, 2> warpingContinuous{false, false};
    // The principal local axes at the end of the flexible length (columns,
    // global): the same as `axes` for a straight bar, turned along an arc.
    Eigen::Matrix3d endAxes = Eigen::Matrix3d::Identity();
    // > 0: the flexible length along an arc (design 008b §5.2).
    double arcLength = 0.0;
    // REQ-008d-10, 12: r0^2 about the shear centre and the monosymmetry
    // coefficients of the section (SectionProperties), for the
    // flexural-torsional terms.
    double polarRadius2 = 0.0;
    double monosymmetryY = 0.0;
    double monosymmetryZ = 0.0;
    // Features 008c, 008d: the reference state, for the second order and the
    // buckling (design 008c §5.1, 5.2, 008d §5.4); null: first order. Shared:
    // the copies of the data (results, modes) do not repeat it.
    std::shared_ptr<const ReferenceProfile> reference;
};

// The reference state of a beam, or an inactive one.
[[nodiscard]] const ReferenceProfile& referenceOf(const BeamData& beam);

// REQ-008-09, 10: local axes of a bar from x1 to x2, as columns (x, y, z).
// 3D: y is the part of the reference normal to the bar (default global Z,
// or global X for a bar along Z), then y and z turn by `roll` about x. 2D:
// y = Z x x and z = Z. Fails for a zero length or a reference parallel to
// the bar.
[[nodiscard]] Expected<Eigen::Matrix3d> beamAxes(const Eigen::Vector3d& x1,
                                                 const Eigen::Vector3d& x2,
                                                 const std::optional<Eigen::Vector3d>& reference,
                                                 double roll, ElementKind kind);

// REQ-008b-13, 16: the circular arc through the ends p and q and the middle
// point m (design 008b §5.2). A straight bar has radius 0. Fails when m
// coincides with an end, when the arc closes on itself, or when m is on the
// line of the ends but not between them.
struct Arc
{
    double length = 0.0; // along the arc
    double radius = 0.0; // 0: straight
    Eigen::Vector3d centre = Eigen::Vector3d::Zero();
    // Unit normal of its plane, so that p, m, q turn counterclockwise about it:
    // the tangent turns as t' = normal x t / radius.
    Eigen::Vector3d normal = Eigen::Vector3d::UnitZ();
    Eigen::Vector3d startTangent = Eigen::Vector3d::UnitX();
    Eigen::Vector3d endTangent = Eigen::Vector3d::UnitX();
};
[[nodiscard]] Expected<Arc> arcThrough(const Eigen::Vector3d& p, const Eigen::Vector3d& m,
                                       const Eigen::Vector3d& q);

// The turn rate of the principal local axes along an arc, in those axes:
// the axes at s are startAxes e^(s [curvature]x) (design 008b §5.2).
[[nodiscard]] Eigen::Vector3d arcCurvature(const Eigen::Matrix3d& startAxes, const Arc& arc);
[[nodiscard]] Eigen::Matrix3d arcAxesAt(const Eigen::Matrix3d& startAxes,
                                        const Eigen::Vector3d& curvature, double s);

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
