#pragma once

#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fem::core {

struct ElementSet;

// REQ-006-08: degrees of freedom of a node.
enum class DofType : std::uint8_t { U, V, W, RX, RY, RZ, T };
inline constexpr int kDofTypeCount = 7;

// Bit k set: DofType k used.
using DofMask = std::uint8_t;

[[nodiscard]] constexpr DofMask mask(DofType type) noexcept
{
    return static_cast<DofMask>(1U << static_cast<unsigned>(type));
}

[[nodiscard]] std::string_view dofName(DofType type) noexcept; // "u", "v", ... "T"

// Index of a dof of the map, 0 .. size() - 1.
using DofIndex = std::int32_t;

// REQ-006-08 to 11: dofs per node from the elements, numbered node by node
// and, within a node, in DofType order; prescribed dofs are left out of the
// equations.
class DofMap
{
public:
    // The union, at each node, of the dofs its elements use.
    [[nodiscard]] static DofMap build(const Mesh& mesh, std::span<const ElementSet> sets);

    [[nodiscard]] std::int32_t size() const noexcept
    {
        return static_cast<std::int32_t>(m_dofNode.size());
    }
    [[nodiscard]] DofMask maskOf(NodeIndex node) const;
    // Dofs of a node, as a contiguous range of DofIndex.
    [[nodiscard]] DofIndex firstDof(NodeIndex node) const;
    [[nodiscard]] std::optional<DofIndex> find(NodeIndex node, DofType type) const;
    [[nodiscard]] NodeIndex nodeOf(DofIndex dof) const;
    [[nodiscard]] DofType typeOf(DofIndex dof) const;

    // REQ-006-10, 11: zero for a support, another value for a settlement.
    [[nodiscard]] Expected<void> prescribe(NodeIndex node, DofType type, double value);
    [[nodiscard]] bool isPrescribed(DofIndex dof) const;
    [[nodiscard]] double prescribedValue(DofIndex dof) const;

    // Equations, numbered over the free dofs in DofIndex order: -1 for a
    // prescribed dof. Numbered on the first call after a prescription (not
    // thread safe on that call; the assembler asks before going parallel).
    [[nodiscard]] std::int32_t equation(DofIndex dof) const;
    [[nodiscard]] std::int32_t freeCount() const;

private:
    void numberEquations() const;

    const Mesh* m_mesh = nullptr;
    std::vector<DofMask> m_masks;                 // per node
    std::vector<DofIndex> m_first;                // per node, plus one
    std::vector<NodeIndex> m_dofNode;             // per dof
    std::vector<DofType> m_dofType;               // per dof
    std::vector<std::uint8_t> m_fixed;            // per dof
    std::vector<double> m_values;                 // per dof
    mutable std::vector<std::int32_t> m_equation; // per dof
    mutable std::int32_t m_freeCount = 0;
    mutable bool m_numbered = false;
};

} // namespace fem::core
