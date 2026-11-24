#include <fem/setup/convert.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Feature 010, T05 (design §5.1): REQ-010-03, NFR-010-02. Every field of
// every core type in the document, listed here on their own (not from the
// code under test), with whether it has a unit.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::setup::UnitSystem;

struct Field
{
    std::string name;
    double value = 0.0;
    bool dimensioned = true;
};

using Fields = std::vector<Field>;

void add(Fields& out, const std::string& name, double v, bool dim = true)
{
    out.push_back({.name = name, .value = v, .dimensioned = dim});
}

void add(Fields& out, const std::string& name, const Eigen::Vector2d& v, bool dim = true)
{
    add(out, name + ".y", v.x(), dim);
    add(out, name + ".z", v.y(), dim);
}

void add(Fields& out, const std::string& name, const Eigen::Vector3d& v, bool dim = true)
{
    add(out, name + ".x", v.x(), dim);
    add(out, name + ".y", v.y(), dim);
    add(out, name + ".z", v.z(), dim);
}

void addShape(Fields& out, const c::SectionShape& shape)
{
    std::visit(
        [&](const auto& sh) {
            using T = std::decay_t<decltype(sh)>;
            if constexpr (std::is_same_v<T, c::Rectangle>) {
                add(out, "b", sh.b);
                add(out, "h", sh.h);
            } else if constexpr (std::is_same_v<T, c::Circle>) {
                add(out, "d", sh.d);
            } else if constexpr (std::is_same_v<T, c::CircularTube>) {
                add(out, "d", sh.d);
                add(out, "t", sh.t);
            } else if constexpr (std::is_same_v<T, c::RectangularTube> ||
                                 std::is_same_v<T, c::Angle>) {
                add(out, "b", sh.b);
                add(out, "h", sh.h);
                add(out, "t", sh.t);
            } else if constexpr (std::is_same_v<T, c::ISection> || std::is_same_v<T, c::Channel> ||
                                 std::is_same_v<T, c::Tee>) {
                add(out, "h", sh.h);
                add(out, "bf", sh.bf);
                add(out, "tf", sh.tf);
                add(out, "tw", sh.tw);
            } else if constexpr (std::is_same_v<T, c::ThinWalled>) {
                for (const auto& p : sh.points) {
                    add(out, "point", p);
                }
                for (const auto& w : sh.walls) {
                    add(out, "wall.t", w.t);
                }
            } else if constexpr (std::is_same_v<T, c::SolidPolygon>) {
                for (const auto& region : sh.regions) {
                    for (const auto& p : region.outline) {
                        add(out, "outline", p);
                    }
                    for (const auto& hole : region.holes) {
                        for (const auto& p : hole) {
                            add(out, "hole", p);
                        }
                    }
                }
                add(out, "meshSize", sh.meshSize);
            }
        },
        shape);
}

