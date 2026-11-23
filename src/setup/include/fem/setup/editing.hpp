#pragma once

// Feature 010: the edits of the document, each one DocumentEdit, one undo
// step (design 010 §4, §5.2; REQ-010-23).

#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

namespace fem::setup {

// REQ-010-03: every value converted to `to`, as one edit; empty when the
// document is already in `to`.
[[nodiscard]] DocumentEdit changeUnits(const Document& document, UnitSystem to);

} // namespace fem::setup
