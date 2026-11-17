#include <fem/core/truss_element.hpp>

#include <cstddef>

namespace fem::core {

TrussElement::TrussElement(ElementKind kind, std::span<const BarData> bars)
    : m_kind(kind)
    , m_bars(bars)
{}

DofMask TrussElement::dofsAt(ElementType /*type*/, int /*localNode*/) const
{
    const DofMask plane = mask(DofType::U) | mask(DofType::V);
    return m_kind == ElementKind::Truss2D ? plane : static_cast<DofMask>(plane | mask(DofType::W));
}

Expected<void> TrussElement::compute(const ElementNodes& nodes, ElementIndex element,
                                     Eigen::Ref<Eigen::MatrixXd> matrix,
                                     Eigen::Ref<Eigen::VectorXd> vector) const
{
    if (nodes.type != ElementType::Line2) {
        return makeError("A truss element must be a 2-node line.");
    }
    const BarData& bar = m_bars[static_cast<std::size_t>(element)];
    const Eigen::Vector3d delta = nodes.x[1] - nodes.x[0];
    const double length = delta.norm();
    if (!(length > 0.0)) {
        return makeError("The bar has zero length.");
    }
    const int dims = m_kind == ElementKind::Truss2D ? 2 : 3;
    const Eigen::Vector3d e = delta / length;

    // K = EA/L [e e^T, -e e^T; -e e^T, e e^T] + N/L [G, -G; -G, G], with
    // G = I - e e^T: the chord turning under the axial force.
    const double k = bar.axialStiffness / length;
    const double g = bar.axialForce / length;
    const double imposed = bar.axialStiffness * bar.imposedStrain;
    const double halfWeight = 0.5 * length;
    for (int i = 0; i < dims; ++i) {
        for (int j = 0; j < dims; ++j) {
            const double kij = k * e(i) * e(j) + g * ((i == j ? 1.0 : 0.0) - e(i) * e(j));
            matrix(i, j) = kij;
            matrix(dims + i, dims + j) = kij;
            matrix(i, dims + j) = -kij;
            matrix(dims + i, j) = -kij;
        }
        // An imposed elongation pushes the nodes apart.
        vector(i) = -imposed * e(i) + halfWeight * bar.weight(i);
        vector(dims + i) = imposed * e(i) + halfWeight * bar.weight(i);
    }
    return {};
}

double axialForce(const BarData& bar, const Eigen::Vector3d& x1, const Eigen::Vector3d& x2,
                  const Eigen::Vector3d& u1, const Eigen::Vector3d& u2)
{
    const Eigen::Vector3d delta = x2 - x1;
    const double length = delta.norm();
    const double strain = (u2 - u1).dot(delta) / (length * length);
    return bar.axialStiffness * (strain - bar.imposedStrain);
}

} // namespace fem::core
