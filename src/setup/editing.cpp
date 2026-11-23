#include <fem/setup/convert.hpp>
#include <fem/setup/editing.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
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

bool sameKind(const Item& a, const Item& b)
{
    return a.index() == b.index();
}

void setId(Item& item, ItemId id)
{
    std::visit([id](auto& i) { i.id = id; }, item);
}

void setName(Item& item, const std::string& name)
{
    std::visit(
        [&](auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, MaterialItem> || std::is_same_v<T, SectionItem>) {
                i.data.name = name;
            } else if constexpr (std::is_same_v<T, LoadCase> || std::is_same_v<T, Combination>) {
                i.name = name;
            }
        },
        item);
}

bool named(const Item& item)
{
    return std::holds_alternative<MaterialItem>(item) ||
           std::holds_alternative<SectionItem>(item) || std::holds_alternative<LoadCase>(item) ||
           std::holds_alternative<Combination>(item);
}

// The item of the kind of `item` with its name, other than itself.
std::optional<ItemId> nameTaken(const Document& document, const Item& item)
{
    if (!named(item)) {
        return std::nullopt;
    }
    const std::string name = nameOf(item);
    for (const Item& other : document.items()) {
        if (sameKind(other, item) && idOf(other) != idOf(item) && nameOf(other) == name) {
            return idOf(other);
        }
    }
    return std::nullopt;
}

tl::unexpected<EditRefusal> refuse(EditRefusal::Kind kind, std::vector<ItemId> items = {})
{
    return tl::unexpected(EditRefusal{.kind = kind, .items = std::move(items)});
}

// The kinds assign() works on.
bool assignable(const Item& item)
{
    return std::holds_alternative<BarAssignment>(item) ||
           std::holds_alternative<ConnectionItem>(item) ||
           std::holds_alternative<OffsetItem>(item) ||
           std::holds_alternative<FoundationItem>(item) ||
           std::holds_alternative<WarpingEndItem>(item) ||
           std::holds_alternative<SupportItem>(item);
}

// Whether `existing` competes with `fresh` for the same targets: the same
// kind and, for end connections, the same end.
bool competes(const Item& existing, const Item& fresh)
{
    if (!sameKind(existing, fresh)) {
        return false;
    }
    if (const auto* a = std::get_if<ConnectionItem>(&existing)) {
        return a->data.end == std::get<ConnectionItem>(fresh).data.end;
    }
    return true;
}

std::string groupKey(const GroupTarget& group)
{
    return std::to_string(group.dimension) + ':' + group.name;
}

// The targets of a new item, as sets for taking them out of other items.
class TargetSets
{
public:
    explicit TargetSets(const Targets& t)
        : m_generation(t.meshGeneration)
    {
        for (const geom::EntityId e : t.entities) {
            m_entities.insert(static_cast<std::uint64_t>(e));
        }
        for (const GroupTarget& g : t.groups) {
            m_groups.insert(groupKey(g));
        }
        m_elements.insert(t.elements.begin(), t.elements.end());
        m_nodes.insert(t.nodes.begin(), t.nodes.end());
    }

    // `t` without these targets; picked elements and nodes only from the
    // same mesh generation (the others are stale anyway).
    [[nodiscard]] Targets without(const Targets& t) const
    {
        Targets out;
        out.meshGeneration = t.meshGeneration;
        for (const geom::EntityId e : t.entities) {
            if (!m_entities.contains(static_cast<std::uint64_t>(e))) {
                out.entities.push_back(e);
            }
        }
        for (const GroupTarget& g : t.groups) {
            if (!m_groups.contains(groupKey(g))) {
                out.groups.push_back(g);
            }
        }
        const bool same = t.meshGeneration == m_generation;
        for (const core::ExternalId e : t.elements) {
            if (!same || !m_elements.contains(e)) {
                out.elements.push_back(e);
            }
        }
        for (const core::ExternalId n : t.nodes) {
            if (!same || !m_nodes.contains(n)) {
                out.nodes.push_back(n);
            }
        }
        return out;
    }

private:
    std::unordered_set<std::uint64_t> m_entities;
    std::unordered_set<std::string> m_groups;
    std::unordered_set<core::ExternalId> m_elements;
    std::unordered_set<core::ExternalId> m_nodes;
    std::uint64_t m_generation = 0;
};

// The first load case other than `except`, if any.
std::optional<ItemId> otherCase(const Document& document, ItemId except)
{
    for (const Item& item : document.items()) {
        if (std::holds_alternative<LoadCase>(item) && idOf(item) != except) {
            return idOf(item);
        }
    }
    return std::nullopt;
}

} // namespace