Fields fieldsOf(const s::Item& item)
{
    Fields out;
    std::visit(
        [&](const auto& i) {
            using T = std::decay_t<decltype(i)>;
            if constexpr (std::is_same_v<T, s::MaterialItem>) {
                add(out, "E", *i.data.youngsModulus);
                add(out, "nu", *i.data.poissonsRatio, false);
                add(out, "rho", *i.data.density);
                add(out, "alpha", *i.data.thermalExpansion);
                add(out, "k", *i.data.conductivity);
                add(out, "c", *i.data.specificHeat);
                for (const auto& p : i.properties) {
                    add(out, p.key, p.value, !(p.quantity == s::quantity::kNone));
                }
            } else if constexpr (std::is_same_v<T, s::SectionItem>) {
                const c::Section& sec = i.data;
                if (sec.area) {
                    add(out, "A", *sec.area);
                    add(out, "Iy", *sec.inertiaY);
                    add(out, "Iz", *sec.inertiaZ);
                    add(out, "Iyz", *sec.productOfInertia);
                    add(out, "J", *sec.torsionConstant);
                    add(out, "sc", *sec.shearCentre);
                    add(out, "Asy", *sec.shearAreaY);
                    add(out, "Asz", *sec.shearAreaZ);
                    add(out, "Iw", *sec.warpingConstant);
                    add(out, "by", *sec.monosymmetryY);
                    add(out, "bz", *sec.monosymmetryZ);
                }
                if (sec.shape) {
                    addShape(out, *sec.shape);
                }
            } else if constexpr (std::is_same_v<T, s::BarAssignment>) {
                add(out, "reference", *i.data.reference, false);
                add(out, "roll", i.data.rollAngle, false);
            } else if constexpr (std::is_same_v<T, s::ConnectionItem>) {
                for (std::size_t k = 0; k < 6; ++k) {
                    add(out, "spring" + std::to_string(k), *i.data.stiffness.at(k));
                }
            } else if constexpr (std::is_same_v<T, s::OffsetItem>) {
                add(out, "start", i.data.start);
                add(out, "end", i.data.end);
            } else if constexpr (std::is_same_v<T, s::FoundationItem>) {
                add(out, "k", i.data.translational);
                add(out, "kt", i.data.torsional);
            } else if constexpr (std::is_same_v<T, s::WarpingEndItem>) {
                add(out, "kw", i.data.stiffness);
            } else if constexpr (std::is_same_v<T, s::LoadItem>) {
                std::visit(
                    [&](const auto& a) {
                        using A = std::decay_t<decltype(a)>;
                        if constexpr (std::is_same_v<A, c::NodalForce>) {
                            add(out, "F", a.force);
                            add(out, "M", a.moment);
                            add(out, "B", a.bimoment);
                        } else if constexpr (std::is_same_v<A, c::DistributedLoad>) {
                            add(out, "q0", a.startValue);
                            add(out, "q1", a.endValue);
                            add(out, "from", a.from.value, !a.from.fraction);
                            add(out, "to", a.to.value, !a.to.fraction);
                            add(out, "m0", a.startTorque);
                            add(out, "m1", a.endTorque);
                            add(out, "point", *a.point);
                        } else if constexpr (std::is_same_v<A, c::ConcentratedLoad>) {
                            add(out, "F", a.force);
                            add(out, "M", a.moment);
                            add(out, "at", a.at.value, !a.at.fraction);
                            add(out, "point", *a.point);
                        } else if constexpr (std::is_same_v<A, s::SelfWeight>) {
                            add(out, "g", a.acceleration);
                        } else if constexpr (std::is_same_v<A, c::TemperatureChange>) {
                            add(out, "dT", a.deltaT);
                        } else if constexpr (std::is_same_v<A, c::TemperatureGradient>) {
                            add(out, "dTy", a.alongY);
                            add(out, "dTz", a.alongZ);
                        } else if constexpr (std::is_same_v<A, c::InitialStrain>) {
                            add(out, "eps", a.strain, false);
                        } else if constexpr (std::is_same_v<A, s::Settlement>) {
                            const bool rotation = a.dof == c::DofType::RX ||
                                                  a.dof == c::DofType::RY ||
                                                  a.dof == c::DofType::RZ;
                            add(out, "value", a.value, !rotation);
                        }
                    },
                    i.action);
            }
        },
        item);
    return out;
}

