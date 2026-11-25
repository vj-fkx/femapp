#include "check_issues.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Feature 010, T22: REQ-010-21. The texts of the check and the elements
// and nodes a problem of the core names.

namespace {

using Ids = std::vector<fem::core::ExternalId>;

} // namespace

TEST_CASE("the elements and nodes a problem of the core names", "[app][check]")
{
    const auto one = fem::app::idsInMessage("The element 12 has zero length.");
    CHECK(one.elements == Ids{12});
    CHECK(one.nodes.empty());

    const auto list = fem::app::idsInMessage("Elements without an assignment: 3, 4, 7 and 2 more.");
    CHECK(list.elements == Ids{3, 4, 7});

    const auto both = fem::app::idsInMessage(
        "Assignment 2: the element 40 is not a beam; node 9 has no rotation.");
    CHECK(both.elements == Ids{40});
    CHECK(both.nodes == Ids{9});

    // Other numbers are not ids: an item number, a count, a type.
    const auto none = fem::app::idsInMessage("Assignment 2: no material named \"S355\".");
    CHECK(none.elements.empty());
    CHECK(none.nodes.empty());
    const auto typed = fem::app::idsInMessage("The element 5 is a Tri3, not a 2-node line.");
    CHECK(typed.elements == Ids{5});
}

// REQ-011-18: the instability messages of the core name the node and the
// elements involved; a count, an iteration and a dof are not ids.
TEST_CASE("the elements and nodes an instability message names", "[app][check]")
{
    const auto mechanism = fem::app::idsInMessage(
        "The structure is a mechanism: the node 12 in v can move without resistance, with "
        "the elements 3, 4, 7 and 2 more (a support may be missing).");
    CHECK(mechanism.nodes == Ids{12});
    CHECK(mechanism.elements == Ids{3, 4, 7});

    const auto diagonal = fem::app::idsInMessage(
        "The structure is unstable: no bar gives stiffness to\n"
        "- the node 3 in w (elements 2, 5)\n"
        "- the node 8 in rz (element 6)\n"
        "(A rotation without stiffness: restrain it, or keep one beam at the node connected.)");
    CHECK(diagonal.nodes == Ids{3, 8});
    CHECK(diagonal.elements == Ids{2, 5, 6});

    const auto secondOrder = fem::app::idsInMessage(
        "The structure is unstable under these loads: the second-order stiffness lost its "
        "stability at iteration 4: the node 7 in u moves the most, and the elements 2, 3 "
        "drive it.");
    CHECK(secondOrder.nodes == Ids{7});
    CHECK(secondOrder.elements == Ids{2, 3});

    const auto inside = fem::app::idsInMessage(
        "The structure is unstable under these loads: the second-order stiffness lost its "
        "stability at iteration 2 (inside the element 5, buckling in its local y direction).");
    CHECK(inside.nodes.empty());
    CHECK(inside.elements == Ids{5});
}

TEST_CASE("the text of each problem of the build", "[app][check]")
{
    using K = fem::setup::IssueKind;
    for (const K kind :
         {K::NoMesh, K::MeshOutOfDate, K::MissingEntity, K::EntityNotMeshed, K::MissingGroup,
          K::StaleSelection, K::MissingElement, K::MissingNode, K::UnknownMaterial,
          K::UnknownSection, K::UnknownLoad, K::SettlementNotHeld, K::TrussBuckling}) {
        const fem::setup::BuildIssue issue{
            .severity = fem::setup::Severity::Error, .kind = kind, .item = {}, .detail = "X1"};
        CHECK_FALSE(fem::app::issueText(issue).isEmpty());
    }
    const fem::setup::BuildIssue missing{.severity = fem::setup::Severity::Error,
                                         .kind = K::UnknownMaterial,
                                         .item = {},
                                         .detail = "S355"};
    CHECK(fem::app::issueText(missing).contains(QStringLiteral("S355")));
    // A problem of the core: its own text.
    const fem::setup::BuildIssue core{.severity = fem::setup::Severity::Error,
                                      .kind = K::Core,
                                      .item = {},
                                      .detail = "The element 3 has zero length."};
    CHECK(fem::app::issueText(core) == QStringLiteral("The element 3 has zero length."));
}
