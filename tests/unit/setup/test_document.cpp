#include <fem/setup/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <variant>
#include <vector>

// Feature 010, T04 (design §4, §5.2): REQ-010-01, 23.

namespace {

using fem::setup::Document;
using fem::setup::DocumentEdit;
using fem::setup::Item;
using fem::setup::ItemId;

std::uint64_t value(ItemId id)
{
    return static_cast<std::uint64_t>(id);
}

// One item of each kind, each with a fresh id.
std::vector<Item> oneOfEach(Document& document)
{
    namespace s = fem::setup;
    std::vector<Item> items;
    items.emplace_back(s::MaterialItem{.id = document.nextId(), .data = {.name = "steel"}});
    items.emplace_back(s::SectionItem{.id = document.nextId(), .data = {.name = "I"}});
    s::Targets beams;
    beams.entities = {fem::geom::EntityId{1}, fem::geom::EntityId{2}};
    items.emplace_back(s::BarAssignment{.id = document.nextId(), .targets = beams, .data = {}});
    items.emplace_back(s::ConnectionItem{.id = document.nextId(), .targets = beams, .data = {}});
    items.emplace_back(s::OffsetItem{.id = document.nextId(), .targets = beams, .data = {}});
    items.emplace_back(s::FoundationItem{.id = document.nextId(), .targets = beams, .data = {}});
    items.emplace_back(s::WarpingEndItem{.id = document.nextId(), .targets = beams, .data = {}});
    s::Targets nodes;
    nodes.nodes = {3, 4};
    nodes.meshGeneration = 2;
    items.emplace_back(s::SupportItem{.id = document.nextId(), .targets = nodes, .held = {}});
    const ItemId dead = document.nextId();
    items.emplace_back(s::LoadCase{.id = dead, .name = "G"});
    items.emplace_back(s::LoadItem{.id = document.nextId(),
                                   .loadCase = dead,
                                   .targets = {},
                                   .action = s::SelfWeight{.acceleration = {0, 0, -9.81}}});
    items.emplace_back(
        s::Combination{.id = document.nextId(), .name = "ULS", .terms = {{dead, 1.35}}});
    return items;
}

} // namespace

TEST_CASE("ids are fresh and never come back", "[setup][document]")
{
    Document document;
    const ItemId a = document.nextId();
    const ItemId b = document.nextId();
    CHECK(value(a) == 1);
    CHECK(value(b) == 2);
    DocumentEdit edit;
    edit.added.emplace_back(fem::setup::LoadCase{.id = b, .name = "Q"});
    document.apply(edit);
    document.revert(edit);
    // Undone, the id is not handed out again.
    CHECK(value(document.nextId()) == 3);
}

TEST_CASE("every kind of item goes in and out", "[setup][document]")
{
    Document document;
    DocumentEdit edit;
    edit.added = oneOfEach(document);
    const std::uint64_t before = document.revision();
    document.apply(edit);
    CHECK(document.revision() != before);
    REQUIRE(document.size() == edit.added.size());
    // In the order made, and found by id.
    for (std::size_t i = 0; i < edit.added.size(); ++i) {
        const ItemId id = fem::setup::idOf(edit.added[i]);
        CHECK(fem::setup::idOf(document.items()[i]) == id);
        const Item* found = document.find(id);
        REQUIRE(found != nullptr);
        CHECK(found->index() == edit.added[i].index());
    }
    CHECK(document.find(ItemId{999}) == nullptr);
    // The targets of the kinds that have them.
    const Item* support = document.find(fem::setup::idOf(edit.added[7]));
    REQUIRE(support != nullptr);
    const auto* targets = fem::setup::targetsOf(*support);
    REQUIRE(targets != nullptr);
    CHECK(targets->nodes == std::vector<fem::core::ExternalId>{3, 4});
    CHECK(targets->meshGeneration == 2);
    CHECK(fem::setup::targetsOf(*document.find(fem::setup::idOf(edit.added[0]))) == nullptr);

    document.revert(edit);
    CHECK(document.size() == 0);
}

TEST_CASE("a change keeps the place of the item", "[setup][document]")
{
    Document document;
    DocumentEdit create;
    create.added = oneOfEach(document);
    document.apply(create);

    // The material renamed: out with the old, in with the new, same id.
    const auto old = std::get<fem::setup::MaterialItem>(create.added[0]);
    auto renamed = old;
    renamed.data.name = "S355";
    DocumentEdit change;
    change.removed.emplace_back(old);
    change.added.emplace_back(renamed);
    document.apply(change);
    REQUIRE(document.size() == create.added.size());
    const auto& first = std::get<fem::setup::MaterialItem>(document.items()[0]);
    CHECK(first.data.name == "S355");
    document.revert(change);
    CHECK(std::get<fem::setup::MaterialItem>(document.items()[0]).data.name == "steel");

    // A removed item comes back in its place on revert.
    DocumentEdit remove;
    remove.removed.emplace_back(create.added[3]);
    document.apply(remove);
    CHECK(document.size() == create.added.size() - 1);
    CHECK(document.find(fem::setup::idOf(create.added[3])) == nullptr);
    document.revert(remove);
    CHECK(fem::setup::idOf(document.items()[3]) == fem::setup::idOf(create.added[3]));
}

TEST_CASE("the units and the active case in an edit", "[setup][document]")
{
    Document document;
    CHECK(document.units() == fem::setup::UnitSystem::KilonewtonMetre);
    CHECK_FALSE(document.activeCase().has_value());
    const ItemId g = document.nextId();
    DocumentEdit edit;
    edit.added.emplace_back(fem::setup::LoadCase{.id = g, .name = "G"});
    edit.units = std::pair{fem::setup::UnitSystem::KilonewtonMetre,
                           fem::setup::UnitSystem::NewtonMillimetre};
    edit.activeCase = std::pair{std::optional<ItemId>{}, std::optional<ItemId>{g}};
    document.apply(edit);
    CHECK(document.units() == fem::setup::UnitSystem::NewtonMillimetre);
    CHECK(document.activeCase() == g);
    document.revert(edit);
    CHECK(document.units() == fem::setup::UnitSystem::KilonewtonMetre);
    CHECK_FALSE(document.activeCase().has_value());
    CHECK(document.size() == 0);
}
