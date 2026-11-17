#pragma once

#include <fem/core/element_formulation.hpp>
#include <fem/core/model.hpp>

#include <Eigen/Core>
#include <span>

namespace fem::core {

// What a bar needs, resolved from the model (design 007 §5.1).
struct BarData
{
    double axialStiffness = 0.0;                      // EA
    double imposedStrain = 0.0;                       // alpha * dT + eps0
    Eigen::Vector3d weight = Eigen::Vector3d::Zero(); // rho * A * g, per length
    // Feature 008c: the axial force of the reference state (tension
    // positive), for the geometric stiffness (design 008c §5.3).
    double axialForce = 0.0;
};

// REQ-007-08, 11: the 2-node truss bar (Line2), in 3D with u, v, w per node
// or in the XY plane with u, v. Stiffness EA/L along the bar; equivalent
// loads of the imposed strain and of the weight. REQ-008c-03: plus the
// geometric stiffness N/L (I - e e^T) of the reference axial force.
class TrussElement final : public ElementFormulation
{
public:
    // `bars` is indexed by ElementIndex and must outlive the formulation.
    TrussElement(ElementKind kind, std::span<const BarData> bars);

    [[nodiscard]] DofMask dofsAt(ElementType type, int localNode) const override;
    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override;

private:
    ElementKind m_kind;
    std::span<const BarData> m_bars;
};

// REQ-007-12: N = EA ((u2 - u1).e / L - imposed strain), tension positive.
[[nodiscard]] double axialForce(const BarData& bar, const Eigen::Vector3d& x1,
                                const Eigen::Vector3d& x2, const Eigen::Vector3d& u1,
                                const Eigen::Vector3d& u2);

} // namespace fem::core
