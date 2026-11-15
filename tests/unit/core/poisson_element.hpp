#pragma once

// Test-only scalar element (design 006 §5.8): -div(k grad u) = s, one dof T
// per node. Works on lines, surfaces (also in space) and solids.

#include <fem/core/element_formulation.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/quadrature.hpp>
#include <fem/core/shape_functions.hpp>

#include <algorithm>
#include <cstddef>

namespace fem::core::test {

class PoissonElement final : public ElementFormulation
{
public:
    using Source = double (*)(const Eigen::Vector3d&);

    explicit PoissonElement(Source source = nullptr, double conductivity = 1.0)
        : m_source(source)
        , m_conductivity(conductivity)
    {}

    [[nodiscard]] DofMask dofsAt(ElementType /*type*/, int /*localNode*/) const override
    {
        return mask(DofType::T);
    }

    [[nodiscard]] Expected<void> compute(const ElementNodes& nodes, ElementIndex /*element*/,
                                         Eigen::Ref<Eigen::MatrixXd> matrix,
                                         Eigen::Ref<Eigen::VectorXd> vector) const override
    {
        if (!hasShapeFunctions(nodes.type)) {
            return makeError("The element type is not supported.");
        }
        const auto& info = elementTypeInfo(nodes.type);
        // Exact for affine elements; a little more for curved ones.
        const int degree = std::min(2 * info.order + 2, maxQuadratureDegree(info.family));
        for (const QuadraturePoint& point : quadrature(info.family, degree)) {
            const auto g = evaluateGeometry(nodes, point.xi);
            if (!g) {
                return tl::unexpected(g.error());
            }
            const double w = point.weight * g->measure;
            const double s = m_source != nullptr ? m_source(g->position) : 0.0;
            for (int i = 0; i < nodes.count; ++i) {
                const auto ii = static_cast<std::size_t>(i);
                const auto& di = g->shape.dn.at(ii);
                vector[i] += w * g->shape.n.at(ii) * s;
                for (int j = 0; j < nodes.count; ++j) {
                    const auto& dj = g->shape.dn.at(static_cast<std::size_t>(j));
                    matrix(i, j) +=
                        w * m_conductivity * (di[0] * dj[0] + di[1] * dj[1] + di[2] * dj[2]);
                }
            }
        }
        return {};
    }

private:
    Source m_source;
    double m_conductivity;
};

} // namespace fem::core::test
