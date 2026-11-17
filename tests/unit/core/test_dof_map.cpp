#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using fem::core::DofMap;
using fem::core::DofMask;
using fem::core::DofType;
using fem::core::ElementFormulation;
using fem::core::ElementIndex;
using fem::core::ElementSet;
using fem::core::ElementType;

namespace {

// A formulation that only declares dofs.
class Declares final : public ElementFormulation
{
public:
    explicit Declares(DofMask dofs)
        : m_dofs(dofs)
    {}
    [[nodiscard]] DofMask dofsAt(ElementType /*type*/, int /*localNode*/) const override
    {
        return m_dofs;
    }
    [[nodiscard]] fem::core::Expected<void>
    compute(const fem::core::ElementNodes& /*nodes*/, ElementIndex /*element*/,
            Eigen::Ref<Eigen::MatrixXd> /*matrix*/,
            Eigen::Ref<Eigen::VectorXd> /*vector*/) const override
    {
        return {};
    }

private:
    DofMask m_dofs;
};

// Nodes 10, 20, 30, 40, 50: a bar 10-20, a bar 20-30 and a triangle 30-40-50
// (node 30 is shared by a bar and the triangle).
fem::core::Mesh mesh()
{
    fem::core::MeshBuilder builder;
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(
            builder.addNode(fem::core::ExternalId{10} * i, Eigen::Vector3d(i, 0, 0)).has_value());
    }
    REQUIRE(builder.addElement(ElementType::Line2, 1, std::vector<fem::core::ExternalId>{10, 20})
                .has_value());
    REQUIRE(builder.addElement(ElementType::Line2, 2, std::vector<fem::core::ExternalId>{20, 30})
                .has_value());
    REQUIRE(builder.addElement(ElementType::Tri3, 3, std::vector<fem::core::ExternalId>{30, 40, 50})
                .has_value());
    auto built = builder.build();
    REQUIRE(built.has_value());
    return std::move(*built);
}

fem::core::NodeIndex node(const fem::core::Mesh& m, fem::core::ExternalId id)
{
    const auto found = m.findNode(id);
    REQUIRE(found.has_value());
    return found.value_or(0);
}

ElementIndex element(const fem::core::Mesh& m, fem::core::ExternalId id)
{
    const auto found = m.findElement(id);
    REQUIRE(found.has_value());
    return found.value_or(0);
}

} // namespace

// REQ-006-08, REQ-006-09: union of the dofs per node, numbered node by node.
TEST_CASE("dofs per node come from the elements", "[core][dofs]")
{
    const fem::core::Mesh m = mesh();
    const Declares bar(fem::core::mask(DofType::U) | fem::core::mask(DofType::V));
    const Declares heat(fem::core::mask(DofType::T));
    const std::vector<ElementIndex> bars = {element(m, 1), element(m, 2)};
    const std::vector<ElementIndex> plates = {element(m, 3)};
    const std::vector<ElementSet> sets = {{.elements = bars, .formulation = &bar},
                                          {.elements = plates, .formulation = &heat}};
    const DofMap dofs = DofMap::build(m, sets);

    // 10, 20: u v; 30: u v T; 40, 50: T.
    CHECK(dofs.size() == 2 + 2 + 3 + 1 + 1);
    CHECK(dofs.maskOf(node(m, 30)) == (fem::core::mask(DofType::U) | fem::core::mask(DofType::V) |
                                       fem::core::mask(DofType::T)));
    CHECK_FALSE(dofs.find(node(m, 40), DofType::U).has_value());

    // Node by node, in DofType order within a node.
    REQUIRE(dofs.find(node(m, 30), DofType::T).has_value());
    const auto t30 = dofs.find(node(m, 30), DofType::T).value_or(-1);
    CHECK(t30 == 6);
    CHECK(dofs.nodeOf(t30) == node(m, 30));
    CHECK(dofs.typeOf(t30) == DofType::T);
    CHECK(dofs.find(node(m, 30), DofType::U) == std::optional<fem::core::DofIndex>(4));
    CHECK(dofs.firstDof(node(m, 40)) == 7);

    // The same input gives the same numbering.
    const DofMap again = DofMap::build(m, sets);
    for (fem::core::DofIndex d = 0; d < dofs.size(); ++d) {
        CHECK(again.nodeOf(d) == dofs.nodeOf(d));
        CHECK(again.typeOf(d) == dofs.typeOf(d));
    }
}

// REQ-006-10, REQ-006-11: prescribed dofs leave the equations; unknown dofs
// are refused.
TEST_CASE("prescribed dofs are eliminated", "[core][dofs]")
{
    const fem::core::Mesh m = mesh();
    const Declares bar(fem::core::mask(DofType::U) | fem::core::mask(DofType::V));
    const std::vector<ElementIndex> bars = {element(m, 1), element(m, 2)};
    const std::vector<ElementSet> sets = {{.elements = bars, .formulation = &bar}};
    DofMap dofs = DofMap::build(m, sets);
    CHECK(dofs.size() == 6);
    CHECK(dofs.freeCount() == 6);

    REQUIRE(dofs.prescribe(node(m, 10), DofType::U, 0.0).has_value());
    REQUIRE(dofs.prescribe(node(m, 10), DofType::V, 0.0).has_value());
    REQUIRE(dofs.prescribe(node(m, 30), DofType::V, -0.01).has_value());
    CHECK(dofs.freeCount() == 3);

    REQUIRE(dofs.find(node(m, 30), DofType::V).has_value());
    const auto v30 = dofs.find(node(m, 30), DofType::V).value_or(-1);
    CHECK(dofs.isPrescribed(v30));
    CHECK(dofs.prescribedValue(v30) == -0.01);
    CHECK(dofs.equation(v30) == -1);
    // Free dofs numbered in order: u20, v20, u30.
    CHECK(dofs.equation(dofs.find(node(m, 20), DofType::U).value_or(0)) == 0);
    CHECK(dofs.equation(dofs.find(node(m, 30), DofType::U).value_or(0)) == 2);

    // Node 40 has no dofs (no element of the set uses it); w does not exist.
    const auto missing = dofs.prescribe(node(m, 40), DofType::U, 0.0);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message == "Node 40 has no dof u.");
    CHECK_FALSE(dofs.prescribe(node(m, 10), DofType::W, 0.0).has_value());
}