std::string nameOf(const Item& item)
{
    return std::visit(
        [](const auto& i) -> std::string {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, MaterialItem> || std::is_same_v<T, SectionItem>) {
                return i.data.name;
            } else if constexpr (std::is_same_v<T, LoadCase> || std::is_same_v<T, Combination>) {
                return i.name;
            } else {
                return {};
            }
        },
        item);
}

std::string freeName(const Document& document, const Item& item, const std::string& base)
{
    const auto taken = [&](const std::string& name) {
        return std::ranges::any_of(document.items(), [&](const Item& other) {
            return sameKind(other, item) && nameOf(other) == name;
        });
    };
    if (!taken(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        std::string name = base + ' ' + std::to_string(n);
        if (!taken(name)) {
            return name;
        }
    }
}

EditResult addItem(Document& document, Item item)
{
    if (const auto* load = std::get_if<LoadItem>(&item)) {
        if (load->targets.empty() && !std::holds_alternative<SelfWeight>(load->action)) {
            return refuse(EditRefusal::Kind::NoTargets);
        }
        const Item* loadCase = document.find(load->loadCase);
        if (loadCase == nullptr || !std::holds_alternative<LoadCase>(*loadCase)) {
            return refuse(EditRefusal::Kind::NotFound, {load->loadCase});
        }
    }
    setId(item, ItemId{});
    if (const auto taken = nameTaken(document, item)) {
        return refuse(EditRefusal::Kind::DuplicateName, {*taken});
    }
    const ItemId id = document.nextId();
    setId(item, id);
    DocumentEdit edit;
    // The first load case is the active one.
    if (std::holds_alternative<LoadCase>(item) && !document.activeCase()) {
        edit.activeCase = std::pair{std::optional<ItemId>{}, std::optional<ItemId>{id}};
    }
    edit.added.push_back(std::move(item));
    return edit;
}

EditResult changeItem(const Document& document, const Item& item)
{
    const Item* current = document.find(idOf(item));
    if (current == nullptr || !sameKind(*current, item)) {
        return refuse(EditRefusal::Kind::NotFound);
    }
    if (const auto taken = nameTaken(document, item)) {
        return refuse(EditRefusal::Kind::DuplicateName, {*taken});
    }
    DocumentEdit edit;
    edit.removed.push_back(*current);
    edit.added.push_back(item);
    // A renamed material or section, also in the assignments that use it.
    const std::string before = nameOf(*current);
    const std::string after = nameOf(item);
    const bool material = std::holds_alternative<MaterialItem>(item);
    const bool section = std::holds_alternative<SectionItem>(item);
    if ((material || section) && before != after) {
        for (const Item& other : document.items()) {
            const auto* bars = std::get_if<BarAssignment>(&other);
            if (bars == nullptr ||
                (material ? bars->data.material : bars->data.section) != before) {
                continue;
            }
            BarAssignment renamed = *bars;
            (material ? renamed.data.material : renamed.data.section) = after;
            edit.removed.push_back(other);
            edit.added.emplace_back(std::move(renamed));
        }
    }
    return edit;
}

EditResult removeItem(const Document& document, ItemId id)
{
    const Item* item = document.find(id);
    if (item == nullptr) {
        return refuse(EditRefusal::Kind::NotFound);
    }
    std::vector<ItemId> uses;
    const std::string name = nameOf(*item);
    for (const Item& other : document.items()) {
        if (const auto* bars = std::get_if<BarAssignment>(&other)) {
            if ((std::holds_alternative<MaterialItem>(*item) && bars->data.material == name) ||
                (std::holds_alternative<SectionItem>(*item) && bars->data.section == name)) {
                uses.push_back(bars->id);
            }
        } else if (const auto* combination = std::get_if<Combination>(&other)) {
            if (std::holds_alternative<LoadCase>(*item) &&
                std::ranges::any_of(combination->terms,
                                    [id](const auto& term) { return term.first == id; })) {
                uses.push_back(combination->id);
            }
        }
    }
    if (!uses.empty()) {
        return refuse(EditRefusal::Kind::InUse, std::move(uses));
    }
    DocumentEdit edit;
    edit.removed.push_back(*item);
    if (std::holds_alternative<LoadCase>(*item)) {
        for (const Item& other : document.items()) {
            const auto* load = std::get_if<LoadItem>(&other);
            if (load != nullptr && load->loadCase == id) {
                edit.removed.push_back(other);
            }
        }
    }
    if (document.activeCase() == id) {
        edit.activeCase = std::pair{document.activeCase(), otherCase(document, id)};
    }
    return edit;
}

EditResult duplicateItem(Document& document, ItemId id)
{
    const Item* found = document.find(id);
    if (found == nullptr) {
        return refuse(EditRefusal::Kind::NotFound);
    }
    const Item original = *found;
    if (assignable(original)) {
        return refuse(EditRefusal::Kind::Unsupported);
    }
    Item copy = original;
    if (named(copy)) {
        setName(copy, freeName(document, copy, nameOf(original) + " (copy)"));
    }
    const ItemId copyId = document.nextId();
    setId(copy, copyId);
    DocumentEdit edit;
    edit.added.push_back(std::move(copy));
    if (std::holds_alternative<LoadCase>(original)) {
        for (const Item& other : document.items()) {
            const auto* load = std::get_if<LoadItem>(&other);
            if (load != nullptr && load->loadCase == id) {
                LoadItem loadCopy = *load;
                loadCopy.id = document.nextId();
                loadCopy.loadCase = copyId;
                edit.added.emplace_back(std::move(loadCopy));
            }
        }
    }
    return edit;
}

EditResult assign(Document& document, Item item)
{
    if (!assignable(item)) {
        return refuse(EditRefusal::Kind::Unsupported);
    }
    const Targets* targets = targetsOf(item);
    if (targets == nullptr || targets->empty()) {
        return refuse(EditRefusal::Kind::NoTargets);
    }
    // An end connection for both ends: one item per end.
    std::vector<Item> fresh;
    if (const auto* connection = std::get_if<ConnectionItem>(&item);
        connection != nullptr && connection->data.end == core::BarEnd::Both) {
        ConnectionItem start = *connection;
        start.data.end = core::BarEnd::Start;
        ConnectionItem end = *connection;
        end.data.end = core::BarEnd::End;
        fresh.emplace_back(std::move(start));
        fresh.emplace_back(std::move(end));
    } else {
        fresh.push_back(std::move(item));
    }

    // The items this edit changes, as it leaves them.
    std::vector<Item> changed;
    for (Item& newItem : fresh) {
        const TargetSets sets(*targetsOf(newItem));
        for (const Item& existing : document.items()) {
            if (!competes(existing, newItem)) {
                continue;
            }
            const auto earlier = std::ranges::find_if(
                changed, [&](const Item& c) { return idOf(c) == idOf(existing); });
            const Item& now = earlier != changed.end() ? *earlier : existing;
            Targets left = sets.without(*targetsOf(now));
            if (left == *targetsOf(now)) {
                continue;
            }
            Item updated = now;
            *targetsOf(updated) = std::move(left);
            if (earlier != changed.end()) {
                *earlier = std::move(updated);
            } else {
                changed.push_back(std::move(updated));
            }
        }
        setId(newItem, document.nextId());
    }
    DocumentEdit edit;
    for (const Item& after : changed) {
        edit.removed.push_back(*document.find(idOf(after)));
        // An item left without targets goes.
        if (!targetsOf(after)->empty()) {
            edit.added.push_back(after);
        }
    }
    for (Item& newItem : fresh) {
        edit.added.push_back(std::move(newItem));
    }
    return edit;
}

EditResult copyLoads(Document& document, std::span<const ItemId> loads, ItemId toCase)
{
    const Item* target = document.find(toCase);
    if (target == nullptr || !std::holds_alternative<LoadCase>(*target)) {
        return refuse(EditRefusal::Kind::NotFound, {toCase});
    }
    DocumentEdit edit;
    for (const ItemId id : loads) {
        const Item* found = document.find(id);
        if (found == nullptr || !std::holds_alternative<LoadItem>(*found)) {
            return refuse(EditRefusal::Kind::NotFound, {id});
        }
        LoadItem copy = std::get<LoadItem>(*found);
        copy.id = document.nextId();
        copy.loadCase = toCase;
        edit.added.emplace_back(std::move(copy));
    }
    return edit;
}

EditResult setActiveCase(const Document& document, ItemId id)
{
    const Item* found = document.find(id);
    if (found == nullptr || !(std::holds_alternative<LoadCase>(*found) ||
                              std::holds_alternative<Combination>(*found))) {
        return refuse(EditRefusal::Kind::NotFound, {id});
    }
    DocumentEdit edit;
    edit.activeCase = std::pair{document.activeCase(), std::optional<ItemId>{id}};
    return edit;
}

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
