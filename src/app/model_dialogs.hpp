#pragma once

// Feature 010 (design §5.5): the dialogs of the Model menu that make items:
// a material from the library, a section of any shape, the assignment of
// bars.

#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

class QWidget;

namespace fem::app {

// REQ-010-04: a material of the library, by standard and grade, in
// `units`, or an empty material of the user's; none when cancelled.
[[nodiscard]] std::optional<setup::MaterialItem> chooseMaterial(QWidget* parent,
                                                                setup::UnitSystem units);

// REQ-010-05: the shape of a new section, as newSection() takes it (-1 for
// a generic section); none when cancelled.
[[nodiscard]] std::optional<int> chooseSectionShape(QWidget* parent);

// REQ-010-07, 12: an assignment of bars. `targets` comes from the viewport
// selection; when it is empty the dialog offers `groups` of the mesh, and
// the ones checked are the targets.
struct BarChoice
{
    core::Assignment data;
    std::vector<setup::GroupTarget> groups;
};

[[nodiscard]] std::optional<BarChoice> chooseBars(QWidget* parent, const setup::Document& document,
                                                  const setup::Targets& targets,
                                                  std::span<const setup::GroupTarget> groups);

} // namespace fem::app
