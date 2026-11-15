#include <fem/core/isoparametric.hpp>

#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fem::core {

Expected<PointGeometry> evaluateGeometry(const ElementNodes& nodes, const std::array<double, 3>& xi)
{
    PointGeometry g;
    evaluateShape(nodes.type, xi, g.shape);
    const int count = g.shape.count;
    const int dims = elementTypeInfo(nodes.type).dimension;

    // J(k, d) = sum_i x_i(k) dN_i/dxi_d, columns beyond the dimension zero.
    Eigen::Matrix3d jacobian = Eigen::Matrix3d::Zero();
    for (int i = 0; i < count; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const Eigen::Vector3d& x = nodes.x.at(ii);
        g.position += g.shape.n.at(ii) * x;
        for (int d = 0; d < dims; ++d) {
            jacobian.col(d) += g.shape.dn.at(ii).at(static_cast<std::size_t>(d)) * x;
        }
    }

    // dN/dx = A dN/dxi, with A = J^-T for solids and J (J^T J)^-1 for lines
    // and surfaces (the gradient along the manifold).
    Eigen::Matrix3d map = Eigen::Matrix3d::Zero();
    if (dims == 3) {
        g.measure = jacobian.determinant();
        if (!(g.measure > 0.0)) {
            return makeError("The element has a non-positive Jacobian determinant.");
        }
        map = jacobian.inverse().transpose();
    } else if (dims == 2) {
        const Eigen::Matrix<double, 3, 2> j = jacobian.leftCols<2>();
        const Eigen::Matrix2d metric = j.transpose() * j;
        const double det = metric.determinant();
        g.measure = std::sqrt(std::max(det, 0.0));
        if (!(det > 0.0)) {
            return makeError("The element has a zero area.");
        }
        map.leftCols<2>() = j * metric.inverse();
    } else {
        const Eigen::Vector3d t = jacobian.col(0);
        const double length2 = t.squaredNorm();
        g.measure = std::sqrt(length2);
        if (!(length2 > 0.0)) {
            return makeError("The element has a zero length.");
        }
        map.col(0) = t / length2;
    }

    for (int i = 0; i < count; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        const auto& reference = g.shape.dn.at(ii);
        const Eigen::Vector3d physical =
            map * Eigen::Vector3d(reference[0], reference[1], reference[2]);
        g.shape.dn.at(ii) = {physical.x(), physical.y(), physical.z()};
    }
    return g;
}

} // namespace fem::core
