#pragma once

// Feature 010 (design §5.5, REQ-010-21): the problems of the check of the
// model as the check dock shows them, without widgets.

#include <fem/core/mesh.hpp>
#include <fem/setup/build.hpp>

#include <QString>

#include <string_view>
#include <vector>

namespace fem::app {

// The text of a problem of the build, naming what its detail names.
[[nodiscard]] QString issueText(const setup::BuildIssue& issue);

// REQ-010-21: the elements and nodes a message of core::validate names, by
// their external ids: "element 12", "node 5", "Elements without an
// assignment: 3, 4 and 2 more" (the count is not an id).
struct NamedIds
{
    std::vector<core::ExternalId> elements;
    std::vector<core::ExternalId> nodes;
};

[[nodiscard]] NamedIds idsInMessage(std::string_view message);

} // namespace fem::app
