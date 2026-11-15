#pragma once

#include <fem/core/dof_map.hpp>
#include <fem/core/element_type.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/isoparametric.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <span>

namespace fem::core {

// Largest number of dofs of one element: 27 nodes with 7 dofs each.
inline constexpr int kMaxElementDofs = kMaxNodes * kDofTypeCount;

// REQ-006-12: what one kind of element contributes. The local dofs go node by
// node in local order and, within a node, in DofType order. Implementations
// are const and thread safe, and do not allocate in compute().
class ElementFormulation
{
public:
    ElementFormulation() = default;
    virtual ~ElementFormulation() = default;
    ElementFormulation(const ElementFormulation&) = delete;
    ElementFormulation& operator=(const ElementFormulation&) = delete;
    ElementFormulation(ElementFormulation&&) = delete;
    ElementFormulation& operator=(ElementFormulation&&) = delete;

    // The dofs this formulation uses at local node `localNode` of `type`.
    [[nodiscard]] virtual DofMask dofsAt(ElementType type, int localNode) const = 0;

    // The local matrix (size x size) and vector (size), zeroed by the
    // caller; size is the sum of the dofs of the nodes. `element` is the
    // internal index, for formulations with data per element.
    [[nodiscard]] virtual Expected<void> compute(const ElementNodes& nodes, ElementIndex element,
                                                 Eigen::Ref<Eigen::MatrixXd> matrix,
                                                 Eigen::Ref<Eigen::VectorXd> vector) const = 0;
};

// Elements that share one formulation.
struct ElementSet
{
    std::span<const ElementIndex> elements;
    const ElementFormulation* formulation = nullptr;
};

} // namespace fem::core
