#pragma once

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_result.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/model.hpp>

#include <Eigen/Core>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace fem::core {

// REQ-008b-14, 15: the loads of an isoparametric curved beam as the model
// gives them, with positions along the arc length of its flexible curve.
struct IsoLoads
{
    struct Distributed
    {
        Axes axes = Axes::Global;
        Eigen::Vector3d startValue = Eigen::Vector3d::Zero();
        Eigen::Vector3d endValue = Eigen::Vector3d::Zero();
        double from = 0.0;
        double to = 0.0;
        double startTorque = 0.0; // about the local x
        double endTorque = 0.0;
    };
    struct Concentrated
    {
        Axes axes = Axes::Global;
        Eigen::Vector3d force = Eigen::Vector3d::Zero();
        Eigen::Vector3d moment = Eigen::Vector3d::Zero();
        double at = 0.0;
    };
    std::vector<Distributed> distributed;
    std::vector<Concentrated> concentrated;
    double imposedStrain = 0.0;
    double curvatureY = 0.0; // imposed, principal axes (as SpanLoads)
    double curvatureZ = 0.0;
};

// What an isoparametric beam needs: the section and connections of the
// BeamData (its axes are not used), the rule of its local axes (design 008
// §5.2, applied at each point: reference, roll plus principal angle) and
// its loads.
struct IsoBeamData
{
    BeamData beam;
    std::optional<Eigen::Vector3d> reference;
    double roll = 0.0; // the roll of the assignment plus the principal angle
    IsoLoads loads;
};

// The points of the flexible curve: its ends (the nodes moved by the
// offsets) and the middle node, in the order of the Line3.
using IsoNodes = std::array<Eigen::Vector3d, 3>;
[[nodiscard]] IsoNodes isoNodes(const BeamData& beam, const ElementNodes& nodes);

// The length of the flexible curve, and the parameter xi in [-1, 1] at an
// arc length s from its start.
[[nodiscard]] double isoLength(const IsoNodes& x);
[[nodiscard]] double isoParameter(const IsoNodes& x, double s);

// REQ-008b-14 (design 008b §5.5): the 3-node isoparametric beam, with dofs
// at its three nodes: u and the rotations quadratic in global components,
// selective reduced integration (3 points for bending and torsion, 2 for
// the normal and shear strains) and, for Euler-Bernoulli, a penalty shear
// stiffness of 1e6 EI / l^2.
class IsoBeamElement final : public ElementFormulation
{
public:
    // `beams` is indexed by ElementIndex and must outlive the formulation.
    // Feature 008d (design 008d §5.3): with `warping`, the warping dof at the
    // middle node and at the ends that `continuous` says; all the beams of
    // the formulation agree with their BeamData.
    IsoBeamElement(ElementKind kind, std::span<const IsoBeamData> beams, bool warping = false,
                   std::array<bool, 2> continuous = {false, false});

    [[nodiscard]] DofMask dofsAt(ElementType type, int localNode) const override;
    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override;

private:
    ElementKind m_kind;
    std::span<const IsoBeamData> m_beams;
    bool m_warping = false;
    std::array<bool, 2> m_continuous{false, false};
};

// REQ-008b-19: a point of an isoparametric beam at the arc length s, from
// the values of the element's nodal dofs (in its order): displacements by
// the shape functions; moments and torque from the curvatures there;
// normal and shear forces extrapolated from the two reduced points (design
// 008b §5.5). Local principal axes at s.
[[nodiscard]] BeamPoint isoBeamAt(const IsoBeamData& data, ElementKind kind,
                                  const ElementNodes& nodes, std::span<const double> nodeValues,
                                  double s);

// REQ-008b-16: fails when the curve folds on itself or the reference is
// parallel to it at a point of the integration.
[[nodiscard]] Expected<void> isoCheck(const IsoBeamData& data, ElementKind kind,
                                      const ElementNodes& nodes);

// REQ-008b-13: false when the connections let the beam move without strain.
[[nodiscard]] bool hasStableIsoConnections(const IsoBeamData& data, ElementKind kind,
                                           const ElementNodes& nodes);

} // namespace fem::core
