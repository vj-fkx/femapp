#pragma once

// Feature 010: the categories of actions by standard (design 010 §5.10,
// REQ-010-26). A data table: a standard is a function returning its rows, a
// category is a row. Informative: the analysis does not use it; the factors
// psi0, psi1 and psi2 are there for a future generation of combinations.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fem::setup {

enum class ActionNature : std::uint8_t { Permanent, Variable, Accidental, Seismic };

struct ActionCategory
{
    std::string standard; // "EN 1990"
    std::string code;     // "Q-B"
    std::string name;     // "Imposed, category B: offices"
    ActionNature nature = ActionNature::Permanent;
    // The combination factors, when the standard gives them.
    std::optional<double> psi0;
    std::optional<double> psi1;
    std::optional<double> psi2;
};

// Every category of every standard, in the order of the table of design
// §5.10.
[[nodiscard]] const std::vector<ActionCategory>& actionCategories();

// The category `code` of `standard`, or null.
[[nodiscard]] const ActionCategory* findCategory(std::string_view standard, std::string_view code);

} // namespace fem::setup