// One item of each kind with every field set and nonzero.
std::vector<s::Item> everything()
{
    std::vector<s::Item> items;
    std::uint64_t id = 0;
    const auto next = [&] { return s::ItemId{++id}; };
    items.emplace_back(s::MaterialItem{
        .id = next(),
        .data = {.name = "m",
                 .youngsModulus = 2.1e8,
                 .poissonsRatio = 0.3,
                 .density = 7.85,
                 .thermalExpansion = 1.2e-5,
                 .conductivity = 50.0,
                 .specificHeat = 460.0},
        .standard = "EN 1993-1-1",
        .grade = "S355",
        .properties = {{.key = "fy", .quantity = s::quantity::kStress, .value = 3.55e5},
                       {.key = "ratio", .quantity = s::quantity::kNone, .value = 0.5}}});
    items.emplace_back(s::SectionItem{.id = next(),
                                      .data = {.name = "generic",
                                               .area = 0.01,
                                               .inertiaY = 2e-5,
                                               .inertiaZ = 3e-5,
                                               .productOfInertia = 1e-6,
                                               .torsionConstant = 4e-7,
                                               .shearCentre = Eigen::Vector2d(0.01, -0.02),
                                               .shearAreaY = 0.004,
                                               .shearAreaZ = 0.005,
                                               .warpingConstant = 1e-8,
                                               .monosymmetryY = 0.05,
                                               .monosymmetryZ = -0.03}});
    const std::vector<c::SectionShape> shapes{
        c::Rectangle{.b = 0.3, .h = 0.5},
        c::Circle{.d = 0.2},
        c::CircularTube{.d = 0.3, .t = 0.01},
        c::RectangularTube{.b = 0.2, .h = 0.3, .t = 0.01},
        c::ISection{.h = 0.4, .bf = 0.2, .tf = 0.015, .tw = 0.01},
        c::Angle{.b = 0.1, .h = 0.15, .t = 0.01},
        c::Channel{.h = 0.3, .bf = 0.1, .tf = 0.012, .tw = 0.008},
        c::Tee{.h = 0.25, .bf = 0.2, .tf = 0.02, .tw = 0.01},
        c::ThinWalled{.points = {{0.01, 0.1}, {0.1, 0.11}, {0.12, -0.1}},
                      .walls = {{.from = 0, .to = 1, .t = 0.01}, {.from = 1, .to = 2, .t = 0.02}}},
        c::SolidPolygon{.regions = {{.outline = {{0.01, 0.02}, {0.2, 0.03}, {0.21, 0.3}},
                                     .holes = {{{0.05, 0.05}, {0.1, 0.05}, {0.1, 0.1}}}}},
                        .meshSize = 0.01},
    };
    for (const auto& shape : shapes) {
        items.emplace_back(s::SectionItem{.id = next(), .data = {.name = "shape", .shape = shape}});
    }
    items.emplace_back(s::BarAssignment{.id = next(),
                                        .targets = {},
                                        .data = {.elements = {},
                                                 .material = {},
                                                 .section = {},
                                                 .reference = Eigen::Vector3d(0.0, 0.6, 0.8),
                                                 .rollAngle = 0.4}});
    c::EndConnection connection;
    connection.stiffness = {1e3, 2e3, 3e3, 4e3, 5e3, 6e3};
    items.emplace_back(s::ConnectionItem{.id = next(), .targets = {}, .data = connection});
    items.emplace_back(s::OffsetItem{.id = next(),
                                     .targets = {},
                                     .data = {.elements = {},
                                              .start = Eigen::Vector3d(0.1, 0.2, 0.3),
                                              .end = Eigen::Vector3d(-0.1, 0.05, 0.5)}});
    items.emplace_back(s::FoundationItem{.id = next(),
                                         .targets = {},
                                         .data = {.elements = {},
                                                  .translational = Eigen::Vector3d(1e4, 2e4, 3e4),
                                                  .torsional = 5e2}});
    items.emplace_back(
        s::WarpingEndItem{.id = next(), .targets = {}, .data = {.elements = {}, .stiffness = 7.5}});
    const s::ItemId loadCase = next();
    items.emplace_back(s::LoadCase{.id = loadCase, .name = "G", .standard = {}, .category = {}});
    const std::vector<s::Action> actions{
        c::NodalForce{.nodes = {},
                      .force = Eigen::Vector3d(1, 2, 3),
                      .moment = Eigen::Vector3d(4, 5, 6),
                      .bimoment = 7},
        c::DistributedLoad{.elements = {},
                           .axes = c::Axes::Local,
                           .startValue = Eigen::Vector3d(1, 2, 3),
                           .endValue = Eigen::Vector3d(4, 5, 6),
                           .from = {.value = 0.5, .fraction = false},
                           .to = {.value = 0.75, .fraction = true},
                           .startTorque = 0.2,
                           .endTorque = 0.3,
                           .point = Eigen::Vector2d(0.1, 0.05)},
        c::ConcentratedLoad{.elements = {},
                            .force = Eigen::Vector3d(7, 8, 9),
                            .moment = Eigen::Vector3d(1, 1, 2),
                            .at = {.value = 1.5, .fraction = false},
                            .point = Eigen::Vector2d(-0.1, 0.02)},
        s::SelfWeight{.acceleration = Eigen::Vector3d(0.1, 0.2, -9.81)},
        c::TemperatureChange{.elements = {}, .deltaT = 30.0},
        c::TemperatureGradient{.elements = {}, .alongY = 10.0, .alongZ = -5.0},
        c::InitialStrain{.elements = {}, .strain = 1e-4},
        s::Settlement{.dof = c::DofType::W, .value = -0.01},
        s::Settlement{.dof = c::DofType::RY, .value = 0.002},
    };
    for (const auto& action : actions) {
        items.emplace_back(
            s::LoadItem{.id = next(), .loadCase = loadCase, .targets = {}, .action = action});
    }
    return items;
}

bool close(double a, double b)
{
    return std::abs(a - b) <= 1e-12 * std::max(std::abs(a), std::abs(b));
}

} // namespace

