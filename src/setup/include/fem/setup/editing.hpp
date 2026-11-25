#pragma once

// Feature 010: the edits of the document, each one DocumentEdit, one undo
// step (design 010 §4, §5.2; REQ-010-23). They build the edit; the caller
// applies it (and keeps it for undo).

#include <fem/setup/analysis_settings.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

#include <tl/expected.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fem::setup {

// Why an edit was refused.
struct EditRefusal
{
    enum class Kind : std::uint8_t {
        InUse,         // REQ-010-04, 05, 18: `items` use it
        DuplicateName, // `items` already has the name
        NotFound,      // no item with that id
        NoTargets,     // an assignment or a load without targets
        Unsupported    // not for this kind of item (duplicating an assignment)
    };
    Kind kind = Kind::NotFound;
    std::vector<ItemId> items;
};

using EditResult = tl::expected<DocumentEdit, EditRefusal>;

// The name of a material, section, load case or combination; empty for the
// other items. Names are unique within each of these kinds.
[[nodiscard]] std::string nameOf(const Item& item);
// `base` if no item of the kind of `item` has it, else "base 2", "base 3"...
[[nodiscard]] std::string freeName(const Document& document, const Item& item,
                                   const std::string& base);

// A new item with a fresh id (the one in `item` is ignored). The first load
// case becomes the active one.
[[nodiscard]] EditResult addItem(Document& document, Item item);
// The item with the id of `item`, replaced. Renaming a material or a
// section renames it in the assignments that use it, in the same edit.
[[nodiscard]] EditResult changeItem(const Document& document, const Item& item);
// REQ-010-04, 05, 18: refused while used (a material or section by an
// assignment, a load case by a combination); a load case goes with its
// loads, and the active case moves to the first other one.
[[nodiscard]] EditResult removeItem(const Document& document, ItemId id);
// A copy named "<name> (copy)" (a load case with copies of its loads); a
// load is copied in its case. Assignments, details and supports are
// refused (Unsupported): two of a kind on the same targets make no sense.
[[nodiscard]] EditResult duplicateItem(Document& document, ItemId id);
// REQ-010-07, 08, 15: an assignment, beam detail or support on its
// targets, taken out of the items of the same kind on those targets (an
// item left without targets goes). An end connection for both ends becomes
// one item per end; one per end replaces only that end.
[[nodiscard]] EditResult assign(Document& document, Item item);
// REQ-010-18: copies of loads in another load case.
[[nodiscard]] EditResult copyLoads(Document& document, std::span<const ItemId> loads,
                                   ItemId toCase);
// REQ-010-16: the load case or combination whose loads are shown.
[[nodiscard]] EditResult setActiveCase(const Document& document, ItemId id);
// REQ-010-03: every value converted to `to`, as one edit; empty when the
// document is already in `to`.
[[nodiscard]] DocumentEdit changeUnits(const Document& document, UnitSystem to);
// REQ-011-01: the type and options of the analysis; empty when the document
// already has them. Nothing is converted.
[[nodiscard]] DocumentEdit changeAnalysis(const Document& document,
                                          const AnalysisSettings& settings);

} // namespace fem::setup
