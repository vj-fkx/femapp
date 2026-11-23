#include <fem/setup/convert.hpp>
#include <fem/setup/editing.hpp>

#include <type_traits>
#include <variant>

namespace fem::setup {

namespace {

// The items that hold values with a unit.
bool hasUnits(const Item& item)
{
    return std::visit(
        [](const auto& i) {
            using T = std::decay_t<decltype(i)>;
            return std::is_same_v<T, MaterialItem> || std::is_same_v<T, SectionItem> ||
                   std::is_same_v<T, ConnectionItem> || std::is_same_v<T, OffsetItem> ||
                   std::is_same_v<T, FoundationItem> || std::is_same_v<T, WarpingEndItem> ||
                   std::is_same_v<T, LoadItem>;
        },
        item);
}

} // namespace

DocumentEdit changeUnits(const Document& document, UnitSystem to)
{
    DocumentEdit edit;
    const UnitSystem from = document.units();
    if (from == to) {
        return edit;
    }
    // The values before are kept, so that undoing restores what was typed
    // instead of converting back (design §5.1).
    for (const Item& item : document.items()) {
        if (!hasUnits(item)) {
            continue;
        }
        edit.removed.push_back(item);
        Item converted = item;
        convertItem(converted, from, to);
        edit.added.push_back(std::move(converted));
    }
    edit.units = std::pair{from, to};
    return edit;
}

} // namespace fem::setup