TEST_CASE("every field of every item is converted", "[setup][units][convert]")
{
    for (const s::Item& original : everything()) {
        const Fields before = fieldsOf(original);
        s::Item item = original;
        s::convertItem(item, UnitSystem::NewtonMetre, UnitSystem::PoundInch);
        const Fields there = fieldsOf(item);
        s::convertItem(item, UnitSystem::PoundInch, UnitSystem::NewtonMetre);
        const Fields back = fieldsOf(item);
        REQUIRE(there.size() == before.size());
        REQUIRE(back.size() == before.size());
        for (std::size_t k = 0; k < before.size(); ++k) {
            INFO("item kind " << original.index() << ", field " << before[k].name);
            CHECK(close(back[k].value, before[k].value));
            if (before[k].dimensioned) {
                CHECK(there[k].value != before[k].value);
            } else {
                CHECK(there[k].value == before[k].value);
            }
        }
    }
}

TEST_CASE("a value converted is the physical quantity", "[setup][units][convert]")
{
    // 210 GPa in N-mm is 210 000 N/mm²; a 9.81 m/s² gravity is 9810 mm/s².
    s::Item material =
        s::MaterialItem{.id = s::ItemId{1},
                        .data = {.name = "m", .youngsModulus = 2.1e11, .density = 7850.0},
                        .standard = {},
                        .grade = {},
                        .properties = {}};
    s::convertItem(material, UnitSystem::NewtonMetre, UnitSystem::NewtonMillimetre);
    const auto& m = std::get<s::MaterialItem>(material).data;
    CHECK(close(*m.youngsModulus, 2.1e5));
    CHECK(close(*m.density, 7.85e-9));
    s::Item weight = s::LoadItem{.id = s::ItemId{2},
                                 .loadCase = s::ItemId{3},
                                 .targets = {},
                                 .action = s::SelfWeight{.acceleration = {0, 0, -9.81}}};
    s::convertItem(weight, UnitSystem::NewtonMetre, UnitSystem::NewtonMillimetre);
    CHECK(close(std::get<s::SelfWeight>(std::get<s::LoadItem>(weight).action).acceleration.z(),
                -9810.0));
}

// REQ-010-03: one edit converts the document, and undoing restores what was
// typed exactly.
TEST_CASE("the units of a document are changed in one edit", "[setup][units][convert]")
{
    s::Document document;
    s::DocumentEdit create;
    for (s::Item& item : everything()) {
        // Fresh ids of this document.
        std::visit([&](auto& i) { i.id = document.nextId(); }, item);
        create.added.push_back(item);
    }
    // The loads must refer to the load case of this document.
    s::ItemId loadCase{};
    for (const s::Item& item : create.added) {
        if (std::holds_alternative<s::LoadCase>(item)) {
            loadCase = s::idOf(item);
        }
    }
    for (s::Item& item : create.added) {
        if (auto* load = std::get_if<s::LoadItem>(&item)) {
            load->loadCase = loadCase;
        }
    }
    document.apply(create);
    const std::vector<s::Item> typed(document.items().begin(), document.items().end());

    const s::DocumentEdit change = s::changeUnits(document, UnitSystem::NewtonMillimetre);
    const auto units =
        change.units.value_or(std::pair{UnitSystem::PoundInch, UnitSystem::PoundInch});
    CHECK(units.first == UnitSystem::KilonewtonMetre);
    CHECK(units.second == UnitSystem::NewtonMillimetre);
    document.apply(change);
    CHECK(document.units() == UnitSystem::NewtonMillimetre);
    // E: 2.1e8 kN/m² is 2.1e5 N/mm².
    CHECK(close(*std::get<s::MaterialItem>(document.items()[0]).data.youngsModulus, 2.1e5));

    document.revert(change);
    CHECK(document.units() == UnitSystem::KilonewtonMetre);
    REQUIRE(document.size() == typed.size());
    for (std::size_t k = 0; k < typed.size(); ++k) {
        const Fields a = fieldsOf(typed[k]);
        const Fields b = fieldsOf(document.items()[k]);
        REQUIRE(a.size() == b.size());
        for (std::size_t f = 0; f < a.size(); ++f) {
            CHECK(a[f].value == b[f].value); // exactly what was typed
        }
    }
    // The same system: nothing to do.
    CHECK(s::changeUnits(document, UnitSystem::KilonewtonMetre).empty());
}
