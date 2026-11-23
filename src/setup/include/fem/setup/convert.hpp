#pragma once

// Feature 010: every value of the document from one unit system to another
// (design 010 §5.1, REQ-010-03).

#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

namespace fem::setup {

// Converts every value of `item` that has a unit, field by field: the
// material, the section and its shape, the assignment (none: directions and
// angles), the beam details, the supports (none) and every action of a
// load. Load cases and combinations have nothing to convert.
void convertItem(Item& item, UnitSystem from, UnitSystem to);

} // namespace fem::setup
