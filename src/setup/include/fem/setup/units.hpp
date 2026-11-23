#pragma once

// Feature 010: the unit systems of the model document (design 010 §5.1).

#include <array>
#include <cstdint>

namespace fem::setup {

// REQ-010-02: the coherent systems offered.
enum class UnitSystem : std::uint8_t {
    NewtonMetre,
    KilonewtonMetre,
    NewtonMillimetre,
    KilogramForceCentimetre,
    PoundInch
};

inline constexpr std::array<UnitSystem, 5> kUnitSystems{
    UnitSystem::NewtonMetre, UnitSystem::KilonewtonMetre, UnitSystem::NewtonMillimetre,
    UnitSystem::KilogramForceCentimetre, UnitSystem::PoundInch};

} // namespace fem::setup
