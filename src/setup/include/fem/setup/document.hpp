#pragma once

// Feature 010: the model document (design 010 §4, §5.2, §5.3).

#include <fem/core/dof_map.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/model.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/setup/units.hpp>

#include <Eigen/Core>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fem::setup {

// A stable identity of an item; 0 is never used, ids are never reused.
enum class ItemId : std::uint64_t {};

// REQ-010-12: a mesh group, by name and dimension.
struct GroupTarget
{
    std::string name;
    int dimension = 1;

    friend bool operator==(const GroupTarget&, const GroupTarget&) = default;
};

// REQ-010-11, 12: what an item acts on, never mesh numbers alone:
// drawing entities, mesh groups, and elements and nodes picked in the mesh
// of `meshGeneration` (REQ-010-13).
struct Targets
{
    std::vector<geom::EntityId> entities;
    std::vector<GroupTarget> groups;
    std::vector<core::ExternalId> elements;
    std::vector<core::ExternalId> nodes;
    std::uint64_t meshGeneration = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return entities.empty() && groups.empty() && elements.empty() && nodes.empty();
    }
    friend bool operator==(const Targets&, const Targets&) = default;
};

// REQ-010-04: an extra property of a material, only informative (fy, fu,
// fck, G...): its key, its quantity for the unit conversion, its value.
// New keys need no change in the conversion (design §5.9).
struct MaterialProperty
{
    std::string key;
    Quantity quantity;
    double value = 0.0;

    friend bool operator==(const MaterialProperty&, const MaterialProperty&) = default;
};

// REQ-010-04, 05: the data of each item is the core type; where the core
// type has a target of its own, it is not used: `targets` is.
struct MaterialItem
{
    ItemId id{};
    core::Material data;
    std::string standard; // "EN 1993-1-1"; empty for a material of the user's
    std::string grade;    // "S355"
    std::vector<MaterialProperty> properties;
};

struct SectionItem
{
    ItemId id{};
    core::Section data;
};

// REQ-010-07
struct BarAssignment
{
    ItemId id{};
    Targets targets;
    core::Assignment data;
};

// REQ-010-08
struct ConnectionItem
{
    ItemId id{};
    Targets targets;
    core::EndConnection data;
};

struct OffsetItem
{
    ItemId id{};
    Targets targets;
    core::RigidOffset data;
};

struct FoundationItem
{
    ItemId id{};
    Targets targets;
    core::ElasticFoundation data;
};

struct WarpingEndItem
{
    ItemId id{};
    Targets targets;
    core::WarpingEnd data;
};

// REQ-010-15: the dofs held (value 0); settlements are loads.
struct SupportItem
{
    ItemId id{};
    Targets targets;
    std::array<bool, core::kDofTypeCount> held{};
};

// REQ-010-16: the actions of a load case.
struct Settlement
{
    core::DofType dof = core::DofType::U;
    double value = 0.0;
};

struct SelfWeight
{
    Eigen::Vector3d acceleration = Eigen::Vector3d::Zero();
};

using Action = std::variant<core::NodalForce, core::DistributedLoad, core::ConcentratedLoad,
                            SelfWeight, core::TemperatureChange, core::TemperatureGradient,
                            core::InitialStrain, Settlement>;

struct LoadCase
{
    ItemId id{};
    std::string name;
    // REQ-010-26: the standard and the category of its action (keys of
    // actionCategories()); empty for none.
    std::string standard;
    std::string category;
};

struct LoadItem
{
    ItemId id{};
    ItemId loadCase{};
    Targets targets; // none for a self-weight
    Action action;
};

// REQ-010-17: a sum of load cases times factors.
struct Combination
{
    ItemId id{};
    std::string name;
    std::vector<std::pair<ItemId, double>> terms;
};

using Item =
    std::variant<MaterialItem, SectionItem, BarAssignment, ConnectionItem, OffsetItem,
                 FoundationItem, WarpingEndItem, SupportItem, LoadCase, LoadItem, Combination>;

[[nodiscard]] ItemId idOf(const Item& item);
// The targets of an item, or null for items without (materials, sections,
// load cases, combinations).
[[nodiscard]] const Targets* targetsOf(const Item& item);
[[nodiscard]] Targets* targetsOf(Item& item);

// REQ-010-23: one reversible change: the items taken out and put in (the
// same id in both is a change of that item), and the unit system and the
// active case before and after.
struct DocumentEdit
{
    std::vector<Item> removed;
    std::vector<Item> added;
    std::optional<std::pair<UnitSystem, UnitSystem>> units;
    std::optional<std::pair<std::optional<ItemId>, std::optional<ItemId>>> activeCase;

    [[nodiscard]] bool empty() const noexcept
    {
        return removed.empty() && added.empty() && !units && !activeCase;
    }
};

// REQ-010-01: the model document. A value type, without Qt; items are kept
// in the order they were made (by id), and keep their id for ever.
class Document
{
public:
    [[nodiscard]] UnitSystem units() const noexcept { return m_units; }
    [[nodiscard]] std::optional<ItemId> activeCase() const noexcept { return m_activeCase; }
    // Changes on every apply and revert.
    [[nodiscard]] std::uint64_t revision() const noexcept { return m_revision; }

    // Reserves a fresh id.
    [[nodiscard]] ItemId nextId() noexcept;

    [[nodiscard]] const Item* find(ItemId id) const;
    [[nodiscard]] std::span<const Item> items() const noexcept { return m_items; }
    [[nodiscard]] std::size_t size() const noexcept { return m_items.size(); }

    // Takes out `edit.removed`, puts in `edit.added`, then sets the units and
    // the active case to their values after.
    void apply(const DocumentEdit& edit);
    // The inverse of apply.
    void revert(const DocumentEdit& edit);

private:
    void insert(const Item& item);
    void erase(ItemId id);

    std::vector<Item> m_items; // sorted by id
    UnitSystem m_units = UnitSystem::KilonewtonMetre;
    std::optional<ItemId> m_activeCase;
    std::uint64_t m_lastId = 0;
    std::uint64_t m_revision = 0;
};

} // namespace fem::setup
