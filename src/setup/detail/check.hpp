#pragma once

#include <fem/setup/build.hpp>

namespace fem::setup::detail {

// REQ-010-21: appends the issues of core::validate on the model built (kind
// Core, without an item). Shared by checkModel and runCase, which builds once.
void addCoreIssues(BuildResult& built);

} // namespace fem::setup::detail
