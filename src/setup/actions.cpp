#include <fem/setup/actions.hpp>

#include <algorithm>

// The categories of actions (design 010 §5.10). Each standard is a function
// that returns its rows; adding a standard is adding a function to the list
// in actionCategories(), adding a category is adding a row.

namespace fem::setup {

namespace {

// A category without combination factors.
ActionCategory fixedRow(const char* standard, const char* code, const char* name,
                        ActionNature nature)
{
    return {.standard = standard,
            .code = code,
            .name = name,
            .nature = nature,
            .psi0 = {},
            .psi1 = {},
            .psi2 = {}};
}

// A variable action with its psi0, psi1 and psi2.
ActionCategory variableRow(const char* standard, const char* code, const char* name, double psi0,
                           double psi1, double psi2)
{
    return {.standard = standard,
            .code = code,
            .name = name,
            .nature = ActionNature::Variable,
            .psi0 = psi0,
            .psi1 = psi1,
            .psi2 = psi2};
}

// ABNT NBR 8681; the psi of table 11.2 of ABNT NBR 6118, which reproduces
// them.
void nbr8681(std::vector<ActionCategory>& out)
{
    constexpr const char* kNbr = "ABNT NBR 8681";
    out.push_back(fixedRow(kNbr, "g1", "Peso próprio da estrutura", ActionNature::Permanent));
    out.push_back(fixedRow(kNbr, "g2", "Elementos construtivos e instalações permanentes",
                           ActionNature::Permanent));
    out.push_back(fixedRow(kNbr, "ε",
                           "Ações permanentes indiretas (recalques, retração, protensão)",
                           ActionNature::Permanent));
    out.push_back(variableRow(kNbr, "q1",
                              "Uso, locais sem predominância de equipamentos fixos nem altas "
                              "concentrações de pessoas (residenciais)",
                              0.5, 0.4, 0.3));
    out.push_back(variableRow(kNbr, "q2",
                              "Uso, locais com predominância de equipamentos fixos ou altas "
                              "concentrações de pessoas (comerciais, escritórios)",
                              0.7, 0.6, 0.4));
    out.push_back(
        variableRow(kNbr, "q3", "Uso, bibliotecas, arquivos, oficinas e garagens", 0.8, 0.7, 0.6));
    out.push_back(variableRow(kNbr, "v", "Vento", 0.6, 0.3, 0.0));
    out.push_back(variableRow(kNbr, "t", "Temperatura (variações uniformes)", 0.6, 0.5, 0.3));
    out.push_back(fixedRow(kNbr, "e", "Excepcional", ActionNature::Accidental));
    out.push_back(fixedRow(kNbr, "s", "Sísmica (ABNT NBR 15421)", ActionNature::Seismic));
}

// EN 1990, annex A1, table A1.1 (buildings).
void en1990(std::vector<ActionCategory>& out)
{
    constexpr const char* kEn = "EN 1990";
    out.push_back(fixedRow(kEn, "G", "Permanent", ActionNature::Permanent));
    out.push_back(fixedRow(kEn, "P", "Prestress", ActionNature::Permanent));
    out.push_back(
        variableRow(kEn, "Q-A", "Imposed, category A: domestic, residential", 0.7, 0.5, 0.3));
    out.push_back(variableRow(kEn, "Q-B", "Imposed, category B: offices", 0.7, 0.5, 0.3));
    out.push_back(
        variableRow(kEn, "Q-C", "Imposed, category C: congregation areas", 0.7, 0.7, 0.6));
    out.push_back(variableRow(kEn, "Q-D", "Imposed, category D: shopping areas", 0.7, 0.7, 0.6));
    out.push_back(variableRow(kEn, "Q-E", "Imposed, category E: storage areas", 1.0, 0.9, 0.8));
    out.push_back(variableRow(kEn, "Q-F", "Imposed, category F: traffic, vehicles up to 30 kN", 0.7,
                              0.7, 0.6));
    out.push_back(
        variableRow(kEn, "Q-G", "Imposed, category G: traffic, 30 to 160 kN", 0.7, 0.5, 0.3));
    out.push_back(variableRow(kEn, "Q-H", "Imposed, category H: roofs", 0.0, 0.0, 0.0));
    out.push_back(variableRow(kEn, "Q-S",
                              "Snow (altitude up to 1000 m, outside Finland, Iceland, Norway, "
                              "Sweden)",
                              0.5, 0.2, 0.0));
    out.push_back(variableRow(kEn, "Q-W", "Wind", 0.6, 0.2, 0.0));
    out.push_back(variableRow(kEn, "Q-T", "Temperature (non-fire)", 0.6, 0.5, 0.0));
    out.push_back(fixedRow(kEn, "A", "Accidental", ActionNature::Accidental));
    out.push_back(fixedRow(kEn, "AE", "Seismic", ActionNature::Seismic));
}

// ASCE 7, the loads that AISC 360 and ACI 318 combine (by load factors,
// without psi).
void asce7(std::vector<ActionCategory>& out)
{
    constexpr const char* kAsce = "ASCE 7";
    out.push_back(fixedRow(kAsce, "D", "Dead", ActionNature::Permanent));
    out.push_back(fixedRow(kAsce, "L", "Live", ActionNature::Variable));
    out.push_back(fixedRow(kAsce, "Lr", "Roof live", ActionNature::Variable));
    out.push_back(fixedRow(kAsce, "S", "Snow", ActionNature::Variable));
    out.push_back(fixedRow(kAsce, "R", "Rain", ActionNature::Variable));
    out.push_back(fixedRow(kAsce, "W", "Wind", ActionNature::Variable));
    out.push_back(fixedRow(kAsce, "E", "Earthquake", ActionNature::Seismic));
    out.push_back(fixedRow(kAsce, "H", "Lateral earth pressure", ActionNature::Permanent));
    out.push_back(fixedRow(kAsce, "F", "Fluids", ActionNature::Permanent));
    out.push_back(fixedRow(kAsce, "T", "Self-straining", ActionNature::Variable));
}

std::vector<ActionCategory> build()
{
    std::vector<ActionCategory> out;
    // The standards, in the order of the table of design §5.10.
    for (const auto standard : {nbr8681, en1990, asce7}) {
        standard(out);
    }
    return out;
}

} // namespace

const std::vector<ActionCategory>& actionCategories()
{
    static const std::vector<ActionCategory> categories = build();
    return categories;
}

const ActionCategory* findCategory(std::string_view standard, std::string_view code)
{
    const auto& all = actionCategories();
    const auto it = std::ranges::find_if(
        all, [&](const ActionCategory& c) { return c.standard == standard && c.code == code; });
    return it == all.end() ? nullptr : &*it;
}

} // namespace fem::setup
