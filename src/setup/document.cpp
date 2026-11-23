#include <fem/setup/document.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace fem::setup {

namespace {

template <typename T>
concept HasTargets = requires(T item) { item.targets; };

std::uint64_t key(ItemId id)
{
    return static_cast<std::uint64_t>(id);
}

// The position of `id` in items sorted by id, or where it goes.
auto positionOf(std::vector<Item>& items, ItemId id)
{
    return std::ranges::lower_bound(items, key(id), {},
                                    [](const Item& item) { return key(idOf(item)); });
}

auto positionOf(const std::vector<Item>& items, ItemId id)
{
    return std::ranges::lower_bound(items, key(id), {},
                                    [](const Item& item) { return key(idOf(item)); });
}

} // namespace

ItemId idOf(const Item& item)
{
    return std::visit([](const auto& i) { return i.id; }, item);
}

const Targets* targetsOf(const Item& item)
{
    return std::visit(
        [](const auto& i) -> const Targets* {
            if constexpr (HasTargets<std::decay_t<decltype(i)>>) {
                return &i.targets;
            } else {
                return nullptr;
            }
        },
        item);
}

Targets* targetsOf(Item& item)
{
    return std::visit(
        [](auto& i) -> Targets* {
            if constexpr (HasTargets<std::decay_t<decltype(i)>>) {
                return &i.targets;
            } else {
                return nullptr;
            }
        },
        item);
}

ItemId Document::nextId() noexcept
{
    return ItemId{++m_lastId};
}

const Item* Document::find(ItemId id) const
{
    const auto it = positionOf(m_items, id);
    return it != m_items.end() && idOf(*it) == id ? &*it : nullptr;
}

void Document::apply(const DocumentEdit& edit)
{
    for (const Item& item : edit.removed) {
        erase(idOf(item));
    }
    for (const Item& item : edit.added) {
        insert(item);
    }
    if (edit.units) {
        m_units = edit.units->second;
    }
    if (edit.activeCase) {
        m_activeCase = edit.activeCase->second;
    }
    ++m_revision;
}

void Document::revert(const DocumentEdit& edit)
{
    for (const Item& item : edit.added) {
        erase(idOf(item));
    }
    for (const Item& item : edit.removed) {
        insert(item);
    }
    if (edit.units) {
        m_units = edit.units->first;
    }
    if (edit.activeCase) {
        m_activeCase = edit.activeCase->first;
    }
    ++m_revision;
}

void Document::insert(const Item& item)
{
    const ItemId id = idOf(item);
    const auto it = positionOf(m_items, id);
    if (it != m_items.end() && idOf(*it) == id) {
        *it = item;
    } else {
        m_items.insert(it, item);
    }
    // Ids coming back with an undo never lower the counter.
    m_lastId = std::max(m_lastId, key(id));
}

void Document::erase(ItemId id)
{
    const auto it = positionOf(m_items, id);
    if (it == m_items.end() || idOf(*it) != id) {
        // An edit applied to a document it was not made for.
        throw std::logic_error("setup: no item " + std::to_string(key(id)) + " to take out");
    }
    m_items.erase(it);
}

} // namespace fem::setup
