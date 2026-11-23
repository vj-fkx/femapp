#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// Feature 010, T06 (design §5.2): REQ-010-04, 05, 07, 08, 15 to 18, 23.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::EntityId;
using s::Document;
using s::EditRefusal;
using s::ItemId;

// Applies an edit that must not be refused.
s::DocumentEdit applied(Document& document, const s::EditResult& result)
{
    REQUIRE(result.has_value());
    document.apply(*result);
    return *result;
}

ItemId lastId(const Document& document)
{
    return s::idOf(document.items().back());
}

s::Targets entities(std::initializer_list<std::uint64_t> ids)
{
    s::Targets t;
    for (const std::uint64_t id : ids) {
        t.entities.push_back(EntityId{id});
    }
    return t;
}

template <typename T>
std::vector<const T*> all(const Document& document)
{
    std::vector<const T*> out;
    for (const s::Item& item : document.items()) {
        if (const auto* i = std::get_if<T>(&item)) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<std::uint64_t> ids(const s::Targets& targets)
{
    std::vector<std::uint64_t> out;
    out.reserve(targets.entities.size());
    for (const EntityId e : targets.entities) {
        out.push_back(static_cast<std::uint64_t>(e));
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

TEST_CASE("items are made, changed and duplicated", "[setup][editing]")
{
    Document document;
    applied(document, s::addItem(document, s::MaterialItem{.id = {}, .data = {.name = "steel"}}));
    const ItemId steel = lastId(document);
    CHECK(static_cast<std::uint64_t>(steel) != 0);

    // A second material with the same name is refused; freeName offers one.
    const auto again = s::addItem(document, s::MaterialItem{.id = {}, .data = {.name = "steel"}});
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().kind == EditRefusal::Kind::DuplicateName);
    CHECK(again.error().items == std::vector<ItemId>{steel});
    CHECK(s::freeName(document, s::MaterialItem{}, "steel") == "steel 2");
    CHECK(s::freeName(document, s::MaterialItem{}, "concrete") == "concrete");
    // A section may share the name of a material.
    CHECK(s::addItem(document, s::SectionItem{.id = {}, .data = {.name = "steel"}}).has_value());

    // Changed in place.
    applied(document, s::changeItem(document, s::MaterialItem{.id = steel,
                                                              .data = {.name = "steel",
                                                                       .youngsModulus = 2.1e8}}));
    CHECK(std::get<s::MaterialItem>(*document.find(steel)).data.youngsModulus == 2.1e8);
    CHECK_FALSE(s::changeItem(document, s::MaterialItem{.id = ItemId{99}, .data = {}}).has_value());

    // Duplicated with a new name.
    applied(document, s::duplicateItem(document, steel));
    const auto copy = std::get<s::MaterialItem>(document.items().back());
    CHECK(copy.data.name == "steel (copy)");
    CHECK(copy.data.youngsModulus == 2.1e8);
    CHECK(copy.id != steel);
}

TEST_CASE("renaming a material or section renames it where it is used", "[setup][editing]")
{
    Document document;
    applied(document, s::addItem(document, s::MaterialItem{.id = {}, .data = {.name = "steel"}}));
    const ItemId steel = lastId(document);
    applied(document, s::addItem(document, s::SectionItem{.id = {}, .data = {.name = "IPE"}}));
    const ItemId ipe = lastId(document);
    applied(document,
            s::assign(document, s::BarAssignment{.id = {},
                                                 .targets = entities({1, 2}),
                                                 .data = {.material = "steel", .section = "IPE"}}));
    const auto rename = applied(
        document, s::changeItem(document, s::MaterialItem{.id = steel, .data = {.name = "S355"}}));
    CHECK(all<s::BarAssignment>(document).front()->data.material == "S355");
    applied(document,
            s::changeItem(document, s::SectionItem{.id = ipe, .data = {.name = "IPE 300"}}));
    CHECK(all<s::BarAssignment>(document).front()->data.section == "IPE 300");
    // One edit: undone together.
    document.revert(rename);
    CHECK(all<s::BarAssignment>(document).front()->data.material == "steel");
}

// REQ-010-04, 05, 18
TEST_CASE("items in use cannot be removed", "[setup][editing]")
{
    Document document;
    applied(document, s::addItem(document, s::MaterialItem{.id = {}, .data = {.name = "steel"}}));
    const ItemId steel = lastId(document);
    applied(document, s::addItem(document, s::SectionItem{.id = {}, .data = {.name = "IPE"}}));
    const ItemId ipe = lastId(document);
    applied(document,
            s::assign(document, s::BarAssignment{.id = {},
                                                 .targets = entities({1}),
                                                 .data = {.material = "steel", .section = "IPE"}}));
    const ItemId bars = lastId(document);
    for (const ItemId used : {steel, ipe}) {
        const auto refused = s::removeItem(document, used);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().kind == EditRefusal::Kind::InUse);
        CHECK(refused.error().items == std::vector<ItemId>{bars});
    }
    // Free once nothing uses them.
    applied(document, s::removeItem(document, bars));
    applied(document, s::removeItem(document, ipe));
    CHECK(document.find(ipe) == nullptr);
    CHECK_FALSE(s::removeItem(document, ItemId{99}).has_value());
}

TEST_CASE("load cases, their loads and combinations", "[setup][editing]")
{
    Document document;
    applied(document, s::addItem(document, s::LoadCase{.id = {}, .name = "G"}));
    const ItemId g = lastId(document);
    // The first case is the active one.
    CHECK(document.activeCase() == g);
    applied(document, s::addItem(document, s::LoadCase{.id = {}, .name = "Q"}));
    const ItemId q = lastId(document);
    CHECK(document.activeCase() == g);

    // Loads add up: two on the same entity stay two.
    for (int k = 0; k < 2; ++k) {
        applied(document,
                s::addItem(document, s::LoadItem{.id = {},
                                                 .loadCase = g,
                                                 .targets = entities({7}),
                                                 .action = c::NodalForce{.force = {0, 0, -10}}}));
    }
    applied(
        document,
        s::addItem(document,
                   s::LoadItem{.id = {}, .loadCase = q, .targets = {}, .action = s::SelfWeight{}}));
    CHECK(all<s::LoadItem>(document).size() == 3);
    // A nodal load needs targets; a self-weight does not.
    CHECK_FALSE(
        s::addItem(document,
                   s::LoadItem{.id = {}, .loadCase = g, .targets = {}, .action = c::NodalForce{}})
            .has_value());

    // Copied to another case.
    std::vector<ItemId> loadsOfG;
    for (const auto* load : all<s::LoadItem>(document)) {
        if (load->loadCase == g) {
            loadsOfG.push_back(load->id);
        }
    }
    applied(document, s::copyLoads(document, loadsOfG, q));
    CHECK(std::ranges::count_if(all<s::LoadItem>(document),
                                [&](const s::LoadItem* l) { return l->loadCase == q; }) == 3);

    // A combination uses G: G cannot go.
    applied(document,
            s::addItem(document,
                       s::Combination{.id = {}, .name = "ULS", .terms = {{g, 1.35}, {q, 1.5}}}));
    const ItemId uls = lastId(document);
    const auto refused = s::removeItem(document, g);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().items == std::vector<ItemId>{uls});

    // Without the combination, G goes with its loads, and Q becomes active.
    applied(document, s::removeItem(document, uls));
    const auto removeG = applied(document, s::removeItem(document, g));
    CHECK(document.find(g) == nullptr);
    CHECK(std::ranges::none_of(all<s::LoadItem>(document),
                               [&](const s::LoadItem* l) { return l->loadCase == g; }));
    CHECK(document.activeCase() == q);
    document.revert(removeG);
    CHECK(document.activeCase() == g);
    CHECK(std::ranges::count_if(all<s::LoadItem>(document),
                                [&](const s::LoadItem* l) { return l->loadCase == g; }) == 2);

    // The active case set by hand; a combination may be active.
    applied(document, s::setActiveCase(document, q));
    CHECK(document.activeCase() == q);
    CHECK_FALSE(s::setActiveCase(document, ItemId{999}).has_value());

    // A duplicated case brings copies of its loads.
    applied(document, s::duplicateItem(document, q));
    const auto cases = all<s::LoadCase>(document);
    const auto copied =
        std::ranges::find_if(cases, [](const s::LoadCase* lc) { return lc->name == "Q (copy)"; });
    REQUIRE(copied != cases.end());
    const ItemId copyId = (*copied)->id;
    CHECK(std::ranges::count_if(all<s::LoadItem>(document),
                                [&](const s::LoadItem* l) { return l->loadCase == copyId; }) == 3);
}

// REQ-010-07: a new assignment replaces the old one on its targets.
TEST_CASE("an assignment takes its targets from the older ones", "[setup][editing]")
{
    Document document;
    applied(document, s::assign(document, s::BarAssignment{.id = {},
                                                           .targets = entities({1, 2, 3}),
                                                           .data = {.section = "A"}}));
    const ItemId first = lastId(document);
    const auto second =
        applied(document, s::assign(document, s::BarAssignment{.id = {},
                                                               .targets = entities({2, 3, 4}),
                                                               .data = {.section = "B"}}));
    const auto bars = all<s::BarAssignment>(document);
    REQUIRE(bars.size() == 2);
    CHECK(bars[0]->id == first);
    CHECK(ids(bars[0]->targets) == std::vector<std::uint64_t>{1});
    CHECK(ids(bars[1]->targets) == std::vector<std::uint64_t>{2, 3, 4});
    // An item left without targets goes.
    applied(document,
            s::assign(document, s::BarAssignment{
                                    .id = {}, .targets = entities({1}), .data = {.section = "C"}}));
    CHECK(document.find(first) == nullptr);
    CHECK(all<s::BarAssignment>(document).size() == 2);
    // Other kinds are not touched.
    applied(document,
            s::assign(document, s::OffsetItem{.id = {}, .targets = entities({2}), .data = {}}));
    CHECK(all<s::BarAssignment>(document).size() == 2);
    // Undo of the second assignment gives the first its targets back.
    Document undo;
    applied(undo,
            s::assign(undo, s::BarAssignment{.id = {}, .targets = entities({1, 2}), .data = {}}));
    const auto take = applied(
        undo, s::assign(undo, s::BarAssignment{.id = {}, .targets = entities({2}), .data = {}}));
    undo.revert(take);
    REQUIRE(all<s::BarAssignment>(undo).size() == 1);
    CHECK(ids(all<s::BarAssignment>(undo).front()->targets) == std::vector<std::uint64_t>{1, 2});
    (void)second;
}

// REQ-010-08, 15: beam details by end, supports, and mesh selections.
TEST_CASE("details by end, supports and picked targets", "[setup][editing]")
{
    Document document;
    // Both ends: one item per end.
    c::EndConnection both;
    both.end = c::BarEnd::Both;
    both.stiffness[4] = 0.0; // a hinge in My
    applied(document,
            s::assign(document,
                      s::ConnectionItem{.id = {}, .targets = entities({1, 2}), .data = both}));
    auto connections = all<s::ConnectionItem>(document);
    REQUIRE(connections.size() == 2);
    CHECK(connections[0]->data.end == c::BarEnd::Start);
    CHECK(connections[1]->data.end == c::BarEnd::End);
    // A new start on bar 1 replaces only the start of bar 1.
    c::EndConnection start;
    start.end = c::BarEnd::Start;
    applied(document, s::assign(document, s::ConnectionItem{
                                              .id = {}, .targets = entities({1}), .data = start}));
    connections = all<s::ConnectionItem>(document);
    REQUIRE(connections.size() == 3);
    CHECK(ids(connections[0]->targets) == std::vector<std::uint64_t>{2});    // old start
    CHECK(ids(connections[1]->targets) == std::vector<std::uint64_t>{1, 2}); // old end
    CHECK(ids(connections[2]->targets) == std::vector<std::uint64_t>{1});

    // Supports: the new one replaces the dofs held on its targets.
    s::SupportItem fixed{.id = {}, .targets = entities({10, 11}), .held = {}};
    fixed.held.fill(true);
    applied(document, s::assign(document, fixed));
    s::SupportItem pinned{.id = {}, .targets = entities({11}), .held = {}};
    pinned.held[0] = pinned.held[1] = pinned.held[2] = true;
    applied(document, s::assign(document, pinned));
    const auto supports = all<s::SupportItem>(document);
    REQUIRE(supports.size() == 2);
    CHECK(ids(supports[0]->targets) == std::vector<std::uint64_t>{10});
    CHECK_FALSE(supports[1]->held[3]);

    // Picked nodes replace only nodes of the same mesh generation.
    s::Targets picked;
    picked.nodes = {5, 6};
    picked.meshGeneration = 1;
    applied(document,
            s::assign(document, s::SupportItem{.id = {}, .targets = picked, .held = fixed.held}));
    s::Targets later;
    later.nodes = {6};
    later.meshGeneration = 2;
    applied(document,
            s::assign(document, s::SupportItem{.id = {}, .targets = later, .held = fixed.held}));
    const auto all3 = all<s::SupportItem>(document);
    REQUIRE(all3.size() == 4);
    CHECK(all3[2]->targets.nodes == std::vector<c::ExternalId>{5, 6});

    // Without targets: refused.
    const auto none = s::assign(document, s::OffsetItem{.id = {}, .targets = {}, .data = {}});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().kind == EditRefusal::Kind::NoTargets);
}

// NFR-010-01, REQ-010-23: 100 000 entities in one edit, undone and redone.
TEST_CASE("a large assignment is one edit", "[setup][editing]")
{
    Document document;
    s::Targets many;
    many.entities.reserve(100000);
    for (std::uint64_t e = 1; e <= 100000; ++e) {
        many.entities.push_back(EntityId{e});
    }
    const auto first = applied(
        document, s::assign(document, s::BarAssignment{.id = {}, .targets = many, .data = {}}));
    s::Targets half;
    for (std::uint64_t e = 1; e <= 100000; e += 2) {
        half.entities.push_back(EntityId{e});
    }
    const auto second = applied(
        document, s::assign(document, s::BarAssignment{.id = {}, .targets = half, .data = {}}));
    auto bars = all<s::BarAssignment>(document);
    REQUIRE(bars.size() == 2);
    CHECK(bars[0]->targets.entities.size() == 50000);
    document.revert(second);
    bars = all<s::BarAssignment>(document);
    REQUIRE(bars.size() == 1);
    CHECK(bars[0]->targets.entities.size() == 100000);
    document.apply(second);
    CHECK(all<s::BarAssignment>(document).size() == 2);
    (void)first;
}
